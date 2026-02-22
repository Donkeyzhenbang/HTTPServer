#include "../inc/DistributedCoord.h"
#include "../inc/ServerApp.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>

namespace gw {

DistributedCoord::DistributedCoord() {
}

DistributedCoord::~DistributedCoord() {
}

DistributedCoord& DistributedCoord::Instance() {
    static DistributedCoord instance;
    return instance;
}

void DistributedCoord::Init(ZkClient* zk_client, const std::string& local_ip, int tcp_port, int http_port) {
    zk_client_ = zk_client;
    local_ip_ = local_ip;
    local_tcp_port_ = tcp_port;
    local_http_port_ = http_port;
    local_node_id_ = "node_" + local_ip + "_" + std::to_string(tcp_port);

    // 注册当前节点
    RegisterNode();

    // 启动各项监听
    StartNodeWatcher();
    StartElectionWatcher();

    std::cout << "[DistCoord] Initialized, local_node_id: " << local_node_id_ << std::endl;
}

std::string DistributedCoord::BuildNodeData() {
    std::ostringstream oss;
    oss << "{"
        << "\"ip\":\"" << local_ip_ << "\","
        << "\"tcp_port\":" << local_tcp_port_ << ","
        << "\"http_port\":" << local_http_port_ << ","
        << "\"status\":\"online\","
        << "\"timestamp\":" << std::time(nullptr) << ","
        << "\"version\":\"1.0.0\""
        << "}";
    return oss.str();
}

void DistributedCoord::RegisterNode() {
    if (!zk_client_) return;

    std::string path = "/gw-server/nodes/" + local_node_id_;
    std::string data = BuildNodeData();

    // 创建临时节点
    zk_client_->Create(path, data, 1); // 1 = Ephemeral
}

NodeInfo DistributedCoord::ParseNodeInfo(const std::string& path, const std::string& data) {
    NodeInfo info;
    info.node_id = path.substr(path.rfind('/') + 1);

    // 简单解析JSON
    size_t p1 = data.find("\"ip\":\"");
    if (p1 != std::string::npos) {
        size_t p2 = data.find("\"", p1 + 6);
        if (p2 != std::string::npos) {
            info.ip = data.substr(p1 + 6, p2 - p1 - 6);
        }
    }

    p1 = data.find("\"tcp_port\":");
    if (p1 != std::string::npos) {
        info.tcp_port = std::stoi(data.substr(p1 + 11));
    }

    p1 = data.find("\"http_port\":");
    if (p1 != std::string::npos) {
        info.http_port = std::stoi(data.substr(p1 + 12));
    }

    p1 = data.find("\"status\":\"");
    if (p1 != std::string::npos) {
        size_t p2 = data.find("\"", p1 + 10);
        if (p2 != std::string::npos) {
            info.status = data.substr(p1 + 10, p2 - p1 - 10);
        }
    }

    return info;
}

void DistributedCoord::OnNodesChanged() {
    auto nodes = GetAllNodes();

    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_ = nodes;

    if (node_change_callback_) {
        node_change_callback_(nodes);
    }
}

void DistributedCoord::StartNodeWatcher() {
    if (!zk_client_) return;

    // 设置Watcher监听节点变化
    zk_client_->SetWatcher("/gw-server/nodes", [this](int type, int state, const std::string& path) {
        if (type == ZOO_CHILD_EVENT) {
            std::cout << "[DistCoord] Nodes changed, refreshing..." << std::endl;
            OnNodesChanged();
        }
    });

    // 初始加载
    OnNodesChanged();
}

std::vector<NodeInfo> DistributedCoord::GetAllNodes() {
    std::vector<NodeInfo> result;

    if (!zk_client_) return result;

    auto children = zk_client_->GetChildren("/gw-server/nodes");
    for (const auto& child : children) {
        std::string path = "/gw-server/nodes/" + child;
        std::string data = zk_client_->GetData(path);
        if (!data.empty()) {
            result.push_back(ParseNodeInfo(path, data));
        }
    }

    return result;
}

void DistributedCoord::WatchNodes(NodeChangeCallback callback) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    node_change_callback_ = callback;
    // 立即触发一次回调
    callback(nodes_);
}

// === 配置管理 ===

std::string DistributedCoord::GetConfig(const std::string& key) {
    if (!zk_client_) return "";

    std::string path = "/gw-server/config/" + key;
    return zk_client_->GetData(path);
}

bool DistributedCoord::SetConfig(const std::string& key, const std::string& value) {
    if (!zk_client_) return false;

    std::string path = "/gw-server/config/" + key;
    return zk_client_->CreateSync(path, value, 0);
}

void DistributedCoord::WatchConfig(const std::string& key, ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_watchers_[key] = callback;

    if (zk_client_) {
        std::string path = "/gw_server/config/" + key;
        zk_client_->SetWatcher(path, [this, key](int type, int state, const std::string& path) {
            OnConfigChanged(key);
        });
    }
}

void DistributedCoord::OnConfigChanged(const std::string& key) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    auto it = config_watchers_.find(key);
    if (it != config_watchers_.end()) {
        std::string value = GetConfig(key);
        it->second(key, value);
    }
}

void DistributedCoord::StartConfigWatcher() {
    if (!zk_client_) return;

    zk_client_->SetWatcher("/gw-server/config", [this](int type, int state, const std::string& path) {
        if (type == ZOO_CHILD_EVENT) {
            // 配置节点有变化
            std::cout << "[DistCoord] Config changed" << std::endl;
        }
    });
}

// === 选主 ===

void DistributedCoord::ParticipateElection() {
    if (!zk_client_) return;

    election_path_ = "/gw-server/election/master";

    // 创建临时顺序节点
    std::string node_name = "candidate_";
    node_name += local_node_id_;
    std::string data = BuildNodeData();

    // 使用ZOO_EPHEMERAL | ZOO_SEQUENCE创建顺序临时节点
    char path_buffer[256];
    int ret = zoo_create(zk_client_->m_zhandle, (election_path_ + "/" + node_name).c_str(),
                        data.c_str(), data.length(), &ZOO_OPEN_ACL_UNSAFE,
                        ZOO_EPHEMERAL | ZOO_SEQUENCE, path_buffer, sizeof(path_buffer));

    if (ret == ZOK || ret == ZNODEEXISTS) {
        lock_node_path_ = path_buffer;
        std::cout << "[DistCoord] Election node: " << lock_node_path_ << std::endl;
    }

    // 检查是否为最小节点
    StartElectionWatcher();
}

void DistributedCoord::OnElectionChanged() {
    if (!zk_client_) return;

    std::lock_guard<std::mutex> lock(election_mutex_);

    // 获取所有选举节点
    auto children = zk_client_->GetChildren(election_path_);
    if (children.empty()) {
        return;
    }

    // 排序找最小的
    std::sort(children.begin(), children.end());

    // 第一个是最小节点，即主节点
    std::string new_master = children[0];
    std::string master_path = election_path_ + "/" + new_master;
    std::string master_data = zk_client_->GetData(master_path);

    bool was_master = is_master_;
    bool is_new_master = (master_path == lock_node_path_);

    master_node_id_ = new_master;
    is_master_ = is_new_master;

    if (was_master != is_master_ && master_change_callback_) {
        master_change_callback_(master_node_id_, is_master_);
    }

    std::cout << "[DistCoord] Master is: " << master_node_id_
              << ", I am master: " << (is_master_ ? "YES" : "NO") << std::endl;
}

void DistributedCoord::StartElectionWatcher() {
    if (!zk_client_) return;

    // 监听选举路径变化
    zk_client_->SetWatcher(election_path_, [this](int type, int state, const std::string& path) {
        if (type == ZOO_CHILD_EVENT) {
            std::cout << "[DistCoord] Election changed" << std::endl;
            OnElectionChanged();
        }
    });

    // 初始检查
    OnElectionChanged();
}

void DistributedCoord::WatchMaster(MasterChangeCallback callback) {
    std::lock_guard<std::mutex> lock(election_mutex_);
    master_change_callback_ = callback;
}

// === 分布式锁 ===

bool DistributedCoord::AcquireLock(const std::string& lock_name, int timeout_ms) {
    if (!zk_client_) return false;

    std::string lock_path = "/gw-server/locks/" + lock_name;

    // 创建顺序临时节点
    char path_buffer[256];
    std::string node_name = "lock_";
    int ret = zoo_create(zk_client_->m_zhandle, (lock_path + "/" + node_name).c_str(),
                        "", 0, &ZOO_OPEN_ACL_UNSAFE,
                        ZOO_EPHEMERAL | ZOO_SEQUENCE, path_buffer, sizeof(path_buffer));

    if (ret != ZOK) {
        return false;
    }

    std::string my_path = path_buffer;

    // 等待获取锁
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        auto children = zk_client_->GetChildren(lock_path);
        if (children.empty()) {
            return false;
        }

        // 排序找最小
        std::sort(children.begin(), children.end());

        // 提取节点名
        std::string my_node = my_path.substr(my_path.rfind('/') + 1);

        // 如果我是最小的，获取锁成功
        if (!children.empty() && children[0] == my_node) {
            std::lock_guard<std::mutex> lock(locks_mutex_);
            held_locks_[lock_name] = my_path;
            std::cout << "[DistCoord] Acquired lock: " << lock_name << std::endl;
            return true;
        }

        // 监听前一个节点
        int my_index = -1;
        for (int i = 0; i < (int)children.size(); ++i) {
            if (children[i] == my_node) {
                my_index = i;
                break;
            }
        }

        if (my_index > 0) {
            std::string prev_node = children[my_index - 1];
            std::string prev_path = lock_path + "/" + prev_node;

            // 等待前一个节点删除 (简化处理：直接等待超时)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            // 超时，删除创建的节点
            zk_client_->Delete(my_path);
            return false;
        }
    }
}

void DistributedCoord::ReleaseLock(const std::string& lock_name) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    auto it = held_locks_.find(lock_name);
    if (it != held_locks_.end()) {
        zk_client_->Delete(it->second);
        held_locks_.erase(it);
        std::cout << "[DistCoord] Released lock: " << lock_name << std::endl;
    }
}

bool DistributedCoord::TryDeviceLock(const std::string& device_id) {
    // 先检查设备当前在哪个节点
    if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
        std::string key = "device:online:" + device_id;
        std::string val = redis->Get(key);

        if (!val.empty()) {
            // 设备在其他节点，尝试获取分布式锁
            std::string lock_name = "device_" + device_id;
            return AcquireLock(lock_name, 3000); // 3秒超时
        }
    }

    // 设备在本地或Redis无记录，直接返回true
    return true;
}

void DistributedCoord::ReleaseDeviceLock(const std::string& device_id) {
    std::string lock_name = "device_" + device_id;
    ReleaseLock(lock_name);
}

} // namespace gw
