#include "../inc/ZkClient.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false) {
}

ZkClient::~ZkClient() {
    if (m_zhandle != nullptr) {
        zookeeper_close(m_zhandle);
    }
}

void ZkClient::watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx)
{
    // Global watcher for session events
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            std::cout << "[ZK] Connected to Zookeeper Server." << std::endl;
            ZkClient* client = (ZkClient*)zoo_get_context(zh);
            if (client) {
                std::unique_lock<std::mutex> lock(client->m_mutex);
                client->m_connected = true;
                client->m_cond.notify_all();
            }
        }
    }
}

void ZkClient::Start(const std::string& host)
{
    m_host = host;
    // Connect async, wait for watcher to set m_connected
    std::cout << "[ZK] Connecting to " << host << "..." << std::endl;
    m_zhandle = zookeeper_init(m_host.c_str(), watcher, 30000, nullptr, this, 0);
    
    if (m_zhandle == nullptr) {
        std::cerr << "[ZK] zookeeper_init failed!" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_connected) {
        // Wait up to 10 seconds for connection
        if (m_cond.wait_for(lock, std::chrono::seconds(10)) == std::cv_status::timeout) {
             std::cerr << "[ZK] Connection timeout!" << std::endl;
             exit(EXIT_FAILURE);
        }
    }
}

void ZkClient::Create(const std::string& path, const std::string& data, int state)
{
    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);
    
    // Check if path exists? Or create blindly?
    // Ephemeral node creation
    int flag = (state == 0) ? 0 : ZOO_EPHEMERAL;
    
    // First, ensure parent path exists (recursively not supported in C API, simplified here)
    // Assume /gw-server and /gw-server/nodes exist or we create them.
    // Hack: For now assume root path exists or is created manually.
    // In production, we'd loop and create parent nodes.

    int ret = zoo_create(m_zhandle, path.c_str(), data.c_str(), data.length(), 
                         &ZOO_OPEN_ACL_UNSAFE, flag, path_buffer, bufferlen);
                         
    if (ret == ZOK) {
        std::cout << "[ZK] Created node: " << path << std::endl;
    } else if (ret == ZNODEEXISTS) {
        std::cout << "[ZK] Node already exists: " << path << std::endl;
    } else {
        std::cerr << "[ZK] Create failed for " << path << ", code: " << ret << std::endl;
    }
}

std::string ZkClient::GetData(const std::string& path)
{
    char buffer[512];
    int bufferlen = sizeof(buffer);
    int ret = zoo_get(m_zhandle, path.c_str(), 0, buffer, &bufferlen, nullptr);
    if (ret != ZOK) {
        std::cerr << "[ZK] GetData failed for " << path << std::endl;
        return "";
    }
    return std::string(buffer, bufferlen);
}

void ZkClient::Close() {
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
}
