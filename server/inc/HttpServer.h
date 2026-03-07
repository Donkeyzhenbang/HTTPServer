#pragma once

#include "HttpParser.h"
#include "EventLoop.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>

namespace gw {

// HTTP处理器类型
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

// HTTP连接上下文
class HttpConnectionContext : public std::enable_shared_from_this<HttpConnectionContext> {
public:
    int fd = -1;
    HttpParser parser;
    std::string read_buffer;
    std::string write_buffer;
    bool keep_alive = false;
    bool is_writing = false;

    // 缓冲区读写锁
    std::mutex buffer_mutex;

    // 请求和响应
    HttpRequest request;
    HttpResponse response;

    // 所属服务器
    class HttpServer* server = nullptr;

    HttpConnectionContext(int fd_, class HttpServer* server_)
        : fd(fd_), server(server_) {
        // 默认关闭keep-alive，等解析完header再决定
        keep_alive = false;
    }

    ~HttpConnectionContext() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

// HTTP服务器
class HttpServer {
public:
    HttpServer(EventLoop* event_loop, int port);
    ~HttpServer();

    // 禁止拷贝
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // 注册路由
    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void Put(const std::string& path, HttpHandler handler);
    void Delete(const std::string& path, HttpHandler handler);

    // 启动服务器
    bool Start();

    // 处理连接
    void HandleAccept();
    void HandleRead(int fd);
    void HandleWrite(int fd);
    void HandleClose(int fd);

    // 静态文件目录
    void SetStaticDir(const std::string& dir) { static_dir_ = dir; }
    void AddMountPoint(const std::string& mount, const std::string& dir);

    // 设置错误响应
    void SetErrorHandler(HttpHandler handler) { error_handler_ = handler; }

    // 关闭
    void Stop() { running_ = false; }

private:
    EventLoop* event_loop_;
    int port_;
    int listen_fd_;
    bool running_ = false;
    std::string static_dir_;

    // 路由表
    // method -> path -> handlers (支持多个handler)
    std::vector<std::unordered_map<std::string, HttpHandler>> routers_;

    // 错误处理
    HttpHandler error_handler_;

    // 静态文件目录映射
    std::unordered_map<std::string, std::string> mount_points_;

    // 连接管理
    std::unordered_map<int, std::shared_ptr<HttpConnectionContext>> connections_;
    std::mutex connections_mutex_;  // 保护connections_的互斥锁

    // 解析URI并查找处理器
    HttpHandler FindHandler(const HttpRequest& req);

    // 处理请求
    void ProcessRequest(std::shared_ptr<HttpConnectionContext> ctx);

    // 发送响应
    void SendResponse(std::shared_ptr<HttpConnectionContext> ctx);

    // 静态文件处理
    bool ServeStaticFile(const HttpRequest& req, HttpResponse& res);

    // 接受新连接
    void AcceptConnection();

    // 设置socket选项
    static void SetSocketOptions(int fd);
};

} // namespace gw
