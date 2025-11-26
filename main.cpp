#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "recvfile.h"
#include "utils.h"
#define SERVER_PORT     52487    //端口号不能发生冲突,不常用的端口号通常大于5000
#define MCU_PORT 1037



void initialize_server(int* sockfd, struct sockaddr_in* server_addr, int port)
{
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

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

    printf("服务器已启动，等待客户端连接...\n");
    // printf("%s \n Please choose operation: ", menu);
    // scanf("%d",&ChannelNum);
    // printf("输入的通道号为: %d\n", ChannelNum);

}

void* wait_for_client(void* sockfd_ptr) 
{
    int sockfd = *(int*)sockfd_ptr;
    struct sockaddr_in client_addr = {0};
    int addrlen = sizeof(client_addr);
    char ip_str[20] = {0};
    

    while (1) {
        int* connfd = (int*)malloc(sizeof(int));
        *connfd = accept(sockfd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen);

        if (*connfd < 0) {
            perror("accept error");
            free(connfd);
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str));
        printf("客户端连接: IP地址: %s; 端口号: %d\n", ip_str, ntohs(client_addr.sin_port));
        pthread_t tid;
        if (pthread_create(&tid, NULL, HandleClient, (void*)connfd) != 0) {
            perror("pthread_create error");
            close(*connfd);
            free(connfd);
        } else {
            // pthread_detach(tid);
            pthread_join(tid, NULL);
        }
    }
    return (void*)0;
}

void* wait_for_mcu(void* sockfd_ptr)
{
    int sockfd = *(int*)sockfd_ptr;
    struct sockaddr_in client_addr = {0};
    int addrlen = sizeof(client_addr);
    char ip_str[20] = {0};
    

    while (1) {
        int* connfd = (int*)malloc(sizeof(int));
        *connfd = accept(sockfd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen);

        if (*connfd < 0) {
            perror("accept error");
            free(connfd);
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str));
        printf("客户端连接: IP地址: %s; 端口号: %d\n", ip_str, ntohs(client_addr.sin_port));
        pthread_t tid;
        if (pthread_create(&tid, NULL, HandleMCU, (void*)connfd) != 0) {
            perror("pthread_create error");
            close(*connfd);
            free(connfd);
        } else {
            // pthread_detach(tid);
            pthread_join(tid, NULL);
        }
    }
    return (void*)0;
}

int main(void) {
    int sockfd, sockfd_mcu;
    struct sockaddr_in server_addr = {}, server_addr_mcu = {};
    initialize_server(&sockfd, &server_addr, SERVER_PORT);
    initialize_server(&sockfd_mcu, &server_addr_mcu, MCU_PORT);

    // 多线程等待不同端口的客户端连接
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, wait_for_client, (void*)&sockfd);
    pthread_create(&tid2, NULL, wait_for_mcu, (void*)&sockfd_mcu);

    // 主线程等待子线程结束
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    // 关闭套接字
    close(sockfd);
    close(sockfd_mcu);
    return 0;
}