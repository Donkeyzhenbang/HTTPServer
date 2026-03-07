#pragma once

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <mutex>

class EventLoop {
public:
    using EventCallback = std::function<void(int fd)>;

    EventLoop() : epollFd(-1), running(false) {
        epollFd = epoll_create1(0);
        if (epollFd < 0) {
            perror("epoll_create1 failed");
            exit(EXIT_FAILURE);
        }
    }

    ~EventLoop() {
        if (epollFd >= 0) {
            close(epollFd);
        }
    }

    void AddSocket(int fd, uint32_t events, EventCallback cb) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl add failed");
        } else {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            callbacks[fd] = cb;
        }
    }

    void RemoveSocket(int fd) {
        if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            // perror("epoll_ctl del failed"); // Often fails if fd already closed, ignore
        }
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks.erase(fd);
    }

    void Run() {
        running = true;
        std::vector<struct epoll_event> events(MaxEvents);

        while (running) {
            int nfds = epoll_wait(epollFd, events.data(), MaxEvents, -1);
            if (nfds < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait failed");
                break;
            }

            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;

                // 复制callback，避免长时间持有锁
                EventCallback callback;
                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);
                    auto it = callbacks.find(fd);
                    if (it == callbacks.end()) {
                        continue;
                    }
                    callback = it->second;
                }

                // 处理断开连接事件：EPOLLRDHUP(对端关闭) | EPOLLHUP(挂起) | EPOLLERR(错误)
                if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    callback(fd);  // 触发断开回调
                    continue;
                }

                // 处理可读事件
                if (ev & EPOLLIN) {
                    callback(fd);
                }
            }
        }
    }

    void Stop() {
        running = false;
    }

private:
    int epollFd;
    bool running;
    static const int MaxEvents = 1000;
    std::unordered_map<int, EventCallback> callbacks;
    std::mutex callbacks_mutex_;
};
