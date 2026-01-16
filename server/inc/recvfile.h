#ifndef __RECVFILE_H_
#define __RECVFILE_H_
#include <string>
#include <functional>
#include <queue>
#include <memory>
// #include "connection.h"
// 全局连接管理器，使用文件描述符作为key
// extern std::unordered_map<int, std::unique_ptr<ConnectionContext>> connection_manager;

typedef struct Packet_t {
    int packetLength;
    unsigned char packetBuffer[2048];
}Packet_t;

int recv_and_resolve(int sockfd, Packet_t* pPacket);
int sFrameResolver(unsigned char* pBuffer, int Length ,int sockfd);
void StartReadThread(int* pSocketId,pthread_t* tId);
extern "C" void *HandleClient(void *arg);
void *HandleMCU(void *arg);
#endif
