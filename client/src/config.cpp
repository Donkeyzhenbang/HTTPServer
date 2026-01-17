#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <json.h> // 需要安装 jsoncpp 库

bool Config::load(const std::string& filename) {
    // 清除旧配置
    configMap.clear();
    
    // 根据文件扩展名选择解析器
    size_t dotPos = filename.find_last_of(".");
    if (dotPos != std::string::npos) {
        std::string extension = filename.substr(dotPos + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        if (extension == "json") {
            return parseJsonFile(filename);
        } else if (extension == "ini") {
            return parseIniFile(filename);
        }
    }
    
    std::cerr << "不支持的配置文件格式: " << filename << std::endl;
    return false;
}

bool Config::parseJsonFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << filename << std::endl;
        return false;
    }
    
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    
    if (!Json::parseFromStream(reader, file, &root, &errs)) {
        std::cerr << "解析 JSON 文件失败: " << errs << std::endl;
        return false;
    }
    
    // 递归遍历 JSON 树，展平键名
    std::function<void(const Json::Value&, const std::string&)> traverse;
    traverse = [&](const Json::Value& node, const std::string& prefix) {
        if (node.isObject()) {
            for (const auto& key : node.getMemberNames()) {
                std::string newPrefix = prefix.empty() ? key : prefix + "." + key;
                traverse(node[key], newPrefix);
            }
        } else if (node.isArray()) {
            for (Json::ArrayIndex i = 0; i < node.size(); ++i) {
                std::string newPrefix = prefix + "[" + std::to_string(i) + "]";
                traverse(node[i], newPrefix);
            }
        } else {
            std::string value;
            if (node.isString()) {
                value = node.asString();
            } else if (node.isInt()) {
                value = std::to_string(node.asInt());
            } else if (node.isBool()) {
                value = node.asBool() ? "true" : "false";
            } else if (node.isDouble()) {
                value = std::to_string(node.asDouble());
            }
            configMap[prefix] = value;
        }
    };
    
    traverse(root, "");
    return true;
}

bool Config::parseIniFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
        // 去除注释
        size_t commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        // 去除前后空白
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;
        
        // 检查是否是节
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }
        
        // 解析键值对
        size_t equalsPos = line.find('=');
        if (equalsPos != std::string::npos) {
            std::string key = line.substr(0, equalsPos);
            std::string value = line.substr(equalsPos + 1);
            
            // 去除键值对的前后空白
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // 构建完整键名
            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
            configMap[fullKey] = value;
        }
    }
    
    return true;
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        return it->second;
    }
    return defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes");
    }
    return defaultValue;
}

std::vector<std::string> Config::getStringArray(const std::string& key) {
    std::vector<std::string> result;
    
    // 查找数组元素
    for (const auto& pair : configMap) {
        if (pair.first.find(key + "[") == 0) {
            result.push_back(pair.second);
        }
    }
    
    // 如果没有找到数组元素，尝试直接查找键
    if (result.empty()) {
        auto it = configMap.find(key);
        if (it != configMap.end()) {
            // 假设值是逗号分隔的字符串
            std::string value = it->second;
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ',')) {
                result.push_back(item);
            }
        }
    }
    
    return result;
}

std::string Config::getNetworkIP() {
    return getString("network.default_ip", "127.0.0.1");
}

int Config::getNetworkPort() {
    return getInt("network.default_port", 52487);
}

std::vector<std::string> Config::getPhotoPaths() {
    return getStringArray("photos.paths");
}

bool Config::isStateGridEnvironment(const std::string& localIP) {
    std::string subnet = getString("environment.state_grid_subnet", "10.100.75");
    return (localIP.find(subnet) == 0);
}