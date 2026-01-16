#ifndef __SENDFILE_H_
#define __SENDFILE_H_
#include "utils.h"
int SendProtocolB342(int socket);
int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen);
int SendProtocolB37(int socket, unsigned char * pBuffer, int Length, u_int8 channelNo);
int SendPhotoData(int SocketFd,unsigned char* pBuffer, int Length, int channelNo);
void SendImageAnalysis(int sockfd);   //入口参数struct 
#endif // !1
