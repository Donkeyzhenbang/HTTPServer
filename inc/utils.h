#ifndef __UTILS_H_
#define __UTILS_H_
#include <stdlib.h>
#include <cstdint>
#include <memory>
typedef unsigned char u_int8;
typedef unsigned short u_int16;
typedef unsigned int u_int32;
extern u_int32 GlobalTimeStamp;
// 定义一个全局日志记录器函数，返回一个指向spdlog的共享指针
//CRC校验
unsigned short GetCheckCRC16(unsigned char* pBuffer, int Length);
int SocketConnect(int sockfd, const char* addr, uint16_t port);
int CheckFrameFull(unsigned char* pBuffer, int Length);
int getFramePacketType(unsigned char* pBuffer, u_int8 *pFrameType, u_int8 *pPacketType);
void deBugFrame(unsigned char* pBuffer, int Length);
int SaveFile(const char *filename, unsigned char* pBuffer, size_t length);
void mv_sleep(int time);
char* ComputeBufferMd5(unsigned char* pBuffer, int Length) ;
extern bool GlobalFlag; //存图结束用于退出循环
extern bool STM32Flag;  //服务器端区分不同模式单片机/Orin Nano 1表示STM32单片机 
extern int ChannelNum;
#endif
