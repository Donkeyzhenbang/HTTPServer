#include <iostream>
#include <cstring> //memcpy
#include <unistd.h> //系统调用 fsync wirte read
#include <sys/types.h>
#include <sys/socket.h>
#include <chrono>   //用于时间相关函数
#include "../inc/sendfile.h"
#include "../inc/utils.h"
#include "../src/jsondist/json/json.h"
#include "../inc/alarmInfo.h"
#define CMD_ID "10370000123456789"  // CMD_ID，17位编码
#define PIC_ID 0x0001               // 图片ID，4字节

static const int AlarmMap[10] = {5, 4, 1, 2, 3, 0, 0, 7, 6, 0};
//                                     0         1         2            3            4           5分裂子      6铁塔     7         8         9异物-发7不发9
// const std::string class_name[10] = {"dustNet", "fire", "excavator", "craneCar", "craneTower", "splitter", "tower", "foreign", "people", "foreign"}
//1、施工机械 2、吊车 3、塔吊 4、烟火 5、防尘网 6、人员 7、线路异物

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
 * @brief 手动请求拍摄照片报文的响应报文格式
 * 
 */
struct __attribute__((packed)) ProtocolB342{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 commandStatus;

    u_int16 CRC16;
    u_int8 End;
};

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

struct __attribute__((packed)) ProtocolAlarmInfo{
    u_int8 alarmType;
    u_int8 alarmCofidence;
    u_int8 alarmAreaBeginX;
    u_int8 alarmAreaBeginY;
    u_int8 alarmAreaEndX;   //2448
    u_int8 alarmAreaEndY;   //2048
    u_int16 distanceOfChan;
    u_int16 distanceOfWire;
};

/**
 * @brief 图像分析告警上报数据格式
 * 
 */
struct __attribute__((packed)) ProtocolB313{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    //!以下为数据区域格式
    u_int8 channelNo;   //通道号
    u_int8 prePosition; //预置位
    u_int32 timeStamp;  //时间戳
    u_int8 alarmNum;    //告警目标数量
    struct ProtocolAlarmInfo* dataSource;

    u_int16 CRC16;
    u_int8 End;
};


// struct __attribute__((packed)) ProtocolB313 {
//     uint16_t sync;
//     uint16_t packetLength;
//     char cmdId[17];
//     uint8_t frameType;
//     uint8_t packetType;
//     uint8_t frameNo;

//     uint8_t channelNo;
//     uint8_t prePosition;
//     uint32_t timeStamp;
//     uint8_t alarmNum;
//     std::vector<ProtocolAlarmInfo> dataSource; // 替换指针为 vector
//     uint16_t CRC16;
//     uint8_t End;
// };

/**
 * @brief B342协议初始化，引用传递
 * 
 * @param frameData 
 */
void ProtocolB342FrameInit(ProtocolB342 &frameData)
{
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x0001, //28字节 整个结构体大小
        .cmdId = {},            //后面重新初始化
        .frameType = 0x05,      //帧类型，05H（图像控制响应报）
        .packetType = 0xEE,     //报文类型，EEH（手动请求拍摄照片/短视频报）
        .frameNo = 0x00,        //帧序列号，响应与请求数据报的帧号字节最高位为0
        .commandStatus = 0xFF,  //数据发送状态，FFH成功，00H失败
        .CRC16 = 0,             //后面重新初始化，从packetLength开始
        .End = 0x96
    };
    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId)); 
    //CRC16的填充
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),sizeof(frameData)-5);
}

/**
 * @brief 发送B342协议包
 * 
 * @param socket 
 * @return int 
 */
int SendProtocolB342(int socket)
{
    ProtocolB342 frameData;
    ProtocolB342FrameInit(frameData);
    int ret = write(socket,&frameData,sizeof(frameData));
    fsync(socket);//刷新缓冲区
    return ret;
}


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
//结构体，并且是缓冲区

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

void ProtocolB313FrameInit(ProtocolB313 &frameData, std::vector<alarmInfoMetaData>& parseInfo)
{
    int parseNum = parseInfo.size();    //解析出来一共几项危险告警
    frameData = {
        .sync = 0x5AA5,
        .packetLength = {0},          //!!!后面重新更改
        .cmdId = {0},               //!!!后面重新初始化
        .frameType = 0x05,          //帧类型，05H
        .packetType = 0xFE,         //报文类型，F7H
        .frameNo = 0x00,            //帧序列号，     
        .channelNo = 0x04,          //通道号，1或2   
        .prePosition = 0xFF,           //后续初始化        
        .timeStamp = GlobalTimeStamp,
        .alarmNum = parseNum,
        .dataSource = {},              //最大1024
        .CRC16 = 0,                 //重新计算        
        .End = 0x96
    };
    frameData.packetLength = 7 + 10 * parseNum;
    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId));
    frameData.dataSource = static_cast<ProtocolAlarmInfo*>(malloc(parseNum * sizeof(ProtocolAlarmInfo)));
    if (!frameData.dataSource)
    {
        throw std::bad_alloc();
    }
    for(int i = 0; i < parseNum; i ++){
        const auto &metaData = parseInfo[i];
        frameData.dataSource[i].alarmType = static_cast<uint8_t>(AlarmMap[metaData.alarmType]);
        std::cout << "metaData.alarmType =  " << metaData.alarmType << "    AlarmMap[metaData.alarmType] " << AlarmMap[metaData.alarmType] << std::endl;
        frameData.dataSource[i].alarmCofidence = static_cast<uint8_t>(metaData.alarmCofidence); // 假设置信度是 0~1 之间
        frameData.dataSource[i].alarmAreaBeginX = static_cast<uint8_t>(metaData.alarmAreaBeginX);
        frameData.dataSource[i].alarmAreaBeginY = static_cast<uint8_t>(metaData.alarmAreaBeginY);
        frameData.dataSource[i].alarmAreaEndX = static_cast<uint8_t>(metaData.alarmAreaEndX);
        frameData.dataSource[i].alarmAreaEndY = static_cast<uint8_t>(metaData.alarmAreaEndY);
        frameData.dataSource[i].distanceOfChan = static_cast<uint16_t>(metaData.distanceOfChan); // 假设单位是 0.1m
        frameData.dataSource[i].distanceOfWire = static_cast<uint16_t>(metaData.distanceOfWire); // 假设单位是 0.1m
    }
    //! 使用 reinterpret_cast 直接赋值 这里不能使用reinterpret是因为json解析的结构体与数据传输的结构体类型不一致 一个是int 一个是uint8_t
    // for (int i = 0; i < parseNum; i++) {
    //     frameData.dataSource[i] = *reinterpret_cast<ProtocolAlarmInfo*>(&parseInfo[i]);
    // }

}

int SendB313Protocol(int socket, std::vector<alarmInfoMetaData>& ParseInfo)
{
    ProtocolB313 frameData;
    ProtocolB313FrameInit(frameData, ParseInfo);
    // 计算 dataSource 的实际大小
    int dataSourceSize = frameData.alarmNum * sizeof(ProtocolAlarmInfo);

    // 计算总发送长度
    int headerSize = offsetof(ProtocolB313, dataSource); // 从头到 dataSource 指针的偏移量
    int footerSize = sizeof(frameData.CRC16) + sizeof(frameData.End);
    int totalSize = headerSize + dataSourceSize + footerSize;

    // 分配发送缓冲区
    //在 C++ 中，malloc 返回的是一个 void* 类型的指针，这意味着它是一个通用指针，不能直接赋值给其他类型的指针。
    //由于 C++ 的类型检查比 C 更严格，直接赋值给 char* 类型会产生编译错误。因此，必须进行显式的类型转换
    char *sendBuffer = static_cast<char*>(malloc(totalSize));
    if (!sendBuffer) {
        perror("Failed to allocate send buffer");
        return -1;
    }
  
    memcpy(sendBuffer, &frameData, headerSize); // 填充第一部分：固定头部    
    memcpy(sendBuffer + headerSize, frameData.dataSource, dataSourceSize);// 填充第二部分：dataSource 指向的内容    
    memcpy(sendBuffer + headerSize + dataSourceSize, &frameData.CRC16, footerSize);// 填充第三部分：固定尾部
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(sendBuffer + offsetof(ProtocolB313, packetLength)), totalSize - footerSize);
    std::cout << std::hex << frameData.CRC16 << std::endl;
    // printf("0x%x", frameData.CRC16);
    memcpy(sendBuffer + headerSize + dataSourceSize, &frameData.CRC16, footerSize - 1);// 填充第三部分：固定尾部
    // 发送完整数据
    int ret = write(socket, sendBuffer, totalSize);
    fsync(socket); // 刷新缓冲区
    printf("发送B313图像分析帧 \n");
    free(sendBuffer);
    free(frameData.dataSource);
    return ret;
}


// void ProtocolB313FrameInit(ProtocolB313 &frameData, const std::vector<alarmInfoMetaData>& parseInfo) {

//     int parseNum = parseInfo.size();

//     // 初始化基础信息
//     frameData = {
//         0x5AA5,                          // sync
//         static_cast<uint16_t>(7 + 10 * parseNum), // packetLength
//         {},                              // cmdId (后续填充)
//         0x05,                            // frameType
//         0xFE,                            // packetType
//         0x00,                            // frameNo
//         0x04,                            // channelNo
//         0xFF,                            // prePosition
//         GlobalTimeStamp,                 // timeStamp
//         static_cast<uint8_t>(parseNum),  // alarmNum
//         {},                              // dataSource (后续填充)
//         0,                               // CRC16
//         0x96                             // End
//     };
//     std::memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));

//     // 填充 dataSource
//     frameData.dataSource.reserve(parseNum);
//     for (const auto &metaData : parseInfo) {
//         frameData.dataSource.push_back({
//             static_cast<uint8_t>(metaData.alarmType),
//             static_cast<uint8_t>(metaData.alarmCofidence), // 假设置信度 0~1
//             static_cast<uint8_t>(metaData.alarmAreaBeginX),
//             static_cast<uint8_t>(metaData.alarmAreaBeginY),
//             static_cast<uint8_t>(metaData.alarmAreaEndX),
//             static_cast<uint8_t>(metaData.alarmAreaEndY),
//             static_cast<uint16_t>(metaData.distanceOfChan), // 假设单位为 0.1m
//             static_cast<uint16_t>(metaData.distanceOfWire)  // 假设单位为 0.1m
//         });
//     }
// }


// void SendB313Protocol(int socket, std::vector<alarmInfoMetaData>& ParseInfo) {
//     ProtocolB313 frameData;
//     ProtocolB313FrameInit(frameData, ParseInfo);
//     // 计算总数据大小
//     // size_t totalSize = sizeof(ProtocolB313) - sizeof(std::vector<ProtocolAlarmInfo>) +
//     //                    frameData.dataSource.size() * sizeof(ProtocolAlarmInfo);
//     std::cout << sizeof(frameData) << std::endl << sizeof(std::vector<ProtocolAlarmInfo>) << std::endl << frameData.dataSource.size() * sizeof(ProtocolAlarmInfo) << std::endl;
//     size_t totalSize = sizeof(frameData.sync) +
//                    sizeof(frameData.packetLength) +
//                    sizeof(frameData.cmdId) +
//                    sizeof(frameData.frameType) +
//                    sizeof(frameData.packetType) +
//                    sizeof(frameData.frameNo) +
//                    sizeof(frameData.channelNo) +
//                    sizeof(frameData.prePosition) +
//                    sizeof(frameData.timeStamp) +
//                    sizeof(frameData.alarmNum) +
//                    frameData.dataSource.size() * sizeof(ProtocolAlarmInfo) +
//                    sizeof(frameData.CRC16) +
//                    sizeof(frameData.End);
//     std::cout << "TotalSize = " << totalSize << std::endl;
//     // 构造发送缓冲区
//     std::vector<char> sendBuffer(totalSize);
//     char *ptr = sendBuffer.data();

//     // 按顺序填充数据
//     auto appendData = [&ptr](const void *data, size_t size) {
//         std::memcpy(ptr, data, size);
//         ptr += size;
//     };

//     appendData(&frameData.sync, sizeof(frameData.sync));
//     appendData(&frameData.packetLength, sizeof(frameData.packetLength));
//     appendData(&frameData.cmdId, sizeof(frameData.cmdId));
//     appendData(&frameData.frameType, sizeof(frameData.frameType));
//     appendData(&frameData.packetType, sizeof(frameData.packetType));
//     appendData(&frameData.frameNo, sizeof(frameData.frameNo));
//     appendData(&frameData.channelNo, sizeof(frameData.channelNo));
//     appendData(&frameData.prePosition, sizeof(frameData.prePosition));
//     appendData(&frameData.timeStamp, sizeof(frameData.timeStamp));
//     appendData(&frameData.alarmNum, sizeof(frameData.alarmNum));
//     appendData(frameData.dataSource.data(), frameData.dataSource.size() * sizeof(ProtocolAlarmInfo));
//     appendData(&frameData.CRC16, sizeof(frameData.CRC16));
//     appendData(&frameData.End, sizeof(frameData.End));

//     // 打印发送数据
//     // 使用 write 发送数据
//     ssize_t ret = write(socket, sendBuffer.data(), totalSize);
//     if (ret < 0) {
//         perror("Socket write failed");
//         return ;
//     }

// }


void SendImageAnalysis(int sockfd)   //入口参数struct 
{
    //拿到json解析结果 1、施工机械 2、吊车 3、塔吊 4、验货 5、防尘网 6、人员 7、线路异物
    std::vector<alarmInfoMetaData> res = getAlarmInfo("TestJson.json");
    std::cout << "Json解析结果：隐患目标个数为 " << res.size() << std::endl;
    // for(int i = 0; i < res.size(); i ++) {
    //     res[i].debug();
    // }
    if(res.size() != 0){
        //!发送图像分析帧 已经确定非0
        SendB313Protocol(sockfd, res);
    }else{
        std::cout << "Yolo图片无危险因素，无需上报" << std::endl;
    }

}

