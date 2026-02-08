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

ServerApp::ServerApp() : serverSocket(-1), port(SERVER_PORT), threadPool(4) {
}

ServerApp::~ServerApp() {
    eventLoop.Stop();
    if (serverSocket >= 0) {
        close(serverSocket);
    }
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

void ServerApp::run() {
    initializeSocket();

    // Start HTTP server in a separate thread
    std::thread httpThread([](){
        start_http_server();
    });
    httpThread.detach();

    // Start Event Loop (blocks main thread effectively)
    eventLoop.Run();
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

