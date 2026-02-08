#include "modelupgrade.h"
#include "utils.h"
#include "../inc/protocol_handler.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdio>

// Helpers using Base WaitForProtocol
static int waitForB352(int fd) {
    // Expected: Type 0x06, Packet 0xEF
    if(WaitForProtocol(fd, 0x06, 0xEF, nullptr, 0) == 0) {
        printf("接收到B352协议\n");
        return 0;
    }
    printf("等待B352协议失败\n");
    return -1;
}

static int waitForB38(int fd) {
    // Expected: Type 0x06, Packet 0xF2
    if(WaitForProtocol(fd, 0x06, 0xF2, nullptr, 0) == 0) {
        printf("接收到B38协议\n");
        return 0;
    }
    printf("等待B38协议失败\n");
    return -1;
}

int SendModelToDevice(const char* filename, int channelNo, int SocketFd)
{
    printf("进入传输模型文件程序\n");
    int ImgId = open(filename,O_RDONLY);
    if(-1 == ImgId) {
        perror("open error");
        return -1;
    }
    struct stat sbuf;
    if (fstat(ImgId,&sbuf) < 0) { close(ImgId); return -1; }
    
    void* ModelAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    if (ModelAddr == MAP_FAILED) { close(ImgId); return -1; }
    
    int packetLen = sbuf.st_size / 1024 + 1;
    printf("发送通道号%d, 总包数%d, mmap读取图片大小为%ld\n",channelNo, packetLen, sbuf.st_size);
    
    SendProtocolB351(SocketFd, channelNo, packetLen);
    printf("已发送B351 \n");
    
    waitForB352(SocketFd);
    
    printf("开始进行传图 \n");
    SendPhotoData(SocketFd,(unsigned char *)ModelAddr,sbuf.st_size,channelNo);
    
    sleep(2); 
    
    SendProtocolB37(SocketFd,(unsigned char *)ModelAddr,sbuf.st_size,channelNo);
    
    printf("发送B37结束 \n");
    munmap(ModelAddr,sbuf.st_size);
    close(ImgId);
    printf("图像传输完毕\n");  
    waitForB38(SocketFd);
    printf("收到B38协议 \n");
    std::cout << "当前通道号为 " << channelNo << std::endl;
    return 0;
}
