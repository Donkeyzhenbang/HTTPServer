// connection_manager.cpp
#include "connection.h"
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <iostream>

// 全局连接管理器，使用文件描述符作为key
std::unordered_map<int, std::unique_ptr<ConnectionContext>> connection_manager;
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

size_t get_connection_count() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    return connection_manager.size();
}

std::vector<std::pair<int, std::string>> get_all_connections() {
    std::lock_guard<std::mutex> lock(connection_manager_mutex);
    
    std::vector<std::pair<int, std::string>> connections;
    connections.reserve(connection_manager.size());
    
    for (const auto& pair : connection_manager) {
        std::string device_info = pair.second->hasDeviceId() 
            ? pair.second->getDeviceId() 
            : "未注册设备 (fd=" + std::to_string(pair.first) + ")";
        connections.emplace_back(pair.first, device_info);
    }
    
    // 按设备ID排序，有设备ID的排前面
    std::sort(connections.begin(), connections.end(), 
        [](const auto& a, const auto& b) {
            // 如果都有设备ID，按字母排序
            if (a.second.find("未注册") == std::string::npos && 
                b.second.find("未注册") == std::string::npos) {
                return a.second < b.second;
            }
            // 有设备ID的排前面
            if (a.second.find("未注册") == std::string::npos) return true;
            if (b.second.find("未注册") == std::string::npos) return false;
            // 都是未注册设备，按fd排序
            return a.first < b.first;
        });
    
    return connections;
}