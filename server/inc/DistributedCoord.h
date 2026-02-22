#pragma once

#include "ZkClient.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace gw {

// 节点信息
struct NodeInfo {
    std::string node_id;      // 节点ID: node_IP_PORT
    std::string ip;
    int tcp_port = 0;
    int http_port = 0;
    std::string status;
    int64_t timestamp = 0;
};

// 回调类型
using NodeChangeCallback = std::function<void(const std::vector<NodeInfo>&)>;
using ConfigChangeCallback = std::function<void(const std::string& key, const std::string& value)>;
using MasterChangeCallback = std::function<void(const std::string& master_node_id, bool is_master)>;

// 分布式协调器
class DistributedCoord {
public:
    static DistributedCoord& Instance();

    // 初始化
    void Init(ZkClient* zk_client, const std::string& local_ip, int tcp_port, int http_port);

    // === 服务发现 ===
    // 获取所有在线节点
    std::vector<NodeInfo> GetAllNodes();

    // 监听节点变化
    void WatchNodes(NodeChangeCallback callback);

    // 获取当前节点ID
    std::string GetLocalNodeId() const { return local_node_id_; }

    // === 配置管理 ===
    // 获取配置
    std::string GetConfig(const std::string& key);

    // 设置配置
    bool SetConfig(const std::string& key, const std::string& value);

    // 监听配置变化
    void WatchConfig(const std::string& key, ConfigChangeCallback callback);

    // === 选主 ===
    // 参与选举
    void ParticipateElection();

    // 是否为主节点
    bool IsMaster() const { return is_master_; }

    // 获取主节点ID
    std::string GetMasterNodeId() const { return master_node_id_; }

    // 监听主节点变化
    void WatchMaster(MasterChangeCallback callback);

    // === 分布式锁 ===
    // 获取锁
    bool AcquireLock(const std::string& lock_name, int timeout_ms = 5000);

    // 释放锁
    void ReleaseLock(const std::string& lock_name);

    // 获取设备锁 (检查设备是否在当前节点)
    bool TryDeviceLock(const std::string& device_id);

    // 释放设备锁
    void ReleaseDeviceLock(const std::string& device_id);

private:
    DistributedCoord();
    ~DistributedCoord();
    DistributedCoord(const DistributedCoord&) = delete;
    DistributedCoord& operator=(const DistributedCoord&) = delete;

    // 内部方法
    void RegisterNode();
    void StartNodeWatcher();
    void StartConfigWatcher();
    void StartElectionWatcher();

    void OnNodesChanged();
    void OnConfigChanged(const std::string& key);
    void OnElectionChanged();

    std::string BuildNodeData();
    NodeInfo ParseNodeInfo(const std::string& path, const std::string& data);

    ZkClient* zk_client_ = nullptr;
    std::string local_ip_;
    int local_tcp_port_ = 0;
    int local_http_port_ = 0;
    std::string local_node_id_;

    // 节点列表
    std::vector<NodeInfo> nodes_;
    std::mutex nodes_mutex_;
    NodeChangeCallback node_change_callback_;

    // 配置
    std::unordered_map<std::string, ConfigChangeCallback> config_watchers_;
    std::mutex config_mutex_;

    // 选主
    std::string election_path_;
    std::string lock_node_path_;
    bool is_master_ = false;
    std::string master_node_id_;
    std::mutex election_mutex_;
    MasterChangeCallback master_change_callback_;

    // 锁管理
    std::mutex locks_mutex_;
    std::unordered_map<std::string, std::string> held_locks_;  // lock_name -> lock_path
};

} // namespace gw
