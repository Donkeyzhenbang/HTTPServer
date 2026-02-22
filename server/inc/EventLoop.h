#pragma once

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <memory>

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
            callbacks[fd] = cb;
        }
    }

    void RemoveSocket(int fd) {
        if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            // perror("epoll_ctl del failed"); // Often fails if fd already closed, ignore
        }
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

                // 处理断开连接事件：EPOLLRDHUP(对端关闭) | EPOLLHUP(挂起) | EPOLLERR(错误)
                if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    if (callbacks.find(fd) != callbacks.end()) {
                        callbacks[fd](fd);  // 触发断开回调
                    }
                    continue;
                }

                // 处理可读事件
                if (ev & EPOLLIN) {
                    if (callbacks.find(fd) != callbacks.end()) {
                        callbacks[fd](fd);
                    }
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
};
