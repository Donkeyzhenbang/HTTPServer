#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <netinet/in.h>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>

class ServerApp {
public:
    static ServerApp& getInstance();
    void run();

private:
    ServerApp();
    ~ServerApp();

    void initializeSocket();
    void acceptLoop();
    static void* handleClientThread(void* arg);
    void startHttpServer();

    int serverSocket;
    int port;
    struct sockaddr_in serverAddr;
};

#endif // SERVERAPP_H
