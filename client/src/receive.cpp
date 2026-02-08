#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "sendfile.h"
#include "utils.h"
#include "receive.h"
#include "protocol_handler.h"

// Helper around generic wait
int waitForFrame(int fd, u_int8 fType, u_int8 pType, const char* name, void* out = nullptr, int outSize = 0) {
    int ret = WaitForProtocol(fd, fType, pType, out, outSize);
    if(ret == 0) {
        printf("接收到%s协议\n", name);
        return 0;
    }
    printf("等待%s协议失败\n", name);
    return -1;
}

int waitForB341(int fd) {
    ProtocolB341 frame;
    // B341: Request Upload (0x07, 0xEE)
    if(waitForFrame(fd, 0x07, 0xEE, "B341", &frame, sizeof(frame)) == 0)
        return frame.channelNo;
    return -1;
}

int waitForB351(int fd) {
    return waitForFrame(fd, 0x05, 0xEF, "B351");
}

int waitForB352(int fd) {
    return waitForFrame(fd, 0x06, 0xEF, "B352");
}

int waitForB38(int fd) {
    return waitForFrame(fd, 0x06, 0xF2, "B38");
}

int AutoGetPhotoHander(const char* filename, int channelNo, int SocketFd)
{
    printf("进入自动抓拍程序\n");
    int ImgId = open(filename,O_RDONLY);
    if(-1 == ImgId) {
        perror("open error");
        return -1;
    }
    struct stat sbuf;
    if (fstat(ImgId,&sbuf) < 0) {
        close(ImgId);
        return -1;
    }
    
    void* ImgAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    if (ImgAddr == MAP_FAILED) {
        close(ImgId);
        return -1;
    }
    
    int packetLen = sbuf.st_size / 1024 + 1;
    printf("发送通道号%d, 总包数%d, mmap读取图片大小为%zu\n",channelNo, packetLen, sbuf.st_size);
    
    SendProtocolB351(SocketFd,channelNo, packetLen);
    printf("已发送B351 \n");
    
    waitForB352(SocketFd); // Wait for Ack

    printf("开始进行传图 \n");
    SendPhotoData(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,channelNo);
    
    sleep(2); 
    SendProtocolB37(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,channelNo);
    printf("发送B37结束 \n");
    
    munmap(ImgAddr,sbuf.st_size);
    close(ImgId);
    
    printf("图像传输完毕\n");  
    waitForB38(SocketFd); // End flow
    printf("收到B38协议 \n");
    std::cout << "当前通道号为 " << channelNo << std::endl;

    if (channelNo == 4)
        SendImageAnalysis(SocketFd);

    return 0;
}

static int ManualGetPhotoHander(unsigned char* pBuffer, int Length,int SocketFd)
{
    printf("收到B341协议\n");
    // Verify length safely
    if (Length < (int)sizeof(ProtocolB341)) {
        printf("B341 too short\n");
        return -1;
    }
    struct ProtocolB341 *pB341 = (struct ProtocolB341 *)pBuffer;
    
    printf("进入手动抓拍程序\n");
    printf("要获取的通道号为 %d\n",pB341->channelNo);
    
    SendProtocolB342(SocketFd);
    
    int ImgId = open("image2.jpg",O_RDONLY);
    if(-1 == ImgId) {
        perror("open error");
        return -1;
    }
    struct stat sbuf;
    fstat(ImgId,&sbuf);
    void* ImgAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    int packetLen = sbuf.st_size / 1024 + 1;
    
    SendProtocolB351(SocketFd,pB341->channelNo, packetLen);
    waitForB352(SocketFd);
    SendPhotoData(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,pB341->channelNo);
    sleep(2); 
    SendProtocolB37(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size, pB341->channelNo);
    
    munmap(ImgAddr,sbuf.st_size);
    close(ImgId);
    printf("图像传输完毕\n");   
    return 0;
}

// Handler table for async/threaded mode if used
struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd);
};

struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, ManualGetPhotoHander}, 
    {.frameType = 0x06, .packetType = 0xEF, NULL}, 
    {.frameType = 0x07, .packetType = 0xF2, NULL} 
};

// Simplified FrameResolver just dispatching
int FrameResolver(unsigned char* pBuffer, int Length ,int sockfd)
{
    int ret;
    if((ret = CheckFrameFull(pBuffer, Length))<0) {
        // printf("FrameResolver Check Error %d\n",ret);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    
    for(size_t i=0;i<sizeof(Handlers)/sizeof(HandlerFun);i++) {
        if(Handlers[i].frameType == frameType && Handlers[i].packetType == packetType) {   
            if(Handlers[i].func!=NULL) {
                Handlers[i].func(pBuffer,Length,sockfd);
            }
            return 0;
        }
    }
    return -1;
}


