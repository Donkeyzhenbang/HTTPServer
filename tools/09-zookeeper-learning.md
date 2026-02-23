# ZooKeeper 分布式协调功能学习指南

## 一、整体架构概览

### 1.1 代码文件结构

```
server/
├── inc/
│   ├── ZkClient.h              # ZK客户端封装
│   └── DistributedCoord.h     # 分布式协调器 (核心)
├── src/
│   ├── ZkClient.cpp           # ZK客户端实现
│   ├── DistributedCoord.cpp   # 分布式协调器实现
│   └── ServerApp.cpp          # 初始化入口
```

### 1.2 ZK命名空间结构

```
/gw-server/
├── /nodes/           # 服务注册与发现
│   └── node_172.19.186.11_52487  # 临时节点
├── /locks/          # 分布式锁
│   └── device_123456789/          # 设备锁
│       ├── lock_0000000001
│       └── lock_0000000002
├── /config/         # 配置管理
│   └── global                    # 全局配置
└── /election/      # 选主
    └── master/
        └── candidate_node_xxx     # 候选节点
```

---

## 二、核心组件详解

### 2.1 ZkClient (ZkClient.cpp)

**职责**: 对ZooKeeper C API的封装

**主要方法**:

```cpp
class ZkClient {
    // 基础操作
    void Start(const std::string& host);           // 连接ZK
    void Create(path, data, state);                // 创建节点
    std::string GetData(path);                      // 获取节点数据
    std::vector<std::string> GetChildren(path);    // 获取子节点列表
    bool Delete(path);                              // 删除节点
    bool Exists(path);                              // 检查节点是否存在
    void SetWatcher(path, callback);                // 设置监听器

    // 自动创建基础路径
    void CreatePathRecursive(path);                 // 递归创建路径
};
```

**关键实现细节**:
- 启动时自动创建 `/gw-server`, `/nodes`, `/locks`, `/config`, `/election` 基础路径
- 使用 `ZOO_EPHEMERAL` 创建临时节点（节点断开后自动删除）
- 使用 `ZOO_SEQUENCE` 创建顺序节点（用于分布式锁和选主）

---

### 2.2 DistributedCoord (DistributedCoord.cpp)

**职责**: 统一的分布式协调入口，单例模式

```cpp
class DistributedCoord {
    static DistributedCoord& Instance();  // 获取单例

    // 初始化 (在ServerApp中调用)
    void Init(ZkClient* zk, ip, tcp_port, http_port);
};
```

---

## 三、功能实现详解

### 3.1 服务注册与发现

#### 3.1.1 服务注册

**代码位置**: `DistributedCoord::RegisterNode()` (第52-60行)

```cpp
void DistributedCoord::RegisterNode() {
    std::string path = "/gw-server/nodes/" + local_node_id_;
    std::string data = BuildNodeData();  // JSON: {"ip":..., "tcp_port":..., "http_port":...}

    // 创建临时节点 (Ephemeral)
    zk_client_->Create(path, data, 1);  // 1 = Ephemeral
}
```

**原理**:
- 每个节点启动时在ZK注册一个临时节点
- 临时节点特性：节点断开连接/崩溃后自动消失
- 其他节点可通过监听感知节点变化

#### 3.1.2 服务发现

**代码位置**: `DistributedCoord::GetAllNodes()` (第122-137行)

```cpp
std::vector<NodeInfo> DistributedCoord::GetAllNodes() {
    // 1. 获取 /gw-server/nodes 下所有子节点
    auto children = zk_client_->GetChildren("/gw-server/nodes");

    // 2. 遍历每个节点，获取详细信息
    for (const auto& child : children) {
        std::string path = "/gw-server/nodes/" + child;
        std::string data = zk_client_->GetData(path);
        result.push_back(ParseNodeInfo(path, data));
    }
    return result;
}
```

#### 3.1.3 节点变化监听

**代码位置**: `DistributedCoord::StartNodeWatcher()` (第107-120行)

```cpp
void DistributedCoord::StartNodeWatcher() {
    // 设置Watcher监听节点列表变化
    zk_client_->SetWatcher("/gw-server/nodes", [](int type, int state, const std::string& path) {
        if (type == ZOO_CHILD_EVENT) {
            // 节点变化，重新获取列表
            OnNodesChanged();
        }
    });
}
```

---

### 3.2 分布式锁

#### 3.2.1 锁原理

使用 **临时顺序节点** + **Watch监听** 实现：

```
步骤1: 创建顺序临时节点
    /gw-server/locks/device_123/lock_0000000001

步骤2: 判断是否为最小节点
    - 是最小节点 → 获取锁成功
    - 不是 → 监听前一个节点

步骤3: 前一个节点删除时，ZK通知
    → 重新检查是否为最小

步骤4: 使用完毕，删除节点释放锁
```

#### 3.2.2 获取锁

**代码位置**: `DistributedCoord::AcquireLock()` (第276-341行)

```cpp
bool DistributedCoord::AcquireLock(const std::string& lock_name, int timeout_ms) {
    std::string lock_path = "/gw-server/locks/" + lock_name;

    // 1. 创建顺序临时节点
    char path_buffer[256];
    zoo_create(zk_client_->m_zhandle, (lock_path + "/lock_").c_str(),
               "", 0, &ZOO_OPEN_ACL_UNSAFE,
               ZOO_EPHEMERAL | ZOO_SEQUENCE,  // 临时+顺序
               path_buffer, sizeof(path_buffer));

    std::string my_path = path_buffer;

    // 2. 循环检查直到获取锁或超时
    while (true) {
        auto children = zk_client_->GetChildren(lock_path);
        std::sort(children);  // 排序找最小

        // 如果我是最小的，获取锁成功
        if (children[0] == extract_node_name(my_path)) {
            held_locks_[lock_name] = my_path;
            return true;
        }

        // 不是最小的，等待前一个节点删除
        // (简化实现：sleep等待)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 检查超时
        if (超时) {
            zk_client_->Delete(my_path);
            return false;
        }
    }
}
```

#### 3.2.3 释放锁

**代码位置**: `DistributedCoord::ReleaseLock()` (第343-351行)

```cpp
void DistributedCoord::ReleaseLock(const std::string& lock_name) {
    auto it = held_locks_.find(lock_name);
    if (it != held_locks_.end()) {
        zk_client_->Delete(it->second);  // 删除节点
        held_locks_.erase(it);
    }
}
```

---

### 3.3 选主机制 (Master Election)

#### 3.3.1 原理

使用 **临时顺序节点** + **最小节点为主** 原则：

```
节点A创建: /gw-server/election/master/candidate_node_A_0000000001  (序号1)
节点B创建: /gw-server/election/master/candidate_node_B_0000000002  (序号2)

序号1最小 → 节点A为主节点
```

#### 3.3.2 参与选举

**代码位置**: `DistributedCoord::ParticipateElection()` (第196-219行)

```cpp
void DistributedCoord::ParticipateElection() {
    election_path_ = "/gw-server/election/master";

    // 创建临时顺序节点
    std::string node_name = "candidate_" + local_node_id_;
    zoo_create(..., (election_path_ + "/" + node_name).c_str(),
               ..., ZOO_EPHEMERAL | ZOO_SEQUENCE, ...);

    // 启动选举监听
    StartElectionWatcher();
}
```

#### 3.3.3 主节点判定

**代码位置**: `DistributedCoord::OnElectionChanged()` (第221-252行)

```cpp
void DistributedCoord::OnElectionChanged() {
    // 获取所有候选节点
    auto children = zk_client_->GetChildren(election_path_);
    std::sort(children);  // 排序

    // 序号最小的为主节点
    std::string new_master = children[0];
    bool is_new_master = (new_master == lock_node_path_);

    // 触发回调
    if (master_change_callback_) {
        master_change_callback_(new_master, is_new_master);
    }
}
```

---

### 3.4 配置管理

#### 3.4.使用 **持久节点1 原理

** + **Watcher监听** 实现配置同步：

```
节点A修改配置 → 更新 /gw-server/config/global 节点
                                    ↓
                            ZK通知所有监听该节点的节点
                                    ↓
                    节点B收到通知 → 重新读取配置 → 更新本地配置
```

#### 3.4.2 API

**代码位置**: `DistributedCoord::GetConfig()`, `SetConfig()` (第148-160行)

```cpp
// 获取配置
std::string GetConfig(const std::string& key) {
    return zk_client_->GetData("/gw-server/config/" + key);
}

// 设置配置
bool SetConfig(const std::string& key, const std::string& value) {
    return zk_client_->CreateSync("/gw-server/config/" + key, value, 0);
}
```

---

## 四、如何接入系统

### 4.1 初始化 (已在ServerApp中完成)

```cpp
// ServerApp.cpp
void ServerApp::registerToZk() {
    if (m_zkHost.empty()) return;

    m_zkClient->Start(m_zkHost);

    // 初始化分布式协调器
    gw::DistributedCoord::Instance().Init(m_zkClient.get(), m_localIp, port, httpPort);

    // 参与选主
    gw::DistributedCoord::Instance().ParticipateElection();
}
```

### 4.2 业务代码中使用

```cpp
#include "../inc/DistributedCoord.h"

// === 1. 服务发现 ===
auto nodes = gw::DistributedCoord::Instance().GetAllNodes();
for (const auto& node : nodes) {
    std::cout << "在线节点: " << node.ip << ":" << node.http_port << std::endl;
}

// 监听节点变化
gw::DistributedCoord::Instance().WatchNodes([](const std::vector<NodeInfo>& nodes) {
    std::cout << "节点列表变化，当前 " << nodes.size() << " 个节点" << std::endl;
});

// === 2. 选主 ===
if (gw::DistributedCoord::Instance().IsMaster()) {
    // 主节点执行定时任务
    do_master_task();
}

// 监听主节点变化
gw::DistributedCoord::Instance().WatchMaster([](const std::string& master_id, bool is_master) {
    if (is_master) {
        std::cout << "我成为主节点了!" << std::endl;
    } else {
        std::cout << "新主节点是: " << master_id << std::endl;
    }
});

// === 3. 分布式锁 ===
// 获取锁
if (gw::DistributedCoord::Instance().AcquireLock("my_task")) {
    // 执行需要互斥的任务
    do_important_task();

    // 释放锁
    gw::DistributedCoord::Instance().ReleaseLock("my_task");
}

// === 4. 设备锁 (防止多节点操作同一设备) ===
if (gw::DistributedCoord::Instance().TryDeviceLock("device_123")) {
    send_command_to_device("device_123");
    gw::DistributedCoord::Instance().ReleaseDeviceLock("device_123");
}

// === 5. 配置管理 ===
// 获取配置
std::string config = gw::DistributedCoord::Instance().GetConfig("global");

// 设置配置 (需要分布式锁保护)
gw::DistributedCoord::Instance().AcquireLock("config_lock");
gw::DistributedCoord::Instance().SetConfig("global", "{\"level\":\"INFO\"}");
gw::DistributedCoord::Instance().ReleaseLock("config_lock");

// 监听配置变化
gw::DistributedCoord::Instance().WatchConfig("global", [](const std::string& key, const std::string& value) {
    std::cout << "配置变更: " << key << " = " << value << std::endl;
    // 重新加载配置
    reload_config(value);
});
```

---

## 五、应用场景

### 5.1 设备操作互斥

**场景**: 两个节点同时对同一设备下发指令

**解决方案**: 使用设备锁

```cpp
void send_command(const std::string& device_id, const std::string& cmd) {
    // 尝试获取设备锁
    if (gw::DistributedCoord::Instance().TryDeviceLock(device_id)) {
        // 发送指令
        send_to_device(device_id, cmd);

        // 释放锁
        gw::DistributedCoord::Instance().ReleaseDeviceLock(device_id);
    } else {
        // 设备正在被其他节点操作
        return ERROR_DEVICE_BUSY;
    }
}
```

### 5.2 主节点定时任务

**场景**: 需要定时执行的任务（如清理过期数据），但只在一台机器执行

**解决方案**: 选主 + 主节点执行

```cpp
// 监听主节点变化
gw::DistributedCoord::Instance().WatchMaster([](const std::string& master_id, bool is_master) {
    if (is_master) {
        start_cleanup_timer();  // 启动定时清理任务
    } else {
        stop_cleanup_timer();   // 停止
    }
});
```

### 5.3 配置热更新

**场景**: 修改日志级别、阈值等配置，需要所有节点立即生效

**解决方案**: ZK配置管理

```cpp
// 管理员修改配置
curl -X POST http://nodeA:8080/api/admin/set_config \
  -d '{"key":"global","value":"{\"log_level\":\"DEBUG\"}"}'

// 所有节点收到通知，自动重载配置
```

### 5.4 节点故障转移

**场景**: 节点A崩溃，需要将上面的设备连接迁移到节点B

**解决方案**: 服务发现 + 心跳

```cpp
// 监听节点变化
gw::DistributedCoord::Instance().WatchNodes([](const std::vector<NodeInfo>& nodes) {
    // 检查是否有节点离线
    for (const auto& node : nodes) {
        if (node.status == "offline") {
            // 迁移该节点上的设备
            migrate_devices_from(node.ip);
        }
    }
});
```

---

## 六、注意事项

### 6.1 连接管理

- ZK连接在ServerApp启动时建立，进程生命周期内保持
- 临时节点依赖ZK连接，连接断开会导致临时节点消失

### 6.2 锁超时

- 分布式锁有超时机制，防止持有者崩溃后锁无法释放
- 建议业务操作也设置超时

### 6.3 Watcher一次性

- ZK的Watcher是一次性的，触发后需要重新注册
- 代码中已在变化后自动重新设置

### 6.4 性能考虑

- 避免频繁的ZK操作，大部分场景可配合本地缓存
- 锁竞争激烈时可考虑Redis分布式锁

---

## 七、测试验证

```bash
# 1. 启动Zookeeper
zkServer.sh start

# 2. 查看ZK节点
zkCli.sh -server localhost:2181
ls /gw-server
ls /gw-server/nodes
ls /gw-server/locks
ls /gw-server/election

# 3. 启动多个服务节点
./httpserver -p 52487 -w 8080 -z 127.0.0.1:2181 -r 127.0.0.1:6379 -i 127.0.0.1

# 4. 观察日志
# - 节点注册成功
# - 选举出主节点
# - 节点变化通知
```

---

*文档版本: 1.0*
*创建日期: 2026-02-23*
