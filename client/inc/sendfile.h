#ifndef __SENDFILE_H_
#define __SENDFILE_H_
#include "utils.h"
int SendProtocolB342(int socket);
int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen);
int SendProtocolB37(int socket, unsigned char * pBuffer, int Length, u_int8 channelNo);
int SendPhotoData(int SocketFd,unsigned char* pBuffer, int Length, int channelNo);
void SendImageAnalysis(int sockfd);   //入口参数struct 

struct __attribute__((packed)) ProtocolPhotoData{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 channelNo;
    u_int16 packetNo; // 总包数
    u_int16 subpacketNo; // 子包包号
    int prefix_sample[2];
    char sample[1024]; //!数据区暂定 后面修改  

    u_int16 CRC16;
    u_int8 End;
};
#endif // !1
