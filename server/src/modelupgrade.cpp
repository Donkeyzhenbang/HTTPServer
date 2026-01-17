#include "modelupgrade.h"
#include "utils.h"
#include <sys/mman.h>  // 添加mmap相关头文件
#include <sys/stat.h>  // 添加stat相关头文件
#include <fcntl.h>     // 添加文件操作相关头文件
#include <unistd.h>    // 添加sleep函数头文件
#include <iostream>
#include <cstring>
#include <cstdio>

static unsigned char buffer[1024] = {};//!不需要清空缓冲区吗
#define CMD_ID "10370000123456789"  // CMD_ID，17位编码
#define PIC_ID 0x0001               // 图片ID，4字节

/**
 * @brief 监拍装置请求上送照片报文格式
 * 
 */
struct __attribute__((packed)) ProtocolB351{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 channelNo;
    u_int8 packetHigh;
    u_int8 packetLow;
    char reverse[8];

    u_int16 CRC16;
    u_int8 End;
};

/**
 * @brief 远程图像数据上送结束标记数据报文B37
 * 
 */
struct __attribute__((packed)) ProtocolB37{
    u_int16 sync;        //报文头：5AA5H
    u_int16 packetLength;//报文长度：72字节

    char cmdId[17];  //CMD_ID,17位编码
    u_int8 frameType;    //帧类型，05H（远程图像数据报）
    u_int8 packetType;   //报文类型，F1H（远程图像数据上送结束标记报）
    u_int8 frameNo;      //帧序列号，80H（主动上传最高位为1）
    u_int8 channelNo;    //通道号，1或2
    u_int32 timeStamp;   //本图像拍摄时间
    char MD5[32];    //文件MD5码
    char reserve[8]; //前1字节表示文件类型：0 图片，1 视频。后7字节备用

    u_int16 CRC16;       //CRC16校验
    u_int8 End;          //报文尾
};

/**
 * @brief 远程图像数据报
 * 
 */
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

/**
 * @brief 图像传输包
 * 
 */
static struct ProtocolPhotoData PhotoDataPacket = {
    .sync = 0x5AA5,
    .packetLength = 0,          //!!!后面重新更改
    .cmdId = {0},               //!!!后面重新初始化
    .frameType = 0x05,          //帧类型，05H
    .packetType = 0xF0,         //报文类型，F0H
    .frameNo = 0x00,            //帧序列号，请求数据报的帧号字节最高位为0         
    .channelNo = 0,     //通道号，1或2   
    .packetNo = 0,//后续初始化        
    .subpacketNo = 0,
    .prefix_sample = {0},
    .sample = {0},              //最大1024
    .CRC16 = 0,                 //重新计算        
    .End = 0x96
};

/**
 * @brief B351协议帧进行初始化
 * 
 * @param frameData 
 * @param channelNo 
 * @param packetLen 
 */
void ProtocolB351FrameInit(ProtocolB351 &frameData, u_int8 channelNo, u_int16 packetLen)
{
    //!注意这里和定义的时候不要重复初始化
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x000b,     //38字节 整个结构体大小
        .cmdId = {},                //后面重新初始化
        .frameType = 0x05,          //帧类型，05H（远程图像数据报）             
        .packetType = 0xEF,         //报文类型，EFH（监拍装置请求上送照片/短视频报）    
        .frameNo = 0x00,            //帧序列号，请求数据报的帧号字节最高位为0         
        .channelNo = channelNo,     //通道号，1或2   
        .packetHigh = static_cast<u_int8>((packetLen >> 8) & 0xFF),
        .packetLow = static_cast<u_int8>(packetLen & 0xFF),           
        .reverse = {(char)(PIC_ID & 0xFF), (char)((PIC_ID >> 8) & 0xFF), (char)((PIC_ID >> 16) & 0xFF), (char)(PIC_ID >> 24), 0, 0, 0, 0},              
        .CRC16 = 0,                 //后面重新初始化，从packetLength开始
        .End = 0x96
    };
    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId)); 
    //CRC16的填充
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),sizeof(frameData)-5);
}

/**
 * @brief 提前告诉服务器，有多少个包
 * 
 * @param socket 
 * @param channelNo 通道号
 * @param packetLen 图片总包数
 * @return int 
 */
int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen)
{
    ProtocolB351 frameData;
    ProtocolB351FrameInit(frameData, channelNo, packetLen);
    int ret = write(socket,&frameData,sizeof(frameData));
    fsync(socket);//刷新缓冲区
    return ret;
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



/**
 * @brief B37协议初始化
 * 
 * @param frameData 
 * @param pBuffer 
 * @param Length 
 */
void ProtocolB37FrameInit(ProtocolB37 &frameData, unsigned char * pBuffer, int Length, u_int8 channelNo)
{
    frameData = {
        .sync = 0x5AA5,        
        .packetLength = 0x002d,
        .cmdId = {},  
        .frameType = 0x05,    
        .packetType = 0xF1,   
        .frameNo = 0x80,      
        .channelNo = channelNo,    
        .timeStamp = GlobalTimeStamp,       
        .MD5 = {},   
        .reserve = {0, 0, 0, 0, 0, 0, 0, 0}, 
        .CRC16 = 0,       
        .End = 0x96          
    };

    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId)); 

    // auto now = std::chrono::system_clock::now();
    // auto duration_since_epoch = now.time_since_epoch();
    // auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();
    // frameData.timeStamp = static_cast<uint32_t>(seconds_since_epoch);
    char* md5str = ComputeBufferMd5(pBuffer, Length);
    std::cout << "图片md5校验码为" << md5str <<std::endl;
    memcpy(frameData.MD5, md5str, 32); 
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),sizeof(frameData)-5);
}

/**
 * @brief 发送B37协议包
 * 
 * @param socket 
 * @param pBuffer 
 * @param Length 
 * @return int 
 */
int SendProtocolB37(int socket, unsigned char * pBuffer, int Length, u_int8 channelNo)
{
    ProtocolB37 frameData;
    ProtocolB37FrameInit(frameData, pBuffer, Length, channelNo);
    int ret = write(socket,&frameData,sizeof(frameData));
    fsync(socket);//刷新缓冲区
    printf("发送B37 \n");
    return ret;
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
 * @brief 发送图像数据报文，对图像进行分片
 * 
 * @param SocketFd 
 * @param pBuffer 
 * @param Length 
 * @param channelNo 
 * @return int 
 */
int SendPhotoData(int SocketFd,unsigned char* pBuffer, int Length, int channelNo) 
{
    static int temp_num = 0;
    static int temp_pic_num = 0;
    //填充packetLength
    //todo,在循环体中执行
    //填充CMD_ID
    memcpy(PhotoDataPacket.cmdId, CMD_ID,sizeof(PhotoDataPacket.cmdId));//初始化CMD_ID
    //填充类型、通道号
    PhotoDataPacket.frameType = 0x05;
    PhotoDataPacket.packetType = 0xF0;
    PhotoDataPacket.channelNo = channelNo;
    //填充总包数、所需的包的个数（最大数据包长为1024字节）
    PhotoDataPacket.packetNo = Length / 1024 + 1;
    int PacketNums = PhotoDataPacket.packetNo;
    int tailPacketDataLength = Length % 1024;
    for(int i=0; i <PacketNums; i++) {//最后一个包长度
        //子包包号
        PhotoDataPacket.frameNo = i;
        PhotoDataPacket.subpacketNo = i;
        PhotoDataPacket.prefix_sample[0] = 3; 
        PhotoDataPacket.prefix_sample[1] = 1024 * i; 
        //分情况确定packetLength和图像数据区，还有CRC16，还有End
        int write_len = 0;
        if(i != PacketNums-1) { //1024包发送方式
            // PhotoDataPacket.packetLength = sizeof(PhotoDataPacket);
            write_len = sizeof(PhotoDataPacket);
            PhotoDataPacket.packetLength = sizeof(PhotoDataPacket) - 27;
            memcpy(PhotoDataPacket.sample,pBuffer + i*1024,1024);
            PhotoDataPacket.CRC16 = GetCheckCRC16((unsigned char *)(&PhotoDataPacket.packetLength),sizeof(PhotoDataPacket)-5);
            PhotoDataPacket.End = 0x96;
        }else{ //最后一个包发送方式
            // PhotoDataPacket.packetLength = tailPacketDataLength + 32;
            write_len = tailPacketDataLength + 32 + 8;
            PhotoDataPacket.packetLength = tailPacketDataLength + 8 + 32 - 27;
            memcpy(PhotoDataPacket.sample,pBuffer + i*1024,tailPacketDataLength);
            u_int16 CRC16= GetCheckCRC16((unsigned char *)(&PhotoDataPacket.packetLength), tailPacketDataLength+27);
            memcpy((void*)&PhotoDataPacket+29+8+tailPacketDataLength, &CRC16, 2);
            memset((void*)&PhotoDataPacket+29+8+tailPacketDataLength+2,0x96,1);
        }
        //统一发送
        // deBugFrame((unsigned char*)&PhotoDataPacket,PhotoDataPacket.packetLength);
        // usleep(1 * 1000);
        // printf("图片数据区PhotoDataPacket.packetLength大小%d, wirte_len大小%d, 包数%d\n", PhotoDataPacket.packetLength, write_len, temp_pic_num++);
        int ret = write(SocketFd,&PhotoDataPacket,write_len);
        fsync(SocketFd);//刷新缓冲区
        if(ret<0) {
            perror("send error");
            printf("send error \n");
            return -1;
        }
    }
    printf("Length : %d, PacketNums: %d, tailPacketDataLength: %d \n",Length, PacketNums, tailPacketDataLength);
    temp_num ++;
    return 0;
}


/**
 * @brief 
 * 
 * @param filename 
 * @param channelNo 
 * @param SocketFd 
 * @return int 
 */
int SendModelToDevice(const char* filename, int channelNo, int SocketFd)
{
    // waitForB341(SocketFd); //!模拟心跳接受
    printf("进入传输模型文件程序\n");
    
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
    void* ModelAddr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, ImgId, 0);
    //告诉主站开始发送图像
    int packetLen = sbuf.st_size / 1024 + 1;
    printf("发送通道号%d, 总包数%d, mmap读取图片大小为%zu\n",channelNo, packetLen, sbuf.st_size);
    SendProtocolB351(SocketFd,channelNo, packetLen);//3s循环发送，最多五次
    printf("已发送B351 \n");
    //等待回复
    waitForB352(SocketFd);
    //开始发送图片报文05F0
    printf("开始进行传图 \n");
    SendPhotoData(SocketFd,(unsigned char *)ModelAddr,sbuf.st_size,channelNo);
    //传输完毕，等待两秒，结束报文md5
    sleep(2); 
    SendProtocolB37(SocketFd,(unsigned char *)ModelAddr,sbuf.st_size,channelNo);//3s循环发送，最多五次
    printf("发送B37结束 \n");
    munmap(ModelAddr,sbuf.st_size);
    printf("图像传输完毕\n");  
    waitForB38(SocketFd);
    printf("收到B38协议 \n");
    std::cout << "当前通道号为 " << channelNo;
    return 0;
}