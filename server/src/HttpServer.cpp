#include "../inc/HttpServer.h"
#include "../inc/ServerApp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <fstream>

namespace gw {

HttpServer::HttpServer(EventLoop* event_loop, int port)
    : event_loop_(event_loop), port_(port), listen_fd_(-1) {
    // 初始化路由表 (支持7种HTTP方法)
    routers_.resize(7);
}

HttpServer::~HttpServer() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

void HttpServer::Get(const std::string& path, HttpHandler handler) {
    routers_[static_cast<int>(HttpMethod::kGet)][path] = handler;
}

void HttpServer::Post(const std::string& path, HttpHandler handler) {
    routers_[static_cast<int>(HttpMethod::kPost)][path] = handler;
}

void HttpServer::Put(const std::string& path, HttpHandler handler) {
    routers_[static_cast<int>(HttpMethod::kPut)][path] = handler;
}

void HttpServer::Delete(const std::string& path, HttpHandler handler) {
    routers_[static_cast<int>(HttpMethod::kDelete)][path] = handler;
}

void HttpServer::AddMountPoint(const std::string& mount, const std::string& dir) {
    mount_points_[mount] = dir;
}

void HttpServer::SetSocketOptions(int fd) {
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // TCP_NODELAY 禁用Nagle算法，减少延迟
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // 设置超时
    struct timeval tv = {5, 0};  // 5秒超时
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool HttpServer::Start() {
    // 创建监听socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[HttpServer] Failed to create socket" << std::endl;
        return false;
    }

    SetSocketOptions(listen_fd_);

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[HttpServer] Failed to bind port " << port_ << std::endl;
        close(listen_fd_);
        return false;
    }

    // 监听
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        std::cerr << "[HttpServer] Failed to listen" << std::endl;
        close(listen_fd_);
        return false;
    }

    // 设置为非阻塞
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

    // 注册到EventLoop
    event_loop_->AddSocket(listen_fd_, EPOLLIN | EPOLLET, [this](int fd) {
        HandleAccept();
    });

    running_ = true;
    std::cout << "[HttpServer] Started on 0.0.0.0:" << port_ << std::endl;
    return true;
}

void HttpServer::HandleAccept() {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (true) {
        int connfd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addrlen);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 没有更多连接
            }
            std::cerr << "[HttpServer] Accept error: " << strerror(errno) << std::endl;
            break;
        }

        SetSocketOptions(connfd);

        // 设置为非阻塞
        int flags = fcntl(connfd, F_GETFL, 0);
        fcntl(connfd, F_SETFL, flags | O_NONBLOCK);

        // 创建连接上下文
        auto ctx = std::make_shared<HttpConnectionContext>(connfd, this);

        // 注册到EventLoop
        event_loop_->AddSocket(connfd, EPOLLIN | EPOLLRDHUP | EPOLLET,
            [this, connfd](int fd) {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto it = connections_.find(fd);
                if (it != connections_.end()) {
                    if (it->second->is_writing) {
                        HandleWrite(fd);
                    } else {
                        HandleRead(fd);
                    }
                }
            });

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[connfd] = ctx;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "[HttpServer] New connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
    }
}

void HttpServer::HandleRead(int fd) {
    std::shared_ptr<HttpConnectionContext> ctx;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) {
            return;
        }
        ctx = it->second;
    }

    // 读取数据
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;  // 稍后重试
        }
        // 连接关闭或错误
        HandleClose(fd);
        return;
    }

    // 追加到读取缓冲区 (需要加锁)
    {
        std::lock_guard<std::mutex> lock(ctx->buffer_mutex);
        ctx->read_buffer.append(buf, n);

        // 解析HTTP请求
        if (ctx->parser.parse(ctx->read_buffer.data(), ctx->read_buffer.size())) {
            // 解析完成
            ctx->request = ctx->parser.request();
            // 清空缓冲区
            ctx->read_buffer.clear();
        } else if (ctx->parser.is_error()) {
            // 解析错误
            ctx->response.SetStatus(400, "Bad Request");
            ctx->response.SetText("400 Bad Request");
            SendResponse(ctx);
            return;
        }
    }

    // 检查Connection头 (不需要锁，因为已经复制了request)
    if (!ctx->request.uri.empty()) {
        std::string conn = ctx->request.GetHeader("Connection");
        ctx->keep_alive = (conn == "keep-alive" || conn == "Keep-Alive");
        // 处理请求
        ProcessRequest(ctx);
    }
}

void HttpServer::HandleWrite(int fd) {
    std::shared_ptr<HttpConnectionContext> ctx;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) {
            return;
        }
        ctx = it->second;
    }

    std::string data_to_send;
    bool should_close = false;
    bool should_reset = false;

    {
        std::lock_guard<std::mutex> lock(ctx->buffer_mutex);

        if (ctx->write_buffer.empty()) {
            ctx->is_writing = false;
            return;
        }

        ssize_t n = write(fd, ctx->write_buffer.data(), ctx->write_buffer.size());

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // 稍后重试
            }
            HandleClose(fd);
            return;
        }

        ctx->write_buffer.erase(0, n);

        if (ctx->write_buffer.empty()) {
            ctx->is_writing = false;
            should_close = !ctx->keep_alive;
            should_reset = ctx->keep_alive;
        }
    }

    if (should_close) {
        HandleClose(fd);
    } else if (should_reset) {
        ctx->parser.reset();
        ctx->request = HttpRequest();
    }
}

void HttpServer::HandleClose(int fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        event_loop_->RemoveSocket(fd);
        connections_.erase(it);
    }
}

HttpHandler HttpServer::FindHandler(const HttpRequest& req) {
    int method_idx = static_cast<int>(req.method);
    if (method_idx < 0 || method_idx >= static_cast<int>(routers_.size())) {
        return nullptr;
    }

    const auto& router = routers_[method_idx];

    // 精确匹配
    auto it = router.find(req.uri);
    if (it != router.end()) {
        return it->second;
    }

    // 前缀匹配 (对于静态文件)
    for (const auto& mount : mount_points_) {
        if (req.uri.find(mount.first) == 0) {
            // 找到匹配的mount point
            return nullptr;  // 静态文件处理
        }
    }

    return nullptr;
}

void HttpServer::ProcessRequest(std::shared_ptr<HttpConnectionContext> ctx) {
    // 查找处理器
    HttpHandler handler = FindHandler(ctx->request);

    if (handler) {
        // 调用处理器
        handler(ctx->request, ctx->response);
    } else {
        // 尝试处理静态文件
        if (!ServeStaticFile(ctx->request, ctx->response)) {
            // 没有找到处理器
            ctx->response.SetStatus(404, "Not Found");
            ctx->response.SetJson("{\"ok\":false,\"error\":\"not found\"}");
        }
    }

    // 发送响应
    SendResponse(ctx);
}

void HttpServer::SendResponse(std::shared_ptr<HttpConnectionContext> ctx) {
    // 设置Connection头
    if (!ctx->response.headers.count("Connection")) {
        ctx->response.SetHeader("Connection", ctx->keep_alive ? "keep-alive" : "close");
    }

    // 序列化响应
    std::string response_data = ctx->response.ToString();

    bool should_close = false;
    bool should_reset = false;

    // 写入缓冲区并发送 (需要加锁)
    {
        std::lock_guard<std::mutex> lock(ctx->buffer_mutex);
        ctx->write_buffer = response_data;

        // 发送
        ssize_t n = write(ctx->fd, ctx->write_buffer.data(), ctx->write_buffer.size());

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ctx->is_writing = true;
                return;
            }
            HandleClose(ctx->fd);
            return;
        }

        ctx->write_buffer.erase(0, n);

        if (!ctx->write_buffer.empty()) {
            ctx->is_writing = true;
        } else {
            should_close = !ctx->keep_alive;
            should_reset = ctx->keep_alive;
        }
    }

    if (should_close) {
        HandleClose(ctx->fd);
    } else if (should_reset) {
        ctx->parser.reset();
        ctx->request = HttpRequest();
    }
}

bool HttpServer::ServeStaticFile(const HttpRequest& req, HttpResponse& res) {
    // 检查mount points
    for (const auto& mount : mount_points_) {
        if (req.uri.find(mount.first) == 0) {
            // 构建文件路径
            std::string filepath = mount.second;
            std::string uri_path = req.uri.substr(mount.first.size());

            // 防止路径遍历攻击
            if (uri_path.find("..") != std::string::npos) {
                return false;
            }

            // 默认为index.html
            if (uri_path.empty() || uri_path == "/") {
                uri_path = "/index.html";
            }

            filepath += uri_path;

            // 读取文件
            std::ifstream ifs(filepath, std::ios::binary);
            if (!ifs) {
                return false;
            }

            // 获取文件内容
            std::string content((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());

            // 设置响应
            std::string ext = filepath.substr(filepath.rfind('.') + 1);
            res.SetFile(filepath, content);
            return true;
        }
    }

    return false;
}

} // namespace gw
