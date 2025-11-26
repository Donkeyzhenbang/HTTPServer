#ifndef __RECVFILE_H_
#define __RECVFILE_H_
#include <string>
#include <functional>
typedef struct Packet_t {
    int packetLength;
    unsigned char packetBuffer[2048];
}Packet_t;
int recv_and_resolve(int sockfd, Packet_t* pPacket);
int sFrameResolver(unsigned char* pBuffer, int Length ,int sockfd);
void StartReadThread(int* pSocketId,pthread_t* tId);
void *HandleClient(void *arg);
void *HandleMCU(void *arg);
#endif
