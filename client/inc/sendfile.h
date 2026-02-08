#ifndef __SENDFILE_H_
#define __SENDFILE_H_
#include "utils.h"
#include "protocol_handler.h"

// int SendProtocolB342(int socket); // In base
// int SendProtocolB351... // In base
// int SendProtocolB37... // In base

// int SendPhotoData(int SocketFd,unsigned char* pBuffer, int Length, int channelNo); // Moved to Protocol Handler
void SendImageAnalysis(int sockfd);

#endif // !1

