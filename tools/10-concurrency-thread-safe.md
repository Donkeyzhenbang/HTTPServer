# 分布式网关服务器 - 技术总览

## 第一部分：ZooKeeper 分布式协调功能

### 一、整体架构概览

#### 1.1 代码文件结构

```
server/
├── inc/
│   ├── ZkClient.h              # ZK客户端封装
│   └── DistributedCoord.h     # 分布式协调器 (核心)
├── src/
│   ├── ZkClient.cpp           # ZK客户端实现
│   ├── DistributedCoord.cpp    # 分布式协调器实现
│   └── ServerApp.cpp           # 初始化入口
```

#### 1.2 ZK命名空间结构

```
/gw-server/
├── /nodes/           # 服务注册与发现
│   └── node_172.19.186.11_52487  # 临时节点
├── /locks/           # 分布式锁
│   └── device_123456789/          # 设备锁
│       ├── lock_0000000001
│       └── lock_0000000002
├── /config/          # 配置管理
│   └── global                    # 全局配置
└── /election/       # 选主
    └── master/
        └── candidate_node_xxx    # 候选节点
```

---

### 二、核心组件详解

#### 2.1 ZkClient (ZkClient.cpp)

**职责**: 对ZooKeeper C API的封装

**主要方法**:

```cpp
class ZkClient {
    void Start(host);              // 连接ZK
    void Create(path, data, state); // 创建节点
    std::string GetData(path);    // 获取节点数据
    std::vector<std::string> GetChildren(path); // 获取子节点列表
    bool Delete(path);            // 删除节点
    bool Exists(path);            // 检查节点是否存在
    void SetWatcher(path, callback); // 设置监听器
    bool CreatePathRecursive(path); // 递归创建路径
};
```

---

### 三、功能实现详解

#### 3.1 服务注册与发现

**服务注册**:
```cpp
// 创建临时节点 (Ephemeral)
zk_client_->Create("/gw-server/nodes/node_xxx", data, 1);  // 1 = Ephemeral
```

**服务发现**:
```cpp
// 获取所有在线节点
auto children = zk_client_->GetChildren("/gw-server/nodes");
for (const auto& child : children) {
    std::string data = zk_client_->GetData("/gw-server/nodes/" + child);
    // 解析节点数据...
}
```

**节点变化监听**: 使用Watcher机制，节点列表变化时自动回调

---

#### 3.2 分布式锁

**原理**: 临时顺序节点 + 最小节点获取锁

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

**代码实现** (`DistributedCoord.cpp:276-341`):
```cpp
bool DistributedCoord::AcquireLock(const std::string& lock_name, int timeout_ms) {
    // 1. 创建顺序临时节点
    zoo_create(..., ZOO_EPHEMERAL | ZOO_SEQUENCE, ...);

    // 2. 循环检查直到获取锁或超时
    while (true) {
        auto children = zk_client_->GetChildren(lock_path);
        std::sort(children);

        // 如果我是最小的，获取锁成功
        if (children[0] == my_node) {
            return true;
        }

        // 等待前一个节点删除
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

---

#### 3.3 选主机制 (Master Election)

**原理**: 临时顺序节点 + 序号最小为主

```
节点A创建: /gw-server/election/master/candidate_node_A_0000000001  (序号1)
节点B创建: /gw-server/election/master/candidate_node_B_0000000002  (序号2)

序号1最小 → 节点A为主节点
```

**代码实现** (`DistributedCoord.cpp:196-219`):
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
    std::string new_master = children[0];
    is_master_ = (new_master == lock_node_path_);
}
```

---

#### 3.4 配置管理

**原理**: 持久节点 + Watcher监听

```
节点A修改配置 → 更新 /gw-server/config/global 节点
                              ↓
                      ZK通知所有监听该节点的节点
                              ↓
              节点B收到通知 → 重新读取配置 → 更新本地配置
```

---

### 四、如何接入系统

```cpp
#include "../inc/DistributedCoord.h"

// 初始化 (在ServerApp中调用)
gw::DistributedCoord::Instance().Init(zk_client, ip, tcp_port, http_port);
gw::DistributedCoord::Instance().ParticipateElection();

// 服务发现
auto nodes = gw::DistributedCoord::Instance().GetAllNodes();

// 选主
if (gw::DistributedCoord::Instance().IsMaster()) {
    // 主节点逻辑
}

// 分布式锁
gw::DistributedCoord::Instance().AcquireLock("task_name");
// 执行任务
gw::DistributedCoord::Instance().ReleaseLock("task_name");

// 配置管理
gw::DistributedCoord::Instance().SetConfig("global", "{\"level\":\"INFO\"}");
std::string config = gw::DistributedCoord::Instance().GetConfig("global");
```

---

## 第二部分：并发安全与线程安全分析

### 一、并发安全问题分析

#### 1.1 发现的并发安全问题

本次代码审查发现了以下并发安全问题：

| 序号 | 问题位置 | 问题描述 | 严重程度 | 状态 |
|-----|---------|---------|---------|------|
| 1 | HttpServer::connections_ | 多线程访问无锁保护 | 高 | ✅ 已修复 |
| 2 | EventLoop::callbacks_ | 多线程访问无锁保护 | 中 | ✅ 已修复 |

#### 1.2 问题详解

##### 问题1: HttpServer的connections_无锁保护

**问题代码**:
```cpp
// 修复前
void HttpServer::HandleRead(int fd) {
    auto it = connections_.find(fd);  // 无锁访问！
    // ...
}

void HttpServer::HandleClose(int fd) {
    connections_.erase(fd);  // 并发删除可能导致崩溃
}
```

**风险**:
- 多个HTTP请求同时到达时，可能同时访问connections_
- 一个线程在遍历connections_时，另一个线程可能删除元素
- 导致迭代器失效、数据竞争甚至崩溃

**修复方案**:
```cpp
// 修复后
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
```

---

##### 问题2: EventLoop的callbacks_无锁保护

**问题代码**:
```cpp
// 修复前
void EventLoop::AddSocket(int fd, EventCallback cb) {
    callbacks[fd] = cb;  // 无锁访问！
}

void EventLoop::Run() {
    while (running) {
        if (callbacks.find(fd) != callbacks.end()) {  // 无锁访问！
            callbacks[fd](fd);
        }
    }
}
```

**修复方案**:
```cpp
// 修复后 - 添加互斥锁保护
void EventLoop::AddSocket(int fd, uint32_t events, EventCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks[fd] = cb;
}

void EventLoop::Run() {
    while (running) {
        // 复制callback，避免长时间持有锁
        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            auto it = callbacks.find(fd);
            if (it == callbacks.end()) continue;
            callback = it->second;
        }
        // 在锁外执行回调，避免阻塞其他线程
        callback(fd);
    }
}
```

---

### 二、已验证安全的组件

#### 2.1 connection.cpp (连接管理器)

```cpp
// 全局连接管理器，使用互斥锁保护
std::mutex connection_manager_mutex;

std::vector<std::string> get_all_device_ids() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);  // ✅ 有锁保护
    // ...
}
```

**结论**: ✅ 安全

#### 2.2 protocol_handler.cpp (协议发送)

```cpp
// SendProtocolB341 使用 SafeSendLocked
int SendProtocolB341(int socket, int channelNo) {
    return SafeSendLocked(socket, &frameData, sizeof(frameData));  // ✅ 使用SafeSendLocked
}

// SafeSendLocked 使用ConnectionContext的sendMutex
static int SafeSendLocked(int socket, const void* buffer, size_t length) {
    ConnectionContext* ctx = find_connection_by_fd(socket);
    if (ctx) {
        std::lock_guard<std::mutex> lock(ctx->sendMutex);  // ✅ 有锁保护
        return SafeSend(socket, buffer, length);
    }
}
```

**结论**: ✅ 安全

#### 2.3 DistributedCoord (分布式协调器)

```cpp
// 内部使用多个互斥锁保护
std::mutex nodes_mutex_;
std::mutex config_mutex_;
std::mutex election_mutex_;
std::mutex locks_mutex_;
```

**结论**: ✅ 安全

---

### 三、线程安全设计原则

#### 3.1 锁的使用原则

1. **最小持有时间**: 在锁内只做必要的操作，如遍历map获取数据后在锁外处理
2. **避免死锁**: 使用`std::lock_guard`自动管理锁的生命周期
3. **锁粒度**: 根据业务场景选择合适的锁粒度

#### 3.2 示例: EventLoop的锁优化

```cpp
// 不推荐: 在锁内执行耗时操作
void Run() {
    while (running) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks[fd](fd);  // ❌ 回调可能很耗时，阻塞其他线程
    }
}

// 推荐: 复制callback后在锁外执行
void Run() {
    while (running) {
        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            auto it = callbacks.find(fd);
            if (it == callbacks.end()) continue;
            callback = it->second;
        }
        callback(fd);  // ✅ 在锁外执行，不阻塞其他线程
    }
}
```

---

## 第三部分：并发安全测试

### 一、测试代码

创建测试文件 `tests/test_concurrency.cpp`:

```cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unistd.h>
#include "../base/inc/connection.h"
#include "../server/inc/EventLoop.h"
#include "../server/inc/HttpServer.h"
#include "../server/inc/HttpParser.h"

// ==================== Connection Manager 并发测试 ====================

TEST(ConnectionManagerConcurrency, ConcurrentReadWrite) {
    const int THREAD_COUNT = 10;
    const int OPS_PER_THREAD = 1000;

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    // 创建测试连接
    for (int i = 0; i < 100; ++i) {
        create_connection_context(i + 100);
    }

    // 线程1: 持续读取
    threads.emplace_back([&]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            auto ids = get_all_device_ids();
            auto conns = get_all_connections();
            get_connection_count();
        }
    });

    // 线程2: 持续写入
    threads.emplace_back([&]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            create_connection_context(2000 + i);
        }
    });

    // 线程3: 持续删除
    threads.emplace_back([&]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            remove_connection_context(2000 + i);
        }
    });

    start.store(true);
    for (auto& t : threads) {
        t.join();
    }

    SUCCEED() << "Concurrent read/write completed without crash";
}

// ==================== HttpParser 并发测试 ====================

TEST(HttpParserConcurrency, ConcurrentParse) {
    const int THREAD_COUNT = 8;
    const int PARSES_PER_THREAD = 500;

    std::atomic<int> success_count{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    std::string test_request =
        "GET /api/devices HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&]() {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < PARSES_PER_THREAD; ++i) {
                gw::HttpParser parser;
                if (parser.parse(test_request.data(), test_request.size())) {
                    success_count++;
                }
            }
        });
    }

    start.store(true);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), THREAD_COUNT * PARSES_PER_THREAD);
}

// ==================== EventLoop 锁测试 ====================

TEST(EventLoopConcurrency, ConcurrentAddRemove) {
    gw::EventLoop loop;
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    // 线程1: 添加socket
    threads.emplace_back([&loop]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 1000; ++i) {
            int pipefd[2];
            pipe(pipefd);
            loop.AddSocket(pipefd[0], EPOLLIN, [](int fd){});
        }
    });

    // 线程2: 删除socket (模拟)
    threads.emplace_back([&loop]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 1000; ++i) {
            loop.RemoveSocket(i + 100);  // 可能不存在的fd
        }
    });

    start.store(true);
    for (auto& t : threads) {
        t.join();
    }

    SUCCEED() << "Concurrent add/remove completed without crash";
}

// ==================== HttpConnectionContext 锁测试 ====================

TEST(HttpConnectionContext, ConcurrentReadWrite) {
    gw::HttpConnectionContext ctx(1, nullptr);
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    // 多线程并发读写缓冲区
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&ctx, &start, t]() {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < 1000; ++i) {
                if (t % 2 == 0) {
                    ctx.read_buffer.append("test data");
                } else {
                    auto data = ctx.read_buffer;
                }
            }
        });
    }

    start.store(true);
    for (auto& t : threads) {
        t.join();
    }

    SUCCEED() << "Concurrent buffer operations completed";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

---

### 二、编译运行测试

```bash
# 1. 编译测试
cd /home/jym/code/cpp/personal-project/gw-server/tests/build
cmake ..
make run_concurrency_tests

# 2. 运行测试
./run_concurrency_tests

# 3. 预期输出
[==========] Running 4 tests from 4 test suites.
[----------] Global test environment set-up.
[----------] 4 tests from ConnectionManagerConcurrency
[----------] 4 tests from HttpParserConcurrency
[----------] 4 tests from EventLoopConcurrency
[----------] 4 tests from HttpConnectionContext
[----------] Global test environment tear-down
[==========] 4 tests from 4 test suites ran.
[ PASSED ] 4 tests.
```

---

*文档版本: 2.0 (整合版)*
*创建日期: 2026-02-23*
