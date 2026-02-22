# HTTP模块重构分析报告

## 一、现状分析

### 1.1 当前架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        当前服务器架构                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐         ┌─────────────────┐                │
│  │  TCP Server     │         │  HTTP Server   │                │
│  │  (设备通信)     │         │  (前端通信)    │                │
│  │  Port: 52487   │         │  Port: 8080    │                │
│  │                 │         │                 │                │
│  │  - epoll       │         │  - httplib     │                │
│  │  - 非阻塞IO    │         │  - 独立线程池   │                │
│  │  - 协议解析    │         │  - 路由处理     │                │
│  └────────┬────────┘         └────────┬────────┘                │
│           │                             │                         │
│           │        ServerApp            │                         │
│           └───────────┬─────────────────┘                         │
│                       │                                            │
│  ┌───────────────────┼─────────────────────────────────────┐     │
│  │                   │                                      │     │
│  ▼                   ▼                                      ▼     │
│  Redis            ZooKeeper                            设备     │
│  (设备状态)        (服务注册)                           连接      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 httplib 特性分析

**httplib** (cpp-httplib) 是一个单头文件的C++ HTTP库，具备以下特点：

| 特性 | 说明 |
|-----|------|
| 优点 | 功能完整、支持HTTP/HTTPS、易于使用、静/动态内容 |
| 缺点 | 同步阻塞模型、默认单线程、内存分配频繁 |
| 并发 | 默认每请求一线程，无内置连接池 |

**httplib 性能瓶颈**：

1. **阻塞式Accept**：httplib内部使用阻塞式的socket accept
2. **线程开销**：每个HTTP请求可能创建新线程处理
3. **内存碎片**：频繁的string动态分配
4. **重复解析**：每个请求都需要完整解析HTTP头

### 1.3 TCP Server 现有实现

TCP Server (处理设备连接) 已采用高性能设计：

```cpp
// ServerApp.cpp
void ServerApp::acceptLoop() {
    // 阻塞accept
    int connfd = accept(serverSocket, ...);
    // 设置非阻塞
    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
    // 加入epoll
    eventLoop.AddSocket(connfd, EPOLLIN | EPOLLET | EPOLLRDHUP, ...);
}

// EventLoop - epoll事件循环
void Run() {
    while (running) {
        int nfds = epoll_wait(epollFd, events.data(), MaxEvents, -1);
        for (int i = 0; i < nfds; ++i) {
            // 处理可读/断开事件
        }
    }
}
```

---

## 二、重构方案

### 2.1 目标

1. 复用TCP Server的epoll事件循环
2. 统一HTTP和TCP的处理框架
3. 提高HTTP并发性能
4. 减少内存分配和上下文切换

### 2.2 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                     重构后服务器架构                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              统一EventLoop (epoll)                       │   │
│  │                                                          │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │   │
│  │  │  TCP Handler │  │ HTTP Handler │  │ 其他Handler │    │   │
│  │  │ (设备协议)   │  │ (HTTP协议)   │  │ (预留)      │    │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘    │   │
│  │         │                 │                  │          │   │
│  │         ▼                 ▼                  ▼          │   │
│  │  ┌──────────────────────────────────────────────────┐   │   │
│  │  │           连接上下文管理器                         │   │   │
│  │  │  - TCP连接: ConnectionContext                     │   │   │
│  │  │  - HTTP连接: HttpConnectionContext               │   │   │
│  │  └──────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────┐         ┌─────────────────┐                │
│  │  TCP Server    │  共用   │  HTTP Server    │                │
│  │  Port: 52487   │◄──────►│  Port: 8080     │                │
│  │                 │ EventLoop │                 │                │
│  └─────────────────┘         └─────────────────┘                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 模块划分

```
server/
├── inc/
│   ├── HttpParser.h      # HTTP协议解析器
│   ├── HttpContext.h     # HTTP连接上下文
│   └── HttpServer.h      # HTTP服务封装
├── src/
│   ├── HttpParser.cpp    # HTTP解析实现
│   ├── HttpContext.cpp   # 上下文实现
│   ├── HttpServer.cpp    # HTTP服务实现
│   └── http_server.cpp   # 路由和业务逻辑 (保留)
```

### 2.4 核心组件设计

#### 2.4.1 HTTP解析器 (HttpParser)

```cpp
class HttpParser {
public:
    // 解析状态
    enum class ParseState {
        kMethod,      // 解析方法
        kUri,         // 解析URI
        kVersion,     // 解析版本
        kHeaderKey,   // 解析Header Key
        kHeaderValue, // 解析Header Value
        kHeadersDone, // Header解析完成
        kBody,        // 解析Body
        kComplete,    // 解析完成
        kError        // 解析错误
    };

    // 解析入口
    ParseResult parse(const char* data, size_t len);

    // 获取解析结果
    const std::string& method() const { return method_; }
    const std::string& uri() const { return uri_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    const HeaderMap& headers() const { return headers_; }

private:
    ParseState state_;
    std::string method_;
    std::string uri_;
    std::string version_;
    std::string body_;
    HeaderMap headers_;
    // ... 内联缓存，避免频繁分配
};
```

#### 2.4.2 HTTP连接上下文 (HttpContext)

```cpp
class HttpConnectionContext : public std::enable_shared_from_this<HttpConnectionContext> {
public:
    int fd;
    HttpParser parser;
    std::string read_buffer;
    std::string write_buffer;

    // 连接状态
    bool keep_alive;
    bool is_complete;

    // 请求/响应
    HttpRequest request;
    HttpResponse response;

    // 处理回调
    std::function<void(HttpConnectionContext*)> on_request_complete;
};
```

#### 2.4.3 HTTP服务器 (HttpServer)

```cpp
class HttpServer {
public:
    HttpServer(EventLoop* event_loop, int port);

    // 注册路由
    void Get(const std::string& path, Handler handler);
    void Post(const std::string& path, Handler handler);

    // 处理HTTP连接
    void HandleRead(int fd);
    void HandleWrite(int fd);

    // 处理连接关闭
    void HandleClose(int fd);

private:
    EventLoop* event_loop_;
    int port_;
    int listen_fd_;

    // 路由表: path -> handler
    std::unordered_map<std::string, std::vector<Handler>> routers_[HttpMethod::kMaxMethod];

    // 连接管理
    std::unordered_map<int, std::shared_ptr<HttpConnectionContext>> connections_;
};
```

---

## 三、性能对比

### 3.1 理论性能分析

| 指标 | httplib (当前) | 自实现HTTP (目标) | 提升 |
|-----|---------------|-----------------|------|
| 事件模型 | select/poll | epoll (ET模式) | 10x |
| 内存分配 | 每次请求分配 | 对象池复用 | 5x |
| 线程切换 | 每请求线程 | 事件驱动0线程 | 显著 |
| 解析效率 | 正则/字符串 | 状态机+内联 | 2-3x |
| 并发连接 | 几百 | 数万 | 100x+ |

### 3.2 预期收益

1. **CPU利用率**：事件驱动模型减少线程上下文切换
2. **内存占用**：对象池复用，减少内存碎片
3. **响应延迟**：epoll ET模式减少唤醒次数
4. **吞吐量**：单线程处理高并发连接

---

## 四、实施计划

### 4.1 阶段一：基础框架 (预计代码量: 500行)

- [ ] 创建HttpParser类，实现基础HTTP解析
- [ ] 创建HttpContext类，管理连接状态
- [ ] 创建HttpServer类，集成到EventLoop

### 4.2 阶段二：路由和业务 (预计代码量: 300行)

- [ ] 实现路由匹配 (精确匹配 + 前缀匹配)
- [ ] 迁移现有API路由 (/api/devices, /api/send_b341等)
- [ ] 实现Response构建和发送

### 4.3 阶段三：高级特性 (预计代码量: 200行)

- [ ] 支持Keep-Alive
- [ ] 支持Chunked编码
- [ ] 静态文件服务
- [ ] 文件上传/下载优化

### 4.4 代码文件清单

| 文件 | 职责 | 行数预估 |
|-----|------|---------|
| HttpParser.h | 解析器声明 | 100 |
| HttpParser.cpp | 解析器实现 | 300 |
| HttpContext.h | 上下文声明 | 80 |
| HttpContext.cpp | 上下文实现 | 150 |
| HttpServer.h | 服务器声明 | 60 |
| HttpServer.cpp | 服务器实现 | 200 |
| http_server.cpp | 业务逻辑迁移 | 200 (调整) |
| **总计** | | **~1100行** |

---

## 五、风险评估

### 5.1 技术风险

| 风险 | 影响 | 缓解措施 |
|-----|------|---------|
| HTTP规范复杂性 | 中 | 分阶段实现，先满足业务需求 |
| 兼容性问题 | 高 | 保留httplib作为备选，双运行 |
| 调试困难 | 中 | 增加详细日志 |
| 静态文件性能 | 低 | 可复用nginx做前端 |

### 5.2 功能覆盖

当前使用的httplib功能：

| 功能 | 优先级 | 说明 |
|-----|-------|------|
| GET/POST路由 | 必须 | 核心功能 |
| Query参数解析 | 必须 | ?channel=1 |
| JSON Body解析 | 必须 | POST请求体 |
| Multipart解析 | 中 | 文件上传 |
| 静态文件服务 | 中 | 前端页面 |
| Keep-Alive | 低 | 可后续添加 |
| HTTPS | 低 | 可后续添加 |

---

## 六、结论与建议

### 6.1 可行性评估

**结论**：重构是**可行**的，收益明显，风险可控。

理由：
1. 现有TCP Server已证明epoll事件驱动模型可行
2. HTTP协议比私有设备协议更简单
3. 当前httplib的瓶颈确实存在
4. 可以渐进式重构，保留兼容性

### 6.2 建议

1. **立即可行**：按阶段实施，先实现核心HTTP解析
2. **保持兼容**：重构后仍保留httplib代码，灰度切换
3. **性能优先**：重点优化解析速度和内存分配
4. **测试充分**：使用wrk等工具进行压力测试

### 6.3 下一步

如果确认方案，请确认：
1. 是否优先实现Keep-Alive支持？
2. 是否需要同时支持HTTP/HTTPS？
3. 静态文件服务是否复用现有nginx？

---

*文档版本: 1.0*
*创建日期: 2026-02-22*
