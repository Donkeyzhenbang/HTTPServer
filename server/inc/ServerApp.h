#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <netinet/in.h>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <memory>
#include "EventLoop.h"
#include "../../base/inc/threadpool.h"
#include "ZkClient.h"
#include "RedisClient.h"

class ServerApp {
public:
    static ServerApp& getInstance();
    
    // Initialize with distributed config
    void Init(int port, int httpPort, const std::string& zkHost, const std::string& redisHost, const std::string& localIp);
    
    void run();
    
    // Accessors for callbacks
    EventLoop& getEventLoop() { return eventLoop; };
    ThreadPool& getThreadPool() { return threadPool; };
    
    RedisClient* GetRedisClient() { return m_redisClient.get(); }
    ZkClient* GetZkClient() { return m_zkClient.get(); }
    std::string GetLocalAddress() const { return m_localIp + ":" + std::to_string(port); }
    int GetHttpPort() const { return httpPort; }

private:
    ServerApp();
    ~ServerApp();

    void initializeSocket();
    void acceptLoop();
    void handleNewConnection(int connfd);
    void registerToZk();

    int serverSocket;
    int port;
    int httpPort;
    std::string m_localIp;

    std::string m_zkHost;
    std::string m_redisHost;
    
    std::unique_ptr<ZkClient> m_zkClient;
    std::unique_ptr<RedisClient> m_redisClient;
    
    struct sockaddr_in serverAddr;
    
    EventLoop eventLoop;
    ThreadPool threadPool;
};



#endif // SERVERAPP_H
