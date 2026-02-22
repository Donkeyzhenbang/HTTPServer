#include "../inc/ServerApp.h"
#include "../inc/http_server.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>

// Import HandleClient declaration
// Ideally this should be in a header like recvfile.h, but if it's not, we declare it here.
// The old main.cpp had it declared manually.
extern "C" { // Keeping the assumption it might need C linkage, or just standard linkage if C++
    void *HandleClient(void *arg);
}
// Actually, if recvfile.cpp is C++, we shouldn't force extern "C" unless defined so.
// But since the project builds with main.cpp having it, let's keep it safe or check recvfile.h?
// Code dump of main.cpp had:
/*
#ifdef __cplusplus
extern "C" {
#endif
void *HandleClient(void *arg);
#ifdef __cplusplus
}
#endif
*/

// Let's stick to C++ header if possible.
#include "../inc/recvfile.h"
#include <fcntl.h>

#define SERVER_PORT 52487

ServerApp& ServerApp::getInstance() {
    static ServerApp instance;
    return instance;
}

ServerApp::ServerApp() : serverSocket(-1), port(SERVER_PORT), httpPort(8080), threadPool(4) {
    m_zkClient = std::make_unique<ZkClient>();
    m_redisClient = std::make_unique<RedisClient>();
    m_localIp = "127.0.0.1";

    m_zkHost = "";
    m_redisHost = "";
}

ServerApp::~ServerApp() {
    eventLoop.Stop();
    if (serverSocket >= 0) {
        close(serverSocket);
    }
    // Zk/Redis clients cleaned up by unique_ptr
}

void ServerApp::Init(int p, int hp, const std::string& zkh, const std::string& rh, const std::string& lip) {
    port = p;
    httpPort = hp;
    m_zkHost = zkh;
    m_redisHost = rh;
    m_localIp = lip;
}


void ServerApp::registerToZk() {
    if (m_zkHost.empty()) return;
    
    // Connect to ZK
    m_zkClient->Start(m_zkHost);
    
    // Create base path if not exists (Assume /gw-server exists for now or create recursive)
    // Here we register ephemeral node
    // Path: /gw-server/nodes/node_IP_PORT
    std::string nodePath = "/gw-server/nodes/node_" + m_localIp + "_" + std::to_string(port);
    std::string nodeData = "{\"ip\":\"" + m_localIp + "\",\"port\":" + std::to_string(port) + ",\"http_port\":" + std::to_string(httpPort) + "}";
    
    m_zkClient->Create(nodePath, nodeData, 1); // 1 = Ephemeral
    std::cout << "[ServerApp] Registered to ZK: " << nodePath << std::endl;
}

void ServerApp::run() {
    // 1. Initialize Distributed Components
    if (!m_redisHost.empty()) {
        std::string host = m_redisHost;
        int rport = 6379;
        size_t colon = host.find(':');
        if (colon != std::string::npos) {
            rport = std::stoi(host.substr(colon + 1));
            host = host.substr(0, colon);
        }
        if (m_redisClient->Connect(host, rport)) {
            std::cout << "[ServerApp] Connected to Redis at " << m_redisHost << std::endl;
        }
    }

    if (!m_zkHost.empty()) {
        registerToZk();
    }

    initializeSocket();

    // Start HTTP server in a separate thread
    std::thread httpThread([this](){
        start_http_server(this->httpPort);
    });
    httpThread.detach();

    // Start Event Loop (blocks main thread effectively)
    eventLoop.Run();
}

void ServerApp::initializeSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set Non-Blocking
    int flags = fcntl(serverSocket, F_GETFL, 0);
    fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind error");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, 50) < 0) {
        perror("listen error");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "服务器已启动，监听端口 " << port << "，等待客户端连接...\n";
    
    // Check if recvfile.cpp functions are available
    // We need to use a lambda to bind the instance method or static wrapper
    eventLoop.AddSocket(serverSocket, EPOLLIN | EPOLLET, [this](int fd){
        this->acceptLoop();
    });
}


void ServerApp::acceptLoop() {
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    char ip_str[INET_ADDRSTRLEN] = {0};

    while (true) {
        int connfd = accept(serverSocket, (struct sockaddr*)&client_addr, &addrlen);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; 
            }
            perror("accept error");
            break;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "客户端连接: IP地址: " << ip_str 
                  << "; 端口号: " << ntohs(client_addr.sin_port) 
                  << "; fd=" << connfd << std::endl;

        handleNewConnection(connfd);
    }
}

void ServerApp::handleNewConnection(int connfd) {
    // Set Non-Blocking
    int flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);

    // Initialise Context
    create_connection_context(connfd);

    // Add to Reactor
    eventLoop.AddSocket(connfd, EPOLLIN | EPOLLET | EPOLLRDHUP, [](int fd){
        OnClientRead(fd);
    });
    
    // Send immediate heartbeat response if needed or wait for client?
    // Old code waited for HeartbeatFrame.
    // We just let `OnClientRead` handle the incoming packet.
}

