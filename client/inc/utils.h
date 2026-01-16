#ifndef __UTILS_H_
#define __UTILS_H_
#include <cstdint>
#include <functional>
typedef unsigned char u_int8;
typedef unsigned short u_int16;
typedef unsigned int u_int32;

extern u_int32 GlobalTimeStamp;

int SocketConnect(int sockfd, const char* addr, uint16_t port);
unsigned short GetCheckCRC16(unsigned char* pBuffer, int Length);
void deBugFrame(unsigned char* pBuffer, int Length);
int CheckFrameFull(unsigned char* pBuffer, int Length);
int getFramePacketType(unsigned char* pBuffer, u_int8 *pFrameType, u_int8 *pPacketType);
char* ComputeBufferMd5(unsigned char* pBuffer, int Length);
void measure_time_func(std::function<void(void)> func,const char* Information);
void mv_sleep(int time);
void get_local_time();
#endif
