#include <sys/mman.h>  // 添加mmap相关头文件
#include <sys/stat.h>  // 添加stat相关头文件
#include <fcntl.h>     // 添加文件操作相关头文件
#include <unistd.h>    // 添加sleep函数头文件
#include "utils.h"

// SendModelToDevice
int SendModelToDevice(const char* filename, int channelNo, int SocketFd);

// 声明协议相关函数（假设这些函数在其他文件中定义）
int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen);
// int waitForB352(int fd);
int SendPhotoData(int SocketFd,unsigned char* pBuffer, int Length, int channelNo);
int SendProtocolB37(int socket, unsigned char * pBuffer, int Length, u_int8 channelNo);
// int waitForB38(int fd);