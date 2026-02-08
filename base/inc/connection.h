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
#include <thread>
#include <mutex>

class MyQueue{
    std::queue<unsigned char> que;
    std::mutex mtx;
public:
    MyQueue() {}
    ~MyQueue() {}
    size_t size(void) {
        std::lock_guard<std::mutex> lock(mtx);
        return que.size();
    }
    void push(const unsigned char &__x) {
        std::lock_guard<std::mutex> lock(mtx);
        que.push(__x);
    }
    void pop() {
        std::lock_guard<std::mutex> lock(mtx);
        que.pop();
    }
    unsigned char front() {
        std::lock_guard<std::mutex> lock(mtx);
        return que.front();
    }
};

/**
 * @brief 连接上下文，包含每个连接的队列和线程信息
 * 
 */
struct ConnectionContext {
    std::unique_ptr<MyQueue> queue;
    std::thread read_thread;
    int connfd;
    char device_id[18];  // 设备ID，17位编码，初始为空
    std::atomic<bool> is_connection_alive;
    std::atomic<bool> is_processing_done;

    // File Transfer State (Moving from local variables in RecvFileHandler)
    enum TransferState {
        IDLE,
        RECEIVING
    };
    TransferState transferState = IDLE;
    unsigned char* pPhotoBuffer = nullptr; // Buffer for the current file
    int packetLen = 0;                     // Total packets expected
    int channelNo = 0;                     // Current channel
    int PhotoFileSize = 0;                 // Bytes received
    int maxOffset = 0;                     // Max offset written
    std::vector<bool> recvStatus;          // Packet received status

    // Reactor Buffer
    std::vector<uint8_t> inputBuffer;
    std::mutex bufferMutex;
    std::atomic<bool> is_processing;       // Is a worker thread currently processing this context?

    ConnectionContext(int fd) : connfd(fd), is_connection_alive(true), is_processing_done(false), is_processing(false) {
        queue = std::make_unique<MyQueue>();
        std::memset(device_id, 0, sizeof(device_id));
        pPhotoBuffer = nullptr;
        inputBuffer.reserve(4096);
    }

    ~ConnectionContext() {
        if(pPhotoBuffer) {
            free(pPhotoBuffer);
            pPhotoBuffer = nullptr;
        }
        if(read_thread.joinable()) {
            read_thread.detach(); // or join if we want
        }
    }
    
    // 禁止拷贝和赋值，因为包含unique_ptr
    ConnectionContext(const ConnectionContext&) = delete;
    ConnectionContext& operator=(const ConnectionContext&) = delete;

    void setDeviceId(const char* id) {
        if (id) {
            std::strncpy(device_id, id, sizeof(device_id) - 1);
            device_id[sizeof(device_id) - 1] = '\0';
        }
    }

    std::string getDeviceId() const {
        return std::string(device_id);
    }

    bool hasDeviceId() const {
        return device_id[0] != '\0';
    }
};

// 全局连接管理器，使用文件描述符作为key
extern std::unordered_map<int, std::shared_ptr<ConnectionContext>> connection_manager;
extern std::mutex connection_manager_mutex;

std::unordered_map<std::string, int> get_device_map();
std::vector<std::string> get_all_device_ids();
ConnectionContext* find_connection_by_device_id(const std::string& device_id);
ConnectionContext* find_connection_by_fd(int fd);
std::shared_ptr<ConnectionContext> get_connection_shared_ptr(int fd);

ConnectionContext* create_connection_context(int fd);
void remove_connection_context(int fd);

size_t get_connection_count();
std::vector<std::pair<int, std::string>> get_all_connections();
