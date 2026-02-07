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
// If recvfile.h exists, include it.
#include "../inc/recvfile.h"

#define SERVER_PORT 52487

ServerApp& ServerApp::getInstance() {
    static ServerApp instance;
    return instance;
}

ServerApp::ServerApp() : serverSocket(-1), port(SERVER_PORT) {
}

ServerApp::~ServerApp() {
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
}

void* ServerApp::handleClientThread(void* arg) {
    // Forward to the legacy handler
    return HandleClient(arg);
}

void ServerApp::run() {
    initializeSocket();

    // Start HTTP server in a separate thread
    std::thread httpThread([](){
        start_http_server();
    });
    httpThread.detach(); // Or join? Old main used pthread_create and then main loop wait_for_client.
    // Actually old main made wait_for_client run in a thread?
    /*
    pthread_create(&tid1, NULL, wait_for_client, (void*)&sockfd)
    pthread_create(&tid2, NULL, http_thread, NULL);
    pthread_join(tid1, NULL);
    */
    // Wait, old main ran `wait_for_client` in a thread `tid1` and `http_thread` in `tid2`.
    // Then joined `tid1`.
    // So the main thread blocked on `wait_for_client`.
    
    // In my design, `run()` can just be the main loop.
    // I can run HTTP server in a thread, and `acceptLoop` in the main thread.
    
    acceptLoop();
}

void ServerApp::acceptLoop() {
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    char ip_str[INET_ADDRSTRLEN] = {0};

    while (true) {
        int* connfd = (int*)malloc(sizeof(int));
        if (!connfd) {
            perror("malloc failed");
            continue;
        }

        *connfd = accept(serverSocket, (struct sockaddr*)&client_addr, &addrlen);
        if (*connfd < 0) {
            perror("accept error");
            free(connfd);
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "客户端连接: IP地址: " << ip_str 
                  << "; 端口号: " << ntohs(client_addr.sin_port) 
                  << "; fd=" << *connfd << std::endl;

        // Use std::thread instead of pthread for client handler
        // But HandleClient expects void* and manages memory (frees arg).
        // Since we are moving to C++17, we might want to wrap it differently,
        // but to minimize breakage in legacy `HandleClient`, let's keep the void* contract.
        
        std::thread clientThread(handleClientThread, (void*)connfd);
        clientThread.detach();
    }
}
