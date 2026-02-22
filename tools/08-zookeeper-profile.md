# ZooKeeper 分布式功能扩展设计文档

## 一、现状分析

### 1.1 当前ZooKeeper使用情况

claude --resume d613d2e5-fb45-4a55-a159-8a00ff89c3fd

当前ZooKeeper仅用于**服务注册**：

```
/gw-server/nodes/node_172.19.186.11_52487 -> {"ip":"...","port":52487,"http_port":8080}
```

| 功能 | 状态 | 说明 |
|-----|------|------|
| 服务注册 | ✅ 已实现 | 临时节点注册 |
| 服务发现 | ❌ 未实现 | 其他节点无法获取节点列表 |
| 分布式锁 | ❌ 未实现 | 无法协调多节点任务 |
| 配置管理 | ❌ 未实现 | 无法统一配置 |
| 选主机制 | ❌ 未实现 | 无主备切换 |
| 变更监听 | ❌ 未实现 | 节点上下线无感知 |

---

## 二、设计目标

扩展ZooKeeper功能，实现完整的分布式协调能力：

```
┌─────────────────────────────────────────────────────────────────┐
│                    ZooKeeper 扩展功能架构                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   ZK 命名空间                            │   │
│  │  /gw-server                                             │   │
│  │    ├── /nodes (服务注册与发现)                          │   │
│  │    │   ├── /node_172.19.186.11_52487                   │   │
│  │    │   └── /node_172.19.186.12_52487                   │   │
│  │    ├── /locks (分布式锁)                                │   │
│  │    │   └── /device_lock_123456789 (设备级锁)           │   │
│  │    ├── /config (配置管理)                               │   │
│  │    │   ├── /global (全局配置)                          │   │
│  │    │   └── /channel_1 (通道配置)                       │   │
│  │    ├── /election (选主)                                │   │
│  │    │   └── /master (主节点)                            │   │
│  │    └── /tasks (任务队列)                               │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 三、详细设计

### 3.1 服务注册与发现 (扩展)

#### 3.1.1 节点注册

```json
Path: /gw-server/nodes/node_{IP}_{TCP_PORT}
Data: {
  "ip": "172.19.186.11",
  "tcp_port": 52487,
  "http_port": 8080,
  "status": "online",
  "timestamp": 1700000000,
  "version": "1.0.0"
}
Type: Ephemeral (临时节点)
```

#### 3.1.2 服务发现 - 获取所有节点

```cpp
// 获取所有在线节点
std::vector<NodeInfo> get_all_nodes() {
    // zoo_get_children获取子节点列表
    // 遍历每个节点获取详细信息
}
```

#### 3.1.3 节点变更监听

```cpp
// Watcher回调
void OnNodeChange(int type, const std::string& path) {
    if (type == ZOO_CHILD_EVENT) {
        // 节点列表变化，重新获取
        auto nodes = get_all_nodes();
        NotifyNodesChanged(nodes);
    }
}
```

---

### 3.2 分布式锁

#### 3.2.1 锁类型

| 锁名称 | 路径 | 用途 |
|-------|------|------|
| 设备锁 | `/gw-server/locks/device_{device_id}` | 防止多节点同时操作同一设备 |
| 配置锁 | `/gw-server/locks/config` | 配置修改互斥 |
| 任务锁 | `/gw-server/locks/task_{task_id}` | 任务执行互斥 |

#### 3.2.2 锁获取流程

```
┌─────────────────────────────────────────────────────────────────┐
│                    分布式锁获取流程                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. 创建临时顺序节点                                            │
│     /gw-server/locks/device_123456789/lock_0000000001          │
│                                                                 │
│  2. 获取当前最小序号                                            │
│     - 如果是序号最小的节点 → 获取锁成功                          │
│     - 否则 → 监听前一个节点删除事件                             │
│                                                                 │
│  3. 等待锁释放                                                  │
│     当前一个节点删除时，ZK通知，重新检查序号                     │
│                                                                 │
│  4. 锁使用完毕 → 删除节点释放锁                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 3.2.3 设备锁使用场景

```cpp
// 节点B要发送指令到设备A
bool try_lock_device(const std::string& device_id, const std::string& node_id) {
    // 1. 检查设备当前所在节点
    std::string current_node = get_device_location(device_id);

    // 2. 如果设备就在当前节点，直接操作
    if (current_node == my_node_id) {
        return true;
    }

    // 3. 尝试获取分布式锁
    return acquire_lock("device_" + device_id);
}

// 要图操作前获取锁
void capture_image(const std::string& device_id) {
    if (try_lock_device(device_id, my_node_id)) {
        // 执行要图操作
        send_capture_command(device_id);
        // 释放锁
        release_lock("device_" + device_id);
    } else {
        // 设备正在被其他节点操作
        std::cout << "设备忙，稍后重试" << std::endl;
    }
}
```

---

### 3.3 配置管理

#### 3.3.1 配置结构

```json
{
  "global": {
    "heartbeat_interval": 30,
    "redis_ttl": 180,
    "max_connections": 10000,
    "log_level": "INFO"
  },
  "channels": [
    {"id": 1, "name": "通道1", "enabled": true, "resolution": "1920x1080"},
    {"id": 2, "name": "通道2", "enabled": true, "resolution": "1920x1080"}
  ],
  "snapshot": {
    "quality": 85,
    "format": "jpg",
    "interval": 5
  }
}
```

#### 3.3.2 配置更新流程

```
┌─────────────────────────────────────────────────────────────────┐
│                    配置更新流程                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. 节点A 修改配置                                              │
│     - 获取配置锁                                                 │
│     - 更新 /gw-server/config/global 节点数据                    │
│     - 释放锁                                                     │
│                                                                 │
│  2. ZK 通知所有Watch该节点的节点                                │
│                                                                 │
│  3. 其他节点收到通知                                             │
│     - 重新读取配置                                               │
│     - 更新本地配置                                               │
│     - 重载相关模块                                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 3.3.3 配置API

```cpp
// 获取配置
Config get_config(const std::string& key);

// 更新配置 (需要锁)
bool update_config(const std::string& key, const Config& config);

// 订阅配置变更
void watch_config(const std::string& key, ConfigChangeCallback callback);
```

---

### 3.4 分布式选主 (Master Election)

#### 3.4.1 选主场景

| 场景 | 说明 |
|-----|------|
| 主节点故障 | 自动选举新主节点 |
| 负载均衡 | 某些任务需要主节点执行 |
| 状态同步 | 主节点负责同步状态 |

#### 3.4.2 选主实现

```
Path: /gw-server/election

节点结构:
/gw-server/election/master_0000000001  (最小序号，为主节点)
/gw-server/election/master_0000000002  (备用节点)
/gw-server/election/master_0000000003  (备用节点)
```

```cpp
class MasterElection {
public:
    // 参加选举
    void participate(const std::string& node_id);

    // 判断是否为主节点
    bool is_master() const { return is_master_; }

    // 获取主节点信息
    std::string get_master_node_id() const { return master_node_id_; }

    // 主节点变更回调
    std::function<void(const std::string& new_master)> on_master_change;

private:
    void check_master_status();
    void watch_previous_node();
};
```

#### 3.4.3 主节点职责

```cpp
// 主节点特殊处理
if (is_master()) {
    // 执行定时任务
    schedule_sync_task();
    // 清理过期Redis记录
    schedule_cleanup_task();
} else {
    // 备用节点不执行这些任务
}
```

---

## 四、代码实现计划

### 4.1 新增文件

| 文件 | 职责 |
|-----|------|
| `inc/DistributedCoord.h` | 分布式协调器声明 |
| `src/DistributedCoord.cpp` | 分布式协调器实现 |
| `inc/ZkClient.h` (扩展) | 扩展ZK客户端 |

### 4.2 ZkClient扩展API

```cpp
class ZkClient {
public:
    // ... 现有API ...

    // 获取子节点列表
    std::vector<std::string> GetChildren(const std::string& path);

    // 设置Watcher
    void SetWatcher(const std::string& path, WatcherCallback callback);

    // 删除节点
    bool Delete(const std::string& path);

    // 检查节点是否存在
    bool Exists(const std::string& path);

    // 异步创建
    void CreateAsync(const std::string& path, const std::string& data,
                     std::function<void(int rc, const char* path)> callback);
};
```

### 4.3 DistributedCoord类

```cpp
class DistributedCoord {
public:
    static DistributedCoord& Instance();

    // === 服务发现 ===
    std::vector<NodeInfo> GetAllNodes();
    void WatchNodes(NodeChangeCallback callback);

    // === 分布式锁 ===
    bool AcquireLock(const std::string& lock_name, int timeout_ms = 5000);
    void ReleaseLock(const std::string& lock_name);

    // === 配置管理 ===
    std::string GetConfig(const std::string& key);
    bool SetConfig(const std::string& key, const std::string& value);
    void WatchConfig(const std::string& key, ConfigChangeCallback callback);

    // === 选主 ===
    void ParticipateElection();
    bool IsMaster() const;
    std::string GetMasterNodeId() const;

private:
    // 分布式锁实现
    std::string create_lock_node(const std::string& lock_name);
    bool wait_for_lock(const std::string& lock_path, int timeout_ms);
    void delete_lock_node(const std::string& lock_path);
};
```

---

## 五、测试验证

### 5.1 功能测试清单

| 功能 | 测试场景 | 预期结果 |
|-----|---------|---------|
| 服务发现 | 启动两个节点 | 两个节点都能看到对方 |
| 设备锁 | 两节点同时操作同一设备 | 只有一个节点操作成功 |
| 配置同步 | 修改一个节点配置 | 其他节点配置同步更新 |
| 选主 | 停止主节点 | 备用节点自动成为主节点 |
| 节点监听 | 停止一个节点 | 另一个节点收到通知 |

### 5.2 压力测试

```bash
# 模拟高并发锁获取
wrk -t10 -c100 -d30s http://localhost:8080/api/test/lock

# 模拟配置变更
for i in {1..100}; do curl -X POST localhost:8080/api/config -d '{"key":"test","value":'$i'}'; done
```

---

## 六、性能考虑

1. **Watcher批量处理**：避免频繁触发，使用合并通知
2. **锁超时机制**：防止锁持有者崩溃，设置自动过期
3. **连接复用**：ZK连接长期保持，减少创建开销
4. **本地缓存**：热点配置本地缓存，减少ZK访问

---

## 七、总结

通过扩展ZooKeeper功能，构建完整的分布式协调系统：

| 功能 | 收益 |
|-----|------|
| 服务发现 | 动态感知节点变化 |
| 分布式锁 | 多节点协调一致 |
| 配置管理 | 统一配置，实时同步 |
| 选主 | 高可用，自动故障转移 |

*文档版本: 1.0*
*创建日期: 2026-02-22*
