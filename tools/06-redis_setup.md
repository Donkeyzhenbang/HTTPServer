# Redis + ZooKeeper 分布式架构说明

## 一、整体架构

本项目采用 **Redis + ZooKeeper** 双中心分布式架构：

```
┌─────────────────────────────────────────────────────────────────┐
│                        网关服务器集群                            │
│  ┌─────────────────────┐          ┌─────────────────────┐       │
│  │    节点 A           │          │    节点 B           │       │
│  │  172.19.186.11     │          │  172.19.186.12     │       │
│  │  TCP:52487          │          │  TCP:52487          │       │
│  │  HTTP:8080          │          │  HTTP:8080          │       │
│  └─────────┬───────────┘          └─────────┬───────────┘       │
│            │                                │                     │
│            └───────────┬────────────────────┘                     │
│                        ▼                                          │
│  ┌─────────────────────────────────────────┐                      │
│  │         ZooKeeper (服务注册/发现)        │                      │
│  │  /gw-server/nodes/node_172.19.186.11_52487  │                  │
│  │  /gw-server/nodes/node_172.19.186.12_52487  │                  │
│  └─────────────────────────────────────────┘                      │
│                        │                                          │
│                        ▼                                          │
│  ┌─────────────────────────────────────────┐                      │
│  │         Redis (设备状态/路由)            │                      │
│  │  device:online:DeviceA -> {ip,port...} │                       device:online:DeviceB -> │
│  │ {ip,port...} │                      │
│  └─────────────────────────────────────────┘                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、ZooKeeper 职责 - 服务注册与发现

### 2.1 核心功能

**ZooKeeper 负责**：节点的注册与发现，构建服务治理基础设施

### 2.2 实现原理

节点启动时调用 `registerToZk()` 方法（`ServerApp.cpp:66-80`）：

```cpp
void ServerApp::registerToZk() {
    m_zkClient->Start(m_zkHost);

    // 创建临时节点路径：/gw-server/nodes/node_IP_PORT
    std::string nodePath = "/gw-server/nodes/node_" + m_localIp + "_" + std::to_string(port);
    std::string nodeData = "{\"ip\":\"" + m_localIp + "\",\"port\":"
        + std::to_string(port) + ",\"http_port\":" + std::to_string(httpPort) + "}";

    // 1 = Ephemeral 表示临时节点
    m_zkClient->Create(nodePath, nodeData, 1);
}
```

### 2.3 数据结构

| ZK节点路径 | 节点数据 | 特性 |
|-----------|---------|------|
| `/gw-server/nodes/node_172.19.186.11_52487` | `{"ip":"172.19.186.11","port":52487,"http_port":8080}` | 临时节点(Ephemeral) |
| `/gw-server/nodes/node_172.19.186.12_52487` | `{"ip":"172.19.186.12","port":52487,"http_port":8080}` | 临时节点(Ephemeral) |

### 2.4 临时节点特性

- **自动清理**：当节点进程崩溃或网络断开时，临时节点会自动消失
- **心跳维持**：节点需要定期与ZK保持连接，否则临时节点会失效
- **服务感知**：其他组件可以通过监听ZK节点变化感知服务上线/下线

### 2.5 适用场景

- 服务注册中心
- 分布式锁（未来扩展）
- 配置管理（未来扩展）
- 选主机制（未来扩展）

---

## 三、Redis 职责 - 设备状态与请求路由

### 3.1 核心功能

**Redis 负责**：设备在线状态存储、跨节点请求路由、全网设备视图

### 3.2 实现原理

#### 3.2.1 设备心跳上报（`recvfile.cpp:40-73`）

```cpp
static int HandleHeartbeat(unsigned char* pBuffer, int Length, int fd) {
    // 获取设备ID
    char device_id[18] = {0};
    memcpy(device_id, frame->cmdId, 17);

    // 更新本地连接上下文
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if(ctx) {
        ctx->setDeviceId(device_id);

        // 写入Redis：Key=device:online:设备ID, TTL=60秒
        std::string key = "device:online:" + std::string(device_id);
        std::string jsonVal = "{\"ip\":\"" + ip +
                              "\",\"port\":" + localAddr.substr(colon + 1) +
                              ",\"http_port\":" + std::to_string(httpPort) + "}";
        redis->Set(key, jsonVal, 60);  // 60秒过期
    }
}
```

#### 3.2.2 设备在线列表查询（`http_server.cpp:375-404`）

```cpp
svr.Get("/api/devices", [](const httplib::Request &req, httplib::Response &res) {
    // 1. 获取本地设备
    auto local_devices = get_all_device_ids();

    // 2. 从Redis获取全网设备
    std::vector<std::string> keys = redis->Keys("device:online:*");
    for (const auto& k : keys) {
        std::string dev_id = k.substr(14);  // 去掉 "device:online:" 前缀
        if (!local_device_set.count(dev_id)) {
            all_devices.push_back(dev_id);远程设备
         // 合并 }
    }

    // 3. 返回全网设备列表
    res.set_content(oss.str(), "application/json");
});
```

#### 3.2.3 跨节点请求路由（`http_server.cpp:645-700`）

```cpp
// 从连接管理器查找设备（本地）
auto* conn_ctx = find_connection_by_device_id(device);

if (!conn_ctx) {
    // 本地未找到，查询Redis获取设备所在节点
    std::string key = "device:online:" + device;
    std::string val = redis->Get(key);

    if (!val.empty()) {
        // 解析目标节点信息，HTTP代理转发
        // 构造HTTP请求转发到目标节点
    }
}
```

### 3.3 数据结构

| Key | Value | TTL |
|-----|-------|-----|
| `device:online:123456789012345` | `{"ip":"172.19.186.11","port":52487,"http_port":8080}` | 60秒 |
| `device:online:123456789012346` | `{"ip":"172.19.186.12","port":52487,"http_port":8080}` | 60秒 |

### 3.4 工作流程图

```
设备A 连接 节点A (172.19.186.11:52487)
         │
         ▼
    发送心跳帧
         │
         ▼
  节点A 更新本地连接表
         │
         ▼
  节点A 写入 Redis: device:online:XXX -> {ip,port,http_port}
         │
         ▼                         节点B 查询 /api/devices
节点B 前端 ◄───────────────────── Redis Keys("device:online:*")
         │                            │
         ▼                            ▼
   显示设备在线                   返回全网设备列表
```

### 3.5 适用场景

- 设备在线状态同步
- 跨节点请求路由
- 前端全网设备列表展示
- 分布式会话状态（无状态设计）

---

## 四、Redis 与 ZooKeeper 对比

| 特性 | Redis | ZooKeeper |
|-----|-------|-----------|
| **数据类型** | KV数据库 | 目录树结构 |
| **数据持久化** | 可选 | 事务日志 |
| **临时节点** | 需要TTL手动维护 | 原生支持 |
| **主要用途** | 设备状态缓存 | 服务注册发现 |
| **数据内容** | 设备IP/端口/HTTP端口 | 服务节点信息 |
| **更新频率** | 高（设备心跳60秒刷新） | 低（节点启动时注册） |
| **客户端查询** | Keys/GET 查询设备 | GET /Children 获取节点列表 |

---

## 五、完整请求链路示例

### 场景：节点B前端发送指令到节点A上的设备

```
1. 设备连接
   设备 ──TCP──► 节点A (172.19.186.11:52487)
                │
                ▼
            写入 Redis: device:online:DeviceA -> {ip:"172.19.186.11", port:52487, http_port:8080}
                │
                ▼
            注册 ZooKeeper: /gw-server/nodes/node_172.19.186.11_52487

2. 前端查询
   节点B前端 ──HTTP GET /api/devices──► 节点B
                                        │
                                        ▼
                              查询 Redis Keys("device:online:*")
                                        │
                                        ▼
                              返回: [DeviceA, DeviceB, ...]

3. 前端发送指令（设备在远程节点）
   节点B前端 ──HTTP POST /api/send_b341 {device:"DeviceA"}──► 节点B
                                                              │
                                                              ▼
                                                    find_connection_by_device_id("DeviceA")
                                                              │本地未找到
                                                              ▼
                                                    查询 Redis: device:online:DeviceA
                                                              │
                                                              ▼
                                                    获取 {ip:"172.19.186.11", http_port:8080}
                                                              │
                                                              ▼
                                                    HTTP 代理转发到 172.19.186.11:8080
                                                              │
                                                              ▼
                                                    节点A 收到指令，转发TCP到设备
```

---

# Redis 远程访问配置步骤

您的第二台服务器 (Node B, `172.19.186.12`) 无法连接第一台服务器 (Node A, `172.19.186.11`) 上的 Redis。
错误信息：`[Redis] Connection error: Connection refused`

这是因为 Redis 默认处于**保护模式**且只监听**本地回环地址 (127.0.0.1)**。您需要修改第一台服务器上的 Redis 配置。

### 步骤 1: 修改 Redis 配置文件 (在第一台服务器上操作)

1.  **找到配置文件**：通常位于 `/etc/redis/redis.conf`。
    ```bash
    sudo vim /etc/redis/redis.conf
    ```

2.  **修改绑定地址 (bind)**：
    找到 `bind 127.0.0.1 ::1` 这一行。
    *   **修改为**：`bind 0.0.0.0` (允许所有 IP 连接)
    *   或者：`bind 127.0.0.1 172.19.186.11` (只允许本地和内网 IP)

3.  **关闭保护模式 (protected-mode)**：
    找到 `protected-mode yes`。
    *   **修改为**：`protected-mode no`
    *   *(注意：关闭保护模式后，Redis 将暴露在内网中。如果您的阿里云安全组已限制公网访问 6379 端口，这通常是安全的。或者您可以设置 `requirepass` 密码，但那样需要修改我们的 C++ 代码来支持密码认证。目前建议先关闭保护模式跑通功能。)*

### 步骤 2: 重启 Redis 服务 (在第一台服务器上操作)

```bash
sudo systemctl restart redis-server
```

### 步骤 3: 验证配置

在第一台服务器上查看 Redis 是否监听了 `0.0.0.0:6379`：

```bash
netstat -tulnp | grep 6379
# 输出应该包含 0.0.0.0:6379
```

### 步骤 4: 重试第二台服务器

再次在第二台服务器上启动 `httpserver`，应该就能连接成功了。

---

### 关于 ZooKeeper 错误的说明
日志中显示：`[ZK] Create failed for /gw-server/nodes/node_172.19.186.12_52487, code: -101`

*   **原因**：`-101` 表示节点已存在 (`ZNODEEXISTS`)。这说明您的第二台服务器之前已经成功在 ZooKeeper 上注册过一次，且连接未断开（或者是上次非正常退出，临时节点还没过期）。
*   **处理**：这是**正常现象**，不影响使用。程序逻辑中，如果节点已存在，会忽略创建错误并继续运行，此时实际上它已经“注册”成功了（或者说复用了之前的注册）。


## 修改总结

                                                                                                                                        
  问题原因：                                                                                                                            
  - 设备心跳时会将信息写入 Redis (device:online:<device_id>)                                                                            
  - 首页 / 的 get_connections_html() 会查询 Redis 获取全网设备
  - 但 /api/devices 和 /api/connections 只返回本地连接，没有查询 Redis

  修改内容 (http_server.cpp)：

  1. /api/devices - 现在返回全网设备列表：
    - 获取本地设备
    - 从 Redis 查询 device:online:* 获取其他节点设备
    - 合并、去重后返回
  2. /api/connections - 现在返回全网连接详情：
    - 本地连接：增加 location: "local" 字段
    - 远程连接：增加 location: "remote"、node_ip、node_port 字段
    - 新增 remote_devices 统计字段

  现在的完整链路：
  设备连接 → 心跳 → 写入 Redis (device:online:<id>)
                          ↓
  前端调用 /api/devices 或 /api/connections
                          ↓
  查询本地连接 + 查询Redis全网设备
                          ↓
  返回全网设备列表（节点A和节点B都能看到）

  重启服务后，节点B的前端应该能看到节点A连接的设备了。

## 分布式路由

问题排查总结                                                                                                                          
                                                
  根本原因：                                                                                                                            
  原来的路由判断逻辑 (http_server.cpp:693)：                                                                                            
  if (target_port != ServerApp::getInstance().GetHttpPort())

  问题是：两个节点的HTTP端口都是8080，所以条件不满足，直接跳过了代理转发，导致返回 "device not connected or not registered locally"
  错误。

  修复方案：
  改为判断目标IP是否为本机：
  bool is_local = (target_ip == ServerApp::getInstance().GetLocalIp() ||
                   target_ip == "127.0.0.1" ||
                   target_ip == "localhost") &&
                  (target_port == ServerApp::getInstance().GetHttpPort());

  if (target_port > 0 && !target_ip.empty() && !is_local) {
      // 转发到目标节点
  }

  修复逻辑：
  - 只有当 目标IP = 本机IP 且 目标端口 = 本机端口 时，才认为设备在本机
  - 否则都需要HTTP代理转发到目标节点

  测试步骤：
  1. 重启两个节点的服务
  2. 客户端连接节点A
  3. 在节点B前端调用 /api/send_b341 发送指令
  4. 应该能成功路由到节点A并转发到设备

## 设备上线redis与前端不匹配bug     

  1. 设备断开时没有删除Redis记录 - 导致其他节点无法感知设备已离线
  2. Redis TTL太短(60秒) - 设备心跳间隔稍长就会导致Redis记录过期，其他节点无法路由
  3. 跨节点路由判断逻辑错误 - 之前已修复（判断IP而不是端口）

  修复内容：

  1. recvfile.cpp - 设备断开时删除Redis记录
  // CloseConnection 中添加：
  if (!device_id.empty()) {
      if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
          std::string key = "device:online:" + device_id;
          redis->Del(key);
      }
  }

  2. recvfile.cpp - 增加Redis TTL到180秒
  redis->Set(key, jsonVal, 180);  // 180秒TTL

  3. http_server.cpp - 跨节点路由判断 (之前已修复)
  bool is_local = (target_ip == ServerApp::getInstance().GetLocalIp() ||
                   target_ip == "127.0.0.1" ||
                   target_ip == "localhost") &&
                  (target_port == ServerApp::getInstance().GetHttpPort());

  完整链路验证：

  设备连接B节点 → 心跳 → Redis: device:online:XXX → {ip:B, http_port:8080, TTL:180s}
                          ↓
  节点A查询 /api/devices → Redis Keys → 返回全网设备列表
                          ↓
  节点B前端要图 → find_connection_by_device_id (本机找到) → 直接发送
                          ↓
  设备断开 → CloseConnection → 删除Redis记录 → 各节点前端同步下线

  现在可以重启服务测试了。

## 客户端异常断联
  根本原因：EventLoop只处理了EPOLLIN事件，没有处理EPOLLRDHUP（对端关闭连接）、EPOLLHUP（连接挂起）、EPOLLERR（连接错误）事件。                      
                                                                                                                                                    
  当客户端主动断开连接时：
  - 如果是优雅关闭（发送FIN包），epoll会触发EPOLLIN事件，read()返回0，代码能检测到
  - 但在某些情况下（如网络异常、客户端崩溃），可能直接触发EPOLLRDHUP/EPOLLHUP事件，而不是EPOLLIN

  修复内容

  EventLoop.h - 增加对连接断开事件的处理：
  // 处理断开连接事件：EPOLLRDHUP(对端关闭) | EPOLLHUP(挂起) | EPOLLERR(错误)
  if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
      if (callbacks.find(fd) != callbacks.end()) {
          callbacks[fd](fd);  // 触发断开回调
      }
      continue;
  }

  这样，当检测到连接断开事件时，会触发OnClientRead回调，进而调用CloseConnection，清理本地连接和Redis记录。

  完整链路

  客户端断开连接 → epoll检测到EPOLLRDHUP/EPOLLHUP/EPOLLERR
      → 触发OnClientRead → CloseConnection
          → 删除本地连接上下文
          → 删除Redis记录(device:online:XXX)

## 7. 分布式节点查询与客户端连接断开修复

在此次测试中，遇到并修复了两个关键的多节点/连接状态反馈问题：

### 7.1 Node B 无法看到 Node A 上的设备
**原因分析：**
在分布式部署下，Node A 和 Node B 是独立启动的网关服务器。默认情况下，如果不指定 Redis 地址参数，每一台服务器可能会连上本地的 Redis 实例（即未配置 `-r` 参数），而不是同一个中心化 Redis。导致虽然 Node A 的设备注册到了 Redis，Node B 由于连的是自己的 Local Redis（或没传正确地址），所以查不到。
**解决方案：**
启动 Node B 时，必须通过 `-r <Redis_IP>:<Port>` 显式指定中心化 Redis 的连接地址。这样 Node B 前端拉取设备列表时，就会直接去所有节点共享的这个 Redis 实例里寻找键为 `device:online:*` 的数据。

### 7.2 服务端终止后，客户端未及时断开
**原因分析：**
客户端 `ClientApp.cpp` 中的 `epoll_wait` 没有正确地监听和处理服务端进程被 Kill（`EPOLLRDHUP`, `EPOLLHUP`, `EPOLLERR`）等异常断开的事件。不仅如此，在客户端的 `EventLoop` 中可能只关注了 `EPOLLIN` 却没有对 TCP 的 RST 被动关闭做优雅处理，导致客户端陷入死等状态，认为连接依然存在。
**解决方案：**
在客户端（及服务端）的 Reactor 检测逻辑中（如 `ClientApp::Run()` / `EventLoop` 等相关处），加入对系统中断或对方挂断 socket 的处理：当检测到对端断开，立即打出日志并清理该 fd，调用相应的 `close(fd)` 并重置连接状态。
