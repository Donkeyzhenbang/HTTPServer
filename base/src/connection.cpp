#include "../inc/connection.h"
#include <algorithm>

// 全局连接管理器，使用文件描述符作为key
std::unordered_map<int, std::shared_ptr<ConnectionContext>> connection_manager;
std::mutex connection_manager_mutex;

std::unordered_map<std::string, int> get_device_map() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    
    std::unordered_map<std::string, int> device_map;
    for (const auto& pair : connection_manager) {
        if (pair.second->hasDeviceId()) {
            device_map[pair.second->getDeviceId()] = pair.first;
        }
    }
    return device_map;
}

std::vector<std::string> get_all_device_ids() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    
    std::vector<std::string> device_ids;
    for (const auto& pair : connection_manager) {
        if (pair.second->hasDeviceId()) {
            device_ids.push_back(pair.second->getDeviceId());
        }
    }
    return device_ids;
}

ConnectionContext* find_connection_by_device_id(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    
    for (const auto& pair : connection_manager) {
        if (pair.second->hasDeviceId() && pair.second->getDeviceId() == device_id) {
            return pair.second.get();
        }
    }
    return nullptr;
}

ConnectionContext* find_connection_by_fd(int fd) {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    
    auto it = connection_manager.find(fd);
    if (it != connection_manager.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::shared_ptr<ConnectionContext> get_connection_shared_ptr(int fd) {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    auto it = connection_manager.find(fd);
    if (it != connection_manager.end()) {
        return it->second;
    }
    return nullptr; 
}

ConnectionContext* create_connection_context(int fd) {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    auto context = std::make_shared<ConnectionContext>(fd);
    auto result = connection_manager.emplace(fd, context);
    return result.first->second.get();
}

void remove_connection_context(int fd) {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    connection_manager.erase(fd);
}

size_t get_connection_count() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    return connection_manager.size();
}

std::vector<std::pair<int, std::string>> get_all_connections() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    std::vector<std::pair<int, std::string>> connections;
    for (const auto& pair : connection_manager) {
        if (pair.second->hasDeviceId()) {
            connections.push_back({pair.first, pair.second->getDeviceId()});
        } else {
             connections.push_back({pair.first, "未注册(FD:" + std::to_string(pair.first) + ")"});
        }
    }
    return connections;
}
