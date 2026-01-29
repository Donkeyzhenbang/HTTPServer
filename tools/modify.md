# 改造路线阶段A/B：完整示例修改与注意事项

本文把 todo.md 中“阶段A（架构修复）/阶段B（阿里云优化）”落到可直接对照的代码修改点，并给出注意事项与验证方法。

---

## 阶段A：把“顺序流独占读包”改为“统一读→解析→分发 + 会话状态机”

### A.1 目标

- 单连接里任何时刻都只允许一条“读 socket → 拼字节流 → 解析完整帧 → 分发 handler”的主线。
- handler 不允许再去 read/recv，也不允许在 handler 内部 while-loop “抢占输入流”。
- 图片/模型等大事务改为“会话状态机”，每个分包到来时只更新状态，结束包触发落盘/回复。
- 心跳/控制等非顺序流协议在图片传输过程中也能正常被解析与处理（不会被吞）。

### A.2 需要修改的文件

- [connection.h](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/connection.h)
- [recvfile.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp)
- [recvfile.h](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/recvfile.h)

### A.3 修改示例：connection.h（会话状态 + 连接上下文）

核心点：在每个连接的上下文里放 `image_rx`，并提供连接管理（按 fd 查上下文），使 handler 能在不读 socket 的情况下推进图片接收。

文件：[connection.h](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/connection.h)

```cpp
#pragma once

#include <string>
#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <pthread.h>

struct ImageRxSession {
    bool active = false;
    int channel_no = 0;
    int total_packets = 0;
    std::vector<uint8_t> received;
    std::vector<unsigned char> buffer;
    size_t received_bytes = 0;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point last_packet_at{};
};

class MyQueue{
    std::queue<unsigned char> que;
    pthread_spinlock_t spin;
public:
    MyQueue() { pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE); }
    ~MyQueue() { pthread_spin_destroy(&spin); }
    size_t size(void) {
        size_t size = 0;
        pthread_spin_lock(&spin);
        size = que.size();
        pthread_spin_unlock(&spin);
        return size;
    }
    void push(const unsigned char &__x) {
        pthread_spin_lock(&spin);
        que.push(__x);
        pthread_spin_unlock(&spin);
    }
    void pop() {
        pthread_spin_lock(&spin);
        que.pop();
        pthread_spin_unlock(&spin);
    }
    unsigned char front() {
        unsigned char x = 0;
        pthread_spin_lock(&spin);
        x = que.front();
        pthread_spin_unlock(&spin);
        return x;
    }
};

struct ConnectionContext {
    std::unique_ptr<MyQueue> queue;
    pthread_t read_thread;
    int connfd;
    char device_id[18];
    std::atomic<bool> is_connection_alive;
    ImageRxSession image_rx;

    ConnectionContext(int fd) : connfd(fd), is_connection_alive(true) {
        queue = std::make_unique<MyQueue>();
        read_thread = 0;
        memset(device_id, 0, sizeof(device_id));
    }

    ~ConnectionContext() {
        if (read_thread) {
            pthread_cancel(read_thread);
        }
    }

    void setDeviceId(const char* id) {
        if (id && strlen(id) <= 17) {
            memcpy(device_id, id, 17);
            device_id[17] = '\0';
            std::cout << "[ConnectionContext] 设置设备ID: " << device_id
                      << " for fd=" << connfd << std::endl;
        }
    }

    std::string getDeviceId() const { return device_id[0] ? std::string(device_id) : ""; }
    bool hasDeviceId() const { return device_id[0] != '\0'; }
};

extern std::unordered_map<int, std::unique_ptr<ConnectionContext>> connection_manager;
extern std::mutex connection_manager_mutex;

std::unordered_map<std::string, int> get_device_map();
std::vector<std::string> get_all_device_ids();
ConnectionContext* find_connection_by_device_id(const std::string& device_id);
ConnectionContext* find_connection_by_fd(int fd);
size_t get_connection_count();
std::vector<std::pair<int, std::string>> get_all_connections();
```

注意事项：

- `ImageRxSession::buffer` 现在按 `total_packets * 1024` 预分配，适合“固定 1024 分包”的协议；如果后续有变长分包，需要把每包真实长度也存入会话里。
- `connection_manager` 当前实现是“按 fd 映射上下文”，通过互斥锁保护；后续阶段B若改成 Reactor/epoll，最好把上下文生命周期和 IO 线程绑定，减少跨线程锁争用。

### A.4 修改示例：recvfile.cpp（统一读/解析/分发 + 图片会话）

核心点：

- `HandleClient` 线程只做三件事：`read()` → 追加到 `stream` → 从 `stream` 里提取完整帧后调用 `sFrameResolver`。
- 图片接收拆成三段 handler：`B351(05EF) 开始`、`F0 分包`、`F1 结束`，都只更新 `ctx->image_rx`，不读 socket、不循环抢流。
- 心跳同理：不再用额外 read 的“waitForHeartBeat”，直接在 `HeartbeatHandler` 里更新 device_id 并回包。

文件：[recvfile.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp)

1）从字节流提取完整帧（粘包/半包）

```cpp
static bool try_extract_one_frame(std::vector<unsigned char>& stream, std::vector<unsigned char>& frame) {
    const unsigned char SYNC1 = 0xA5;
    const unsigned char SYNC2 = 0x5A;

    size_t i = 0;
    while (i + 1 < stream.size()) {
        if (stream[i] == SYNC1 && stream[i + 1] == SYNC2) break;
        ++i;
    }
    if (i > 0) {
        stream.erase(stream.begin(), stream.begin() + (std::vector<unsigned char>::difference_type)i);
    }
    if (stream.size() < 4) return false;

    uint16_t packet_len = (uint16_t)stream[2] | ((uint16_t)stream[3] << 8);
    size_t total_len = (size_t)packet_len + 27;
    if (total_len > 2048 || total_len < 8) {
        stream.erase(stream.begin(), stream.begin() + 2);
        return false;
    }
    if (stream.size() < total_len) return false;

    frame.assign(stream.begin(), stream.begin() + (std::vector<unsigned char>::difference_type)total_len);
    stream.erase(stream.begin(), stream.begin() + (std::vector<unsigned char>::difference_type)total_len);
    return true;
}
```

2）HandleClient：统一读/解析/分发（不再额外开“读线程”）

```cpp
void *HandleClient(void *arg) {
    int connfd = *(int *)arg;
    free(arg);

    printf("新客户端线程启动...\n");
    create_connection_context(connfd);

    std::vector<unsigned char> stream;
    stream.reserve(8192);
    unsigned char rxbuf[4096];

    while (1) {
        ssize_t n = read(connfd, rxbuf, sizeof(rxbuf));
        if (n < 0) {
            if (errno == EINTR) continue;
            printf("Socket Read出错: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            printf("客户端断开连接...\n");
            break;
        }

        stream.insert(stream.end(), rxbuf, rxbuf + n);

        std::vector<unsigned char> frame;
        while (try_extract_one_frame(stream, frame)) {
            int chk = CheckFrameFull(frame.data(), (int)frame.size());
            if (chk < 0) {
                continue;
            }
            sFrameResolver(frame.data(), (int)frame.size(), connfd);
        }

        if (stream.size() > 4 * 1024 * 1024) {
            stream.clear();
        }
    }

    remove_connection_context(connfd);
    close(connfd);
    printf("客户端线程结束...\n");
    pthread_exit(NULL);
}
```

3）图片会话：B351/F0/F1 三段推进

```cpp
static void reset_image_session(ImageRxSession& s) {
    s.active = false;
    s.channel_no = 0;
    s.total_packets = 0;
    s.received.clear();
    s.buffer.clear();
    s.received_bytes = 0;
    s.started_at = std::chrono::steady_clock::time_point{};
    s.last_packet_at = std::chrono::steady_clock::time_point{};
}

static int begin_image_session(ConnectionContext* ctx, int channel_no, int total_packets) {
    if (!ctx) return -1;
    if (total_packets <= 0) return -1;

    reset_image_session(ctx->image_rx);
    ctx->image_rx.active = true;
    ctx->image_rx.channel_no = channel_no;
    ctx->image_rx.total_packets = total_packets;
    ctx->image_rx.received.assign((size_t)total_packets, 0);
    ctx->image_rx.buffer.resize((size_t)total_packets * 1024);
    ctx->image_rx.started_at = std::chrono::steady_clock::now();
    ctx->image_rx.last_packet_at = ctx->image_rx.started_at;
    return 0;
}

static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd)
{
    printf("接收到B351 05EF\n");
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return -1;

    ProtocolB351* pB351 = (ProtocolB351*)pBuffer;
    int channelNo = pB351->channelNo;
    int packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);

    if (begin_image_session(ctx, channelNo, packetLen) != 0) {
        return -1;
    }

    SendProtocolB352(fd);
    return 0;
}

static int PhotoDataHandler(unsigned char* pBuffer, int Length, int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return -1;
    if (!ctx->image_rx.active) return 0;

    ProtocolPhotoData* pPhotoPacket = (ProtocolPhotoData*)pBuffer;
    uint16_t subpacketNo = pPhotoPacket->subpacketNo;
    if (subpacketNo >= (uint16_t)ctx->image_rx.total_packets) return 0;

    int payload_len = Length - 32 - 8;
    if (payload_len <= 0) return 0;
    if (payload_len > 1024) payload_len = 1024;

    size_t base = (size_t)subpacketNo * 1024;
    if (base + (size_t)payload_len > ctx->image_rx.buffer.size()) return 0;

    memcpy(ctx->image_rx.buffer.data() + base, pPhotoPacket->sample, (size_t)payload_len);
    if (ctx->image_rx.received[(size_t)subpacketNo] == 0) {
        ctx->image_rx.received[(size_t)subpacketNo] = 1;
        ctx->image_rx.received_bytes += (size_t)payload_len;
    }
    ctx->image_rx.last_packet_at = std::chrono::steady_clock::now();
    return 0;
}

static int PhotoEndHandler(unsigned char*, int, int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return -1;
    if (!ctx->image_rx.active) return 0;

    std::string exe_dir = get_exe_dir();
    std::string upload_dir = exe_dir + "/web/uploads";
    ensure_dir_exists(upload_dir);

    char filename[100];
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::time_t now_time_t = static_cast<std::time_t>(seconds);
    std::tm* local_time = std::localtime(&now_time_t);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", local_time);
    sprintf(filename, "ch%d_%s.jpg", ctx->image_rx.channel_no, time_str);

    std::string fullpath = upload_dir + "/" + std::string(filename);
    std::ofstream ofs(fullpath, std::ios::binary);
    if (ofs) {
        ofs.write(reinterpret_cast<const char*>(ctx->image_rx.buffer.data()),
                  (std::streamsize)ctx->image_rx.received_bytes);
        ofs.close();
    }

    SendProtocolB38(fd);
    reset_image_session(ctx->image_rx);
    return 0;
}
```

4）心跳：在 handler 里注册设备ID + 回包（不破坏输入流）

```cpp
static int HeartbeatHandler(unsigned char* pBuffer, int Length, int fd) {
    if (Length < 4 + 17) return -1;
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return -1;

    char device_id[18] = {0};
    memcpy(device_id, pBuffer + 4, 17);
    device_id[17] = '\0';
    ctx->setDeviceId(device_id);
    SendHeartbeat(fd);
    return 0;
}
```

注意事项（非常关键）：

- 解析器 `try_extract_one_frame()` 里对 `packet_len` + 固定开销 `27` 的计算必须与协议真实布局一致；如果后续协议头结构变化，需要同步调整。
- `PhotoDataHandler` 的 `payload_len = Length - 32 - 8` 是按当前结构体/帧格式推算的常量，如果协议头长度变化，会导致拷贝长度错误；建议后续把“数据区起始偏移/长度”由协议字段计算出来，避免魔数。
- `PhotoEndHandler` 当前按“收到结束包就落盘”，如果允许丢包/乱序，需要在结束包到来时检查 `missing` 并决定是否拒绝落盘/请求重传。

### A.5 recvfile.h：保持接口稳定

文件：[recvfile.h](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/recvfile.h)

```cpp
#ifndef __RECVFILE_H_
#define __RECVFILE_H_
int sFrameResolver(unsigned char* pBuffer, int Length ,int sockfd);
extern "C" void *HandleClient(void *arg);
#endif
```

---

## 阶段B：阿里云图片加载卡顿（可落地优化 + 示例修改）

### B.1 最直接的瓶颈：前端每次都 cache-bust

你当前的图片文件名本身已包含时间戳（服务端落盘 `chX_YYYYmmdd_HHMMSS.jpg`），天然唯一，浏览器缓存能显著降低公网重复下载。

已落地的修改文件：

- [index.html](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/bin/web/index.html)

修改点：移除 `?_=` / `?t=` + `Date.now()`，仅在 `onerror` 重试时用 `?v=<Date.now()>` 强制绕过缓存。
  
`` `js
im g .src = '/uploads/' + encodeURIComponent(name);

 img.onerror = function() {
    setTimeout(() => {
    this.src = '/uploads/' + encodeURIComponent(name) + '?v=' + Date.now();
  }, 1000);
};
```

### B.2 服务器侧的实践方案（按优先级）

1）静态资源优先由 Nginx 直出

- 确认 `location /uploads/ { alias ... }` 指向的目录，和服务端图片实际落盘目录一致。
- 当前服务端落盘路径在 [recvfile.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L381-L397)（`exe_dir + "/web/uploads"`），Nginx 的 alias 需要对齐。

2）缓存策略（不靠 Date.now，而靠版本号/文件名）

- 历史图片：使用唯一文件名（你现在已做到）即可，让浏览器与 CDN/Nginx 自然缓存。
- 若未来存在“同名覆盖”：后端返回 `mtime`/版本号，前端拼 `?v=<mtime>`，只在版本变化时更新。

3）公网体验：缩略图与分辨率控制

- 列表用缩略图（例如 320px 宽），点击再加载原图，能把总下载量从“多张大图”变成“多张小图 + 少量大图”。
- 如果要做得更彻底：上传后在服务端异步生成缩略图，Nginx 直接提供 `/thumbs/...`。

---

## 验证清单（建议你部署到阿里云前先本地验证）

- 传图过程中：持续发心跳（0x0aE6）应始终能被正常回复（不再被图片流程吞掉）。
- 连续上传多张图：HTTP 页面缩略图切换时不会每次重新下载历史图片（浏览器 Network 面板命中 disk cache / memory cache）。
- 客户端断开：服务端不应因为 `len==0` 崩溃退出（现在 `HandleClient` 仅退出该连接线程并清理 fd）。

