#pragma once

#include <string>
#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <cstring>


class MyQueue{
    std::queue<unsigned char> que;
    pthread_spinlock_t spin;//定义自旋锁
public:
    MyQueue() {
        pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    }
    ~MyQueue() {
        pthread_spin_destroy(&spin);
    }
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

/**
 * @brief 连接上下文，包含每个连接的队列和线程信息
 * 
 */
struct ConnectionContext {
    std::unique_ptr<MyQueue> queue;
    pthread_t read_thread;
    int connfd;
    char device_id[18];  // 设备ID，17位编码，初始为空

    
    ConnectionContext(int fd) : connfd(fd) {
        queue = std::make_unique<MyQueue>();
    }
    
    ~ConnectionContext() {
        // 确保线程被取消
        if (read_thread) {
            pthread_cancel(read_thread);
        }
    }

    /**
     * @brief 设置设备ID
     */
    void setDeviceId(const char* id) {
        if (id && strlen(id) <= 17) {
            memcpy(device_id, id, 17);
            device_id[17] = '\0';
            std::cout << "[ConnectionContext] 设置设备ID: " << device_id 
                      << " for fd=" << connfd << std::endl;
        }
    }
    
    /**
     * @brief 获取设备ID
     */
    std::string getDeviceId() const {
        return device_id[0] ? std::string(device_id) : "";
    }
    
    /**
     * @brief 检查是否有设备ID
     */
    bool hasDeviceId() const {
        return device_id[0] != '\0';
    }
};

// 全局连接管理器，使用文件描述符作为key
extern std::unordered_map<int, std::unique_ptr<ConnectionContext>> connection_manager;
extern std::mutex connection_manager_mutex;

/**
 * @brief 获取设备ID到fd的映射
 */
std::unordered_map<std::string, int> get_device_map();

/**
 * @brief 获取所有设备ID列表
 */
std::vector<std::string> get_all_device_ids();

/**
 * @brief 根据设备ID查找连接
 */
ConnectionContext* find_connection_by_device_id(const std::string& device_id);

/**
 * @brief 获取连接统计信息
 */
size_t get_connection_count();

/**
 * @brief 获取所有连接的详细信息
 */
std::vector<std::pair<int, std::string>> get_all_connections();

