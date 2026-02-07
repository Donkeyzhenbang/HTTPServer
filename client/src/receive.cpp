#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>   //用于文件状态和fstat函数
#include <fcntl.h>      //用于文件控制操作（如open）
#include <sys/mman.h>   //mmap
#include "sendfile.h"
#include "utils.h"
#include "receive.h"
static unsigned char buffer[1024] = {};//!不需要清空缓冲区吗
static int SampleHander(unsigned char* pBuffer, int Length, int fd);
static int ManualGetPhotoHander(unsigned char* pBuffer, int Length, int fd);

/**
 * @brief 手动请求拍照
 * 
 */
struct __attribute__((packed)) ProtocolB341{
    u_int16 sync;        //报文头：5AA5H
    u_int16 packetLength;//报文长度：38字节，等于sizeof(ProtocolB341)

    char cmdId[17];      //CMD_ID,17位编码
    u_int8 frameType;    //帧类型，07H（远程图像控制报）
    u_int8 packetType;   //报文类型，EEH（手动请求拍摄照片/短视频报）
    u_int8 frameNo;      //帧序列号，请求数据报的帧号字节最高位为0
    u_int8 channelNo;    //通道号
    char reserve[10]={0}; //内容与标识，第一字节为数据类型，0 为图片，1 为短视频；其余字节备用

    u_int16 CRC16;         //CRC校验
    u_int8 End;          //报文尾
};

/**
 * @brief 监拍装置请求上送照片报文的响应报文格式
 * 
 */
// struct __attribute__((packed)) ProtocolB352{ 
//     u_int16 sync;        //报文头：5AA5H
//     u_int16 packetLength;//报文长度：28字节
//     char cmdId[17];      //CMD_ID,17位编码
//     u_int8 frameType;    //帧类型，06H（远程图像数据响应报）
//     u_int8 packetType;   //报文类型，EFH（拍摄装置请求上送）
//     u_int8 frameNo;      //帧序列号
//     u_int8 uploadStatus; //FFH：允许；00H：不允许

//     u_int16 CRC16;       //CRC16校验
//     u_int8 End;          //报文尾
// };

/**
 * @brief 远程图像补包数据下发数据报文B38协议
 * 
 */
struct __attribute__((packed)) ProtocolB38{
    u_int16 sync;        //报文头：5AA5H
    u_int16 packetLength;//报文长度：38字节

    char cmdId[17];      //CMD_ID,17位编码
    u_int8 frameType;    //帧类型，07H（远程图像数据响应报）
    u_int8 packetType;   //报文类型，F2H（远程图像补包数据下发报）
    u_int8 frameNo;      //帧序列号，返回请求的80H
    u_int8 channelNo;    //通道号，1或2
    u_int16 complementPackSum;//补包包数
    char reserve[8]={0};//前4字节，内容ID；后4字节备用

    u_int16 CRC16;       //CRC16校验
    u_int8 End;          //报文尾
};

/**
 * @brief 回调函数结构体
 * 
 */
struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd);
};

/**
 * @brief 回调函数注册表
 * 
 */
struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, ManualGetPhotoHander},  //ProtocolB341的处理函数，从这里开始手动抓拍图像流程
    {.frameType = 0x06, .packetType = 0xEF, NULL},
    {.frameType = 0x07, .packetType = 0xF2, NULL}
};


// int waitForHeartBeat(int fd) moved to base library

/**
 * @brief 等待接收B341协议
 * 
 * @param fd 
 * @return int 
 */


int waitForB341(int fd) 
{
    int len = read(fd, buffer, 1024);
    if(len < 0) {
        printf("B341 Socket Read出错\n");
        return -1;
    }
    
    int ret;
    if((ret = CheckFrameFull(buffer, len)) < 0) {
        printf("帧解析出错，不完整，错误码%d\n", ret);
        deBugFrame(buffer, len);
        return -1;
    }
    
    u_int8 frameType, packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    
    if(frameType == 0x07 && packetType == 0xEE) {
        printf("接收到B341协议\n");
        deBugFrame(buffer, len);
        
        // 提取通道号 - 根据B341报文格式
        // 计算偏移量：
        // Sync(2) + Packet_Length(2) + CMD_ID(17) + Frame_Type(1) + Packet_Type(1) + Frame_No(1) = 24字节
        // Channel_No是第25个字节（从1开始计数），索引为24（从0开始）
        
        if(len >= 25) {  // 确保报文足够长
            u_int8 channelNo = buffer[24];  // 第25个字节是通道号
            printf("B341报文中的通道号: %d\n", channelNo);
            
            // 如果需要，这里可以保存通道号到全局变量或返回
            // global_channel = channelNo;  // 假设有全局变量
            
            // 返回通道号作为成功（正整数）或0
            return (int)channelNo;
        } else {
            printf("B341报文长度不足，无法提取通道号\n");
            return -3;
        }
    }
    
    printf("收到其他包，没有收到B341\n");
    return -2;
}

int waitForB351(int fd) 
{
    int len = read(fd,buffer,1024);
    if(len < 0) {
        //出错
        printf("B352 Socket Read出错 %d \n", len);
        return -1;
    }
    int ret;
    if((ret = CheckFrameFull(buffer, len))<0) {
        printf("帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(buffer,len);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    printf("frameType : 0x%x, packetType : 0x%x \n", frameType, packetType);
    if(frameType == 0x05 && packetType == 0xEF) {
        printf("接收到B351协议\n");
        // deBugFrame(buffer,len);
        return 0;
    }
    printf("收到其他包，没有收到B352\n");
    return -2;
}

/**
 * @brief 等待B352协议包
 * 
 * @param fd 
 * @return int 
 */
int waitForB352(int fd) 
{
    int len = read(fd,buffer,1024);
    if(len < 0) {
        //出错
        printf("B352 Socket Read出错 %d \n", len);
        return -1;
    }
    int ret;
    if((ret = CheckFrameFull(buffer, len))<0) {
        printf("帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(buffer,len);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    printf("frameType : 0x%x, packetType : 0x%x \n", frameType, packetType);
    if(frameType == 0x06 && packetType == 0xEF) {
        printf("接收到B352协议\n");
        // deBugFrame(buffer,len);
        return 0;
    }
    printf("收到其他包，没有收到B352\n");
    return -2;
}

int waitForB38(int fd) 
{
    int len = read(fd,buffer,1024);
    if(len < 0) {
        //出错
        printf("B38 Socket Read出错 %d \n", len);
        return -1;
    }
    int ret;
    if((ret = CheckFrameFull(buffer, len))<0) {
        printf("帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(buffer,len);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    printf("frameType : 0x%x, packetType : 0x%x \n", frameType, packetType);
    if(frameType == 0x06 && packetType == 0xf2) {
        printf("接收到B38协议\n");
        deBugFrame(buffer,len);
        return 0;
    }
    printf("收到其他包，没有收到B38\n");
    return -2;
}



/**
 * @brief 
 * 
 * @param filename 
 * @param channelNo 
 * @param SocketFd 
 * @return int 
 */
int AutoGetPhotoHander(const char* filename, int channelNo, int SocketFd)
{
    // waitForB341(SocketFd); //!模拟心跳接受
    printf("进入自动抓拍程序\n");
    
    //要获取的通道号
    // SendProtocolB342(SocketFd);
    // printf("已发送B342 \n");
    //获取图像
    int ImgId = open(filename,O_RDONLY);
    if(-1 == ImgId) {
        perror("open error");
        return -1;
    }
    //获得图像大小
    struct stat sbuf;
    fstat(ImgId,&sbuf);
    void* ImgAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    //告诉主站开始发送图像
    int packetLen = sbuf.st_size / 1024 + 1;
    printf("发送通道号%d, 总包数%d, mmap读取图片大小为%zu\n",channelNo, packetLen, sbuf.st_size);
    SendProtocolB351(SocketFd,channelNo, packetLen);//3s循环发送，最多五次
    printf("已发送B351 \n");
    //等待回复
    waitForB352(SocketFd);
    //开始发送图片报文05F0
    printf("开始进行传图 \n");
    SendPhotoData(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,channelNo);
    //传输完毕，等待两秒，结束报文md5
    sleep(2); 
    SendProtocolB37(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,channelNo);//3s循环发送，最多五次
    printf("发送B37结束 \n");
    munmap(ImgAddr,sbuf.st_size);
    printf("图像传输完毕\n");  
    waitForB38(SocketFd);
    printf("收到B38协议 \n");
    std::cout << "当前通道号为 " << channelNo;
    // if(channelNo == 4)
    //     SendImageAnalysis(SocketFd);//同目录下有json文件
    return 0;
}



/**
 * @brief 
 * 
 * @param pBuffer 
 * @param Length 
 * @param SocketFd 
 * @return int 
 */
static int ManualGetPhotoHander(unsigned char* pBuffer, int Length,int SocketFd)
{
    printf("收到B341协议\n");
    struct ProtocolB341 *pB341 = (struct ProtocolB341 *)pBuffer;
    printf("进入手动抓拍程序\n");
    //要获取的通道号
    printf("要获取的通道号为 %d\n",pB341->channelNo);
    //回复，我知道了08EE
    printf("发送B342协议\n");
    SendProtocolB342(SocketFd);
    //获取图像
    int ImgId = open("image2.jpg",O_RDONLY);
    if(-1 == ImgId) {
        perror("open error");
        return -1;
    }
    //获得图像大小
    struct stat sbuf;
    fstat(ImgId,&sbuf);
    void* ImgAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    //告诉主站开始发送图像
    int packetLen = sbuf.st_size / 1024 + 1;
    SendProtocolB351(SocketFd,pB341->channelNo, packetLen);//3s循环发送，最多五次
    //等待回复
    waitForB352(SocketFd);
    //开始发送图片报文05F0
    SendPhotoData(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size,pB341->channelNo);
    //传输完毕，等待两秒，结束报文md5
    sleep(2); 
    SendProtocolB37(SocketFd,(unsigned char *)ImgAddr,sbuf.st_size, pB341->channelNo);//3s循环发送，最多五次
    //
    munmap(ImgAddr,sbuf.st_size);
    printf("图像传输完毕\n");   
    //暂时不补包
    return 0;
}


//收到一个包，判断它的帧类型和报文类型
//根据不同的帧类型和报文类型，做处理

/**
 * @brief 
 * 
 * @param pBuffer 
 * @param Length 
 * @return int 
 */
int FrameResolver(unsigned char* pBuffer, int Length ,int sockfd)
{
    int ret;
    if((ret = CheckFrameFull(pBuffer, Length))<0) {
        printf("帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(pBuffer,Length);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    //根据帧类型和包类型，调用不同的处理函数
    //查表，然后处理吗？
    for(int i=0;i<sizeof(Handlers)/sizeof(HandlerFun);i++) {
        if(Handlers[i].frameType == frameType && Handlers[i].packetType == packetType) {   
            if(Handlers[i].func!=NULL) {
                Handlers[i].func(pBuffer,Length,sockfd);
            }
            return 0;
        }
    }
    //没找到，处理
    printf("未处理协议 frameType = 0x%x, packetType = 0x%x\n",frameType,packetType);
    return -1;
}

/**
 * @brief 开始TCP客户端接收线程
 * 
 * @param pSocketId 
 */
void StartReadThread(int* pSocketId)
{
    pthread_t tId;
    int ret = pthread_create(&tId,NULL,[](void*sockid)->void* {
        while(1) {
            int fd = *(int*)sockid;
            int len = read(fd,buffer,1024);
            if(len < 0) {
                //出错
                printf("Thread Socket Read出错\n");
                return (void*) 0;
            }
            // 帧解析处理
            FrameResolver(buffer,len,fd);
        }
    }, pSocketId);
    if(ret){
        //出错
        return;
    }
    pthread_detach(tId);
}
/**
 * @brief 样例处理程序
 * 
 * @param pBuffer 
 * @param Length 
 * @param fd 
 * @return int 
 */
static int SampleHander(unsigned char* pBuffer, int Length, int fd)
{
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    printf("解析出 frameType = 0x%x, packetType = 0x%x\n",frameType,packetType);
    return 0;    
}


