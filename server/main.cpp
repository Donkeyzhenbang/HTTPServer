// main.cpp (fixed) - uses pthread and the old-style HandleClient(void*)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unordered_map>
#include <mutex>
#include "recvfile.h"
#include "utils.h"
#include "http_server.h"


#define SERVER_PORT 52487
#define MCU_PORT 1037

std::unordered_map<std::string, int> g_device_map;
std::mutex g_device_mtx;

// Ensure we declare the C-linkage signature to match implementation
#ifdef __cplusplus
extern "C" {
#endif
void *HandleClient(void *arg);
#ifdef __cplusplus
}
#endif

void initialize_server(int* sockfd, struct sockaddr_in* server_addr, int port)
{
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr->sin_port = htons(port);

    if (bind(*sockfd, (struct sockaddr*)server_addr, sizeof(*server_addr)) < 0) {
        perror("bind error");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(*sockfd, 50) < 0) {
        perror("listen error");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    printf("服务器已启动，监听端口 %d，等待客户端连接...\n", port);
}

// thread wrapper: simply forward arg to HandleClient (do not free arg here)
static void* thread_start(void* arg) {
    return HandleClient(arg); // HandleClient will free(arg) internally
}

void* wait_for_client(void* sockfd_ptr) 
{
    int sockfd = *(int*)sockfd_ptr;
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    char ip_str[INET_ADDRSTRLEN] = {0};

    while (1) {
        int* connfd = (int*)malloc(sizeof(int));
        if (!connfd) {
            perror("malloc failed");
            continue;
        }

        *connfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
        if (*connfd < 0) {
            perror("accept error");
            free(connfd);
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        printf("客户端连接: IP地址: %s; 端口号: %d; fd=%d\n", ip_str, ntohs(client_addr.sin_port), *connfd);

        pthread_t tid;
        int rc = pthread_create(&tid, NULL, thread_start, (void*)connfd);
        if (rc != 0) {
            perror("pthread_create error");
            close(*connfd);
            free(connfd);
            continue;
        }
        pthread_detach(tid);
    }
    return (void*)0;
}

static void* http_thread(void*) {
    start_http_server();
    return nullptr;
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr = {};
    initialize_server(&sockfd, &server_addr, SERVER_PORT);


    pthread_t tid1;
    if (pthread_create(&tid1, NULL, wait_for_client, (void*)&sockfd) != 0) {
        perror("pthread_create wait_for_client failed");
        close(sockfd);
        return 1;
    }

    pthread_t http_tid;
    pthread_create(&http_tid, NULL, http_thread, NULL);

    pthread_join(tid1, NULL);

    close(sockfd);
    return 0;
}