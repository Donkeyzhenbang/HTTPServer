# 分布式网关服务器 - 开发总结

## 一、项目概述

本项目是一个**分布式物联网网关服务器**，用于实现设备远程图像传输和模型升级功能。项目采用多模块架构，包含 C++ 后端服务器、Python Agent 服务和 Web 前端界面。

### 1.1 项目架构

```
gw-server/
├── base/              # 基础库模块 - 协议、连接、线程池等核心组件
├── client/            # 客户端模块 - 模拟设备端发送图像/报警数据
├── server/            # 服务器模块 - 核心网关服务器
│   ├── agent/         # Python Agent 服务 (AI 智能助手)
│   └── frontend/      # Web 前端界面
├── tests/             # 测试模块 - 单元测试和集成测试
└── tools/             # 设计文档和技术笔记
```

---

## 二、技术栈

### 2.1 后端技术

| 技术 | 版本/库 | 用途 |
|------|---------|------|
| **C++17** | GCC 9+ | 核心服务器开发 |
| **Epoll** | Linux 系统调用 | 高性能非阻塞 I/O 事件循环 |
| **线程池** | 自研 | 多线程并发处理业务 |
| **ZooKeeper** | zookeeper_mt | 分布式协调（服务发现、选主、分布式锁） |
| **Redis** | Hiredis | 设备会话管理、在线状态存储 |
| **OpenSSL** | - | 加密功能 |
| **httplib** | 可选 | HTTP 客户端/服务器 |
| **GTest** | - | 单元测试框架 |

### 2.2 前端技术

| 技术 | 用途 |
|------|------|
| **HTML/CSS/JavaScript** | Web 前端界面 |
| **Nginx** | 反向代理和负载均衡 |

### 2.3 AI Agent 技术

| 技术 | 用途 |
|------|------|
| **Python 3.x** | Agent 服务开发 |
| **FastAPI** | Web 框架 |
| **阿里云 DashScope** | 大模型 SDK |
| **LangChain** | LLM 应用开发框架 |
| **Uvicorn** | ASGI 服务器 |

### 2.4 基础设施

| 服务 | 端口 | 用途 |
|------|------|------|
| **Nginx** | 80 | 统一入口反向代理 |
| **C++ HTTPServer** | 8080 | 后端 HTTP 服务 |
| **Agent 服务** | 8000 | AI 智能助手 |
| **TCP 网关** | 52487 | 设备长连接 |

---

## 三、开发中遇到的困难及解决方案

### 3.1 高性能网络服务器设计

#### 困难描述

需要设计一个能够处理大量设备连接的网关服务器，要求支持：
- 高并发设备连接
- 实时图像数据传输
- 心跳保活机制
- 协议解析与分发

#### 解决方案

**采用 Epoll + 线程池架构**：

```cpp
// 事件循环注册
eventLoop.AddSocket(serverSocket, EPOLLIN | EPOLLET, [this](int fd){
    this->acceptLoop();
});

// 新连接处理
void ServerApp::handleNewConnection(int connfd) {
    int flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
    create_connection_context(connfd);
    eventLoop.AddSocket(connfd, EPOLLIN | EPOLLET | EPOLLRDHUP, OnClientRead);
}
```

**自定义二进制协议设计**：
- 帧类型 + 包类型 + 数据结构
- 支持心跳、图像数据、报警信息等多种协议
- 协议处理采用回调注册机制，支持热插拔

---

### 3.2 并发安全与线程安全

#### 困难描述

代码审查中发现多处并发安全问题：
- `HttpServer::connections_` 多线程访问无锁保护
- `EventLoop::callbacks_` 多线程访问无锁保护

#### 解决方案

**添加互斥锁保护**：

```cpp
// 修复后 - HttpServer
void HttpServer::HandleRead(int fd) {
    std::shared_ptr<HttpConnectionContext> ctx;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        ctx = it->second;
    }
    // 使用ctx...
}

// 修复后 - EventLoop
void EventLoop::AddSocket(int fd, uint32_t events, EventCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_[fd] = cb;
}
```

**锁使用原则**：
- 最小持有时间：遍历 map 获取数据后在锁外处理
- 使用 `std::lock_guard` 自动管理锁的生命周期
- 复制 callback 后在锁外执行，避免阻塞

---

### 3.3 分布式架构设计

#### 困难描述

从单机架构扩展到分布式多节点部署，需要解决：
- 服务注册与发现
- 设备会话管理
- 跨节点请求路由
- 负载均衡

#### 解决方案

**1. ZooKeeper 服务注册与发现**

```cpp
// 服务注册 - 创建临时节点
zk_client_->Create("/gw-server/nodes/node_xxx", data, 1);  // 1 = Ephemeral

// 服务发现 - 获取所有在线节点
auto children = zk_client_->GetChildren("/gw-server/nodes");
```

**2. Redis 设备会话管理**

```cpp
// 设备上线写入 Redis
Key: device:online:<DeviceID>
Value: {"ip": "NodeIP", "port": TCP_Port, "http_port": HTTP_Port}
TTL: 60秒
```

**3. HTTP 请求跨节点路由**

```cpp
// 查询设备所在节点
auto nodeInfo = redis_client_->GetDeviceNode(deviceId);

// 如果设备在当前节点，直接处理
// 如果设备在其他节点，HTTP 代理转发
if (nodeInfo.ip != currentIp) {
    httplib::Client proxy(nodeInfo.ip, nodeInfo.http_port);
    proxy.Post("/api/...", ...);
}
```

---

### 3.4 分布式协调功能

#### 困难描述

需要实现分布式选主、分布式锁、配置管理等高级特性。

#### 解决方案

**1. 分布式选主（基于临时顺序节点）**

```cpp
void DistributedCoord::ParticipateElection() {
    // 创建临时顺序节点
    zoo_create(..., ZOO_EPHEMERAL | ZOO_SEQUENCE, ...);
    StartElectionWatcher();
}

void DistributedCoord::OnElectionChanged() {
    auto children = zk_client_->GetChildren(election_path_);
    std::sort(children);
    // 序号最小的为主节点
    is_master_ = (children[0] == lock_node_path_);
}
```

**2. 分布式锁（临时顺序节点 + 最小节点获取锁）**

```cpp
bool DistributedCoord::AcquireLock(const std::string& lock_name, int timeout_ms) {
    // 1. 创建顺序临时节点
    zoo_create(..., ZOO_EPHEMERAL | ZOO_SEQUENCE, ...);

    // 2. 循环检查直到获取锁或超时
    while (true) {
        auto children = zk_client_->GetChildren(lock_path);
        std::sort(children);
        if (children[0] == my_node) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

**3. 配置管理（持久节点 + Watcher）**

```
节点A修改配置 → ZK通知所有监听节点 → 重新读取配置
```

---

### 3.5 自研 HTTP 服务器

#### 困难描述

在已有基于 epoll 的 TCP 服务器基础上，需要支持 HTTP 协议用于 Web 管理和 API 接口。

#### 解决方案

**自研 HTTP 服务器**（基于 epoll）：
- HTTP 解析器：解析请求行、Header、Body
- 路由管理：支持路径到处理函数的映射
- 连接管理：Keep-Alive、Chunked 编码支持

```cpp
// HTTP 路由注册
httpServer.Get("/api/devices", [](const HttpRequest& req, HttpResponse& res) {
    auto devices = get_all_device_ids();
    res.set_json(devices);
});
```

---

### 3.6 模型升级功能

#### 困难描述

需要支持远程设备固件/模型文件升级，要求：
- 大文件分片传输
- 断点续传
- 升级进度追踪
- 失败回滚

#### 解决方案

**模型升级协议**：
```
B361: 升级准备 → B362: 确认
B363: 发送元数据 → B364: 确认
B365: 发送模型数据 → B366: 确认
```

---

### 3.7 AI Agent 集成

#### 困难描述

需要集成 AI 能力，为设备管理提供智能助手功能。

#### 解决方案

**基于阿里云 DashScope 的 Agent 服务**：

```python
# FastAPI 服务
app = FastAPI()

# Agent 核心逻辑
from dashscope import Generation
response = Generation.call(
    model='qwen-plus',
    messages=[{"role": "user", "content": query}]
)
```

---

## 四、解决的问题与场景

### 4.1 核心业务场景

| 场景 | 描述 |
|------|------|
| **设备连接管理** | 大量物联网设备通过 TCP 长连接接入服务器 |
| **图像传输** | 设备端主动上报图像数据到服务器 |
| **报警推送** | 设备告警信息实时推送到管理平台 |
| **模型升级** | 远程向设备推送 AI 模型更新 |
| **智能助手** | AI Agent 辅助设备管理和问题诊断 |

### 4.2 技术解决的问题

| 问题 | 解决方案 |
|------|----------|
| **高并发连接** | Epoll 非阻塞 I/O + 线程池 |
| **分布式部署** | ZooKeeper + Redis 分布式架构 |
| **服务高可用** | 分布式选主实现主备切换 |
| **资源竞争** | 分布式锁解决跨节点资源访问 |
| **配置一致性** | ZooKeeper 配置中心化管理 |
| **负载均衡** | Nginx + 多节点部署 |

---

## 五、项目部署

### 5.1 启动命令

```bash
# 编译服务器
cd server/build && cmake .. && make

# 启动服务器
./bin/httpserver -p 52487 -w 8080 -z 127.0.0.1:2181 -r 127.0.0.1:6379 -i 127.0.0.1
```

### 5.2 服务依赖

```bash
# 启动基础设施
sudo service redis-server start
sudo service zookeeper start

# 启动 Nginx
sudo nginx

# 启动 AI Agent
source ~/agent-venv/bin/activate
python server/agent/main.py
```

---

## 六、总结

本项目从单机网关服务器逐步演进为分布式物联网平台，经历了以下技术挑战：

1. **网络编程**：从基础 socket 封装到高性能 epoll 事件循环
2. **并发安全**：从无锁设计到完善的线程安全机制
3. **分布式架构**：从单点服务到多节点协同
4. **生态集成**：从基础 HTTP 到 AI Agent 智能助手

通过解决这些技术难点，项目实现了高并发、高可用、分布式的物联网网关服务能力。
