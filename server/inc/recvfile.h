#ifndef __RECVFILE_H_
#define __RECVFILE_H_
#include <string>
#include <functional>
#include <queue>
#include <memory>
// Use base protocol definitions
#include "protocol.h"
#include "protocol_handler.h"

int recv_and_resolve(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive);
int ServerFrameResolver(unsigned char* pBuffer, int Length ,int sockfd);

// New Reactor Interface
void OnClientRead(int fd); // Output from IO Loop
void ProcessConnection(int fd); // Task for Worker Pool

// Legacy (Deprecated but kept if needed for linking transitional code)
extern "C" void *HandleClient(void *arg);
void *HandleMCU(void *arg);

#endif
