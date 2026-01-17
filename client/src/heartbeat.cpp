#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <chrono>
#include "utils.h"
#include "heartbeat.h"

#define CMD_ID "10370000123456789"  // CMD_ID，17位编码

/**
 * @brief 心跳协议帧
 * 
 */
struct __attribute__((packed)) HeartbeatFrame {
    u_int16 sync; // 报文头：5AA5H
    u_int16 packetLength; // 报文长度
    char cmdId[17]; // CMD_ID，17位编码
    u_int8 frameType; // 帧类型，09H
    u_int8 packetType; // 报文类型，E6H
    u_int8 frameNo; // 帧序列号
    u_int32 clocktimeStamp; // 当前时间
    u_int16 CRC16;// 将sync到clocktimeStamp这部分进行校验
    u_int8 End;  //默认等于0x96
};

/**
 * @brief 初始化心跳帧，后面就可以拿这个心跳帧去发送了
 * 
 * @param frameData 
 */
void HeartbeatFrameInit(HeartbeatFrame &frameData)
{
    //before C++20 按名称指定结构体成员的初始化顺序
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x0004, //31 
        .cmdId = {},  //后面重新初始化
        .frameType = 0x09,
        .packetType = 0xE6,
        .frameNo = 0x00, //0x80
        .clocktimeStamp = 0, //后面重新初始化
        .CRC16 = 0,    //后面重新初始化，从packetLength开始
        .End = 0x96
    }; 
    //cmdId填充
    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId)); 
    //clocktimeStamp的填充
    auto now = std::chrono::system_clock::now();// 使用chrono库获取当前时间
    auto duration_since_epoch = now.time_since_epoch();// 计算自Unix纪元以来的时间
    auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();// 转换duration为秒（如果duration不是秒，则使用duration_cast转换）
    frameData.clocktimeStamp = static_cast<uint32_t>(seconds_since_epoch);// 将秒数转换为quint32类型
    //CRC16的填充
    // uintptr_t startAddr = reinterpret_cast<uintptr_t>(&frameData.packetLength), endAddr = reinterpret_cast<uintptr_t>(&frameData.CRC16);
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),sizeof(frameData)-5);
    // frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),(endAddr-startAddr));
}
/**
 * @brief 
 * 
 * @param socket 
 * @return int 返回值小于零，代表发生错误。大于代表正常发送的数量
 */
int SendHeartbeat(int socket)
{
    HeartbeatFrame frameData;
    HeartbeatFrameInit(frameData);
    deBugFrame((unsigned char*)&frameData, sizeof(frameData));
    int ret = write(socket,&frameData,sizeof(frameData));
    fsync(socket);//刷新缓冲区
    return ret;
    // return CheckFrameFull((unsigned char*)&frameData,sizeof(frameData));
    // return CheckFrameFull(reinterpret_cast<unsigned char*>(&frameData),sizeof(frameData));
}




