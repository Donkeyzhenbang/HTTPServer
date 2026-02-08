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

class ServerApp {
public:
    static ServerApp& getInstance();
    void run();
    
    // Accessors for callbacks
    EventLoop& getEventLoop() { return eventLoop; };
    ThreadPool& getThreadPool() { return threadPool; };

private:
    ServerApp();
    ~ServerApp();

    void initializeSocket();
    void acceptLoop();
    void handleNewConnection(int connfd);

    int serverSocket;
    int port;
    struct sockaddr_in serverAddr;
    
    EventLoop eventLoop;
    ThreadPool threadPool;
};


#endif // SERVERAPP_H
