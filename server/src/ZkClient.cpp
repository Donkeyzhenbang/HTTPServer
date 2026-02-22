#include "../inc/ZkClient.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>

void ZkClient::handle_watcher_event(int type, int state, const std::string& path) {
    std::lock_guard<std::mutex> lock(watchers_mutex_);
    auto it = watchers_.find(path);
    if (it != watchers_.end()) {
        it->second(type, state, path);
    }

    // 处理子节点变化
    if (type == ZOO_CHILD_EVENT) {
        // 查找以该路径为前缀的watcher
        for (auto& w : watchers_) {
            if (path.find(w.first) == 0 || w.first.find(path) == 0) {
                w.second(type, state, path);
            }
        }
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false) {
}

ZkClient::~ZkClient() {
    if (m_zhandle != nullptr) {
        zookeeper_close(m_zhandle);
    }
}

void ZkClient::watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx)
{
    // Global watcher for session events
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            std::cout << "[ZK] Connected to Zookeeper Server." << std::endl;
            ZkClient* client = (ZkClient*)zoo_get_context(zh);
            if (client) {
                std::unique_lock<std::mutex> lock(client->m_mutex);
                client->m_connected = true;
                client->m_cond.notify_all();
            }
        }
    } else {
        // 其他事件类型，转发给对应watcher
        if (watcherCtx != nullptr && path != nullptr) {
            ZkClient* client = (ZkClient*)watcherCtx;
            client->handle_watcher_event(type, state, std::string(path));
        }
    }
}

void ZkClient::Start(const std::string& host)
{
    m_host = host;
    // Connect async, wait for watcher to set m_connected
    std::cout << "[ZK] Connecting to " << host << "..." << std::endl;
    m_zhandle = zookeeper_init(m_host.c_str(), watcher, 30000, nullptr, this, 0);

    if (m_zhandle == nullptr) {
        std::cerr << "[ZK] zookeeper_init failed!" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_connected) {
        // Wait up to 10 seconds for connection
        if (m_cond.wait_for(lock, std::chrono::seconds(10)) == std::cv_status::timeout) {
             std::cerr << "[ZK] Connection timeout!" << std::endl;
             exit(EXIT_FAILURE);
        }
    }

    // 创建基础路径
    CreatePathRecursive("/gw-server");
    CreatePathRecursive("/gw-server/nodes");
    CreatePathRecursive("/gw-server/locks");
    CreatePathRecursive("/gw-server/config");
    CreatePathRecursive("/gw-server/election");
}

bool ZkClient::CreatePathRecursive(const std::string& path) {
    if (path.empty() || path == "/") return true;

    // 检查路径是否存在
    if (Exists(path)) return true;

    // 递归创建父路径
    std::string parent = path.substr(0, path.rfind('/'));
    if (!parent.empty() && parent != path) {
        CreatePathRecursive(parent);
    }

    // 创建当前路径
    Create(path, "", 0);
    return Exists(path);
}

void ZkClient::Create(const std::string& path, const std::string& data, int state)
{
    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);

    int flag = (state == 0) ? 0 : ZOO_EPHEMERAL;

    int ret = zoo_create(m_zhandle, path.c_str(), data.c_str(), data.length(),
                         &ZOO_OPEN_ACL_UNSAFE, flag, path_buffer, bufferlen);

    if (ret == ZOK) {
        std::cout << "[ZK] Created node: " << path << std::endl;
    } else if (ret == ZNODEEXISTS) {
        // 节点已存在，尝试更新数据
        ret = zoo_set(m_zhandle, path.c_str(), data.c_str(), data.length(), -1);
        if (ret == ZOK) {
            std::cout << "[ZK] Updated node: " << path << std::endl;
        }
    } else {
        std::cerr << "[ZK] Create failed for " << path << ", code: " << ret << std::endl;
    }
}

bool ZkClient::CreateSync(const std::string& path, const std::string& data, int state, int timeout_ms) {
    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);

    int flag = (state == 0) ? 0 : ZOO_EPHEMERAL;

    // 先确保父路径存在
    std::string parent = path.substr(0, path.rfind('/'));
    if (!parent.empty()) {
        CreatePathRecursive(parent);
    }

    int ret = zoo_create(m_zhandle, path.c_str(), data.c_str(), data.length(),
                         &ZOO_OPEN_ACL_UNSAFE, flag, path_buffer, bufferlen);

    if (ret == ZOK) {
        return true;
    } else if (ret == ZNODEEXISTS) {
        return true; // 已存在也认为成功
    }
    return false;
}

std::string ZkClient::GetData(const std::string& path)
{
    char buffer[512];
    int bufferlen = sizeof(buffer);
    int ret = zoo_get(m_zhandle, path.c_str(), 0, buffer, &bufferlen, nullptr);
    if (ret != ZOK) {
        return "";
    }
    return std::string(buffer, bufferlen);
}

std::vector<std::string> ZkClient::GetChildren(const std::string& path) {
    std::vector<std::string> children;

    struct String_vector strs;
    int ret = zoo_get_children(m_zhandle, path.c_str(), 0, &strs);
    if (ret != ZOK) {
        return children;
    }

    for (int i = 0; i < strs.count; ++i) {
        children.push_back(strs.data[i]);
    }

    // 释放内存
    if (strs.data) {
        free(strs.data);
    }

    return children;
}

void ZkClient::SetWatcher(const std::string& path, WatcherCallback callback) {
    std::lock_guard<std::mutex> lock(watchers_mutex_);
    watchers_[path] = callback;

    // 设置ZK watcher
    zoo_wget_children(m_zhandle, path.c_str(), watcher, this, nullptr);
}

bool ZkClient::Delete(const std::string& path) {
    int ret = zoo_delete(m_zhandle, path.c_str(), -1);
    return ret == ZOK;
}

bool ZkClient::Exists(const std::string& path) {
    struct Stat stat;
    int ret = zoo_exists(m_zhandle, path.c_str(), 0, &stat);
    return ret == ZOK;
}

void ZkClient::Close() {
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
}
