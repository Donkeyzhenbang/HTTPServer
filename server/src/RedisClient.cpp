#include <iostream>
#include <cstring>
#include <mutex>

#include "../inc/RedisClient.h"

// Check if hiredis headers exist, if not, stub out.
// But as we modified CMakeLists based on find_package, we assume it exists.

RedisClient::RedisClient() : m_context(nullptr), m_port(6379), m_host("127.0.0.1") {
}

RedisClient::~RedisClient() {
    Disconnect();
}

void RedisClient::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_context) {
        redisFree(m_context);
        m_context = nullptr;
    }
}

bool RedisClient::Connect(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_host = host;
    m_port = port;
    if (m_context) {
        redisFree(m_context);
    }
    
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    m_context = redisConnectWithTimeout(host.c_str(), port, timeout);
    
    if (m_context == nullptr || m_context->err) {
        if (m_context) {
            std::cerr << "[Redis] Connection error: " << m_context->errstr << std::endl;
            redisFree(m_context);
            m_context = nullptr;
        } else {
            std::cerr << "[Redis] Connection error: can't allocate redis context" << std::endl;
        }
        return false;
    }
    return true;
}

bool RedisClient::Set(const std::string& key, const std::string& value, int expirySeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_context) return false;
    
    redisReply *reply;
    if (expirySeconds > 0) {
        reply = (redisReply*)redisCommand(m_context, "SET %s %s EX %d", key.c_str(), value.c_str(), expirySeconds);
    } else {
        reply = (redisReply*)redisCommand(m_context, "SET %s %s", key.c_str(), value.c_str());
    }

    if (reply == nullptr) return false;
    
    // Check reply->type == REDIS_REPLY_STATUS
    freeReplyObject(reply);
    return true;
}

std::string RedisClient::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_context) return "";

    redisReply *reply = (redisReply*)redisCommand(m_context, "GET %s", key.c_str());
    if (reply == nullptr) return "";

    std::string result = "";
    if (reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
    }
    freeReplyObject(reply);
    return result;
}

bool RedisClient::Del(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_context) return false;

    redisReply *reply = (redisReply*)redisCommand(m_context, "DEL %s", key.c_str());
    if (reply == nullptr) return false;

    freeReplyObject(reply);
    return true;
}
