#include <iostream>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <cstring>
#include "../inc/sendfile.h"
#include "../inc/utils.h"


#define CMD_ID "10370000123456789"  // CMD_ID，17位编码
#define PIC_ID 0x0001               // 图片ID，4字节

struct __attribute__((packed)) ProtocolB341{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 channelNo;
    char reverse[10];

    u_int16 CRC16;
    u_int8 End;
};

struct __attribute__((packed)) ProtocolB352{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 upload_status;

    u_int16 CRC16;
    u_int8 End;
};

//! 此处为无需补包协议 后续补包再重新写 
struct __attribute__((packed)) ProtocolB38{
    u_int16 sync;
    u_int16 packetLength;

    char cmdId[17];
    u_int8 frameType;
    u_int8 packetType;
    u_int8 frameNo;
    u_int8 channelNo;
    u_int16 ComplementPackSum;
    char reverse[8];

    u_int16 CRC16;
    u_int8 End;
};

/**
 * @brief B341结构体初始化
 * 
 * @param frameData 
 */
void ProtocolB341FrameInit(ProtocolB341& frameData, int channel = ChannelNum)
{
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x000B,
        .cmdId = {},
        .frameType = 0x07,
        .packetType = 0xEE,
        .frameNo = 0x00,
        .channelNo = static_cast<unsigned char>(channel),
        .reverse = {0},
        .CRC16 = 0,
        .End = 0x96
    };
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.CRC16 = GetCheckCRC16((unsigned char*)(&frameData.packetLength), sizeof(frameData) - 5);
}

/**
 * @brief 发送B341
 * 
 * @param socket 
 * @return int 
 */
int SendProtocolB341(int socket, int channel)
{
    ProtocolB341 frameData;
    ProtocolB341FrameInit(frameData, channel);
    printf("B341协议通道号为 %d \n", frameData.channelNo);
    //使用send避免内核中断信号
    printf("使用send避免内核中断信号\n");
    ssize_t ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    if (ret == -1) {
        // 处理 send 错误
        if (errno == EPIPE) {
            printf("Broken pipe: connection closed by peer\n");
        } else {
            perror("send failed");
        }
        return -1;
    }
    // int ret = write(socket, &frameData, sizeof(frameData));
    // fsync(socket);
    return ret;
}

/**
 * @brief B352结构体初始化
 * 
 * @param frameData 
 */
void ProtocolB352FrameInit(ProtocolB352& frameData)
{
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x0001,
        .cmdId = {},
        .frameType = 0x06,
        .packetType = 0xEF,
        .frameNo = 0x00,
        .upload_status = 0xFF,
        .CRC16 = 0,
        .End = 0x96
    };
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.CRC16 = GetCheckCRC16((unsigned char*)(&frameData.packetLength), sizeof(frameData) - 5);
}

/**
 * @brief 发送B352
 * 
 * @param socket 
 * @return int 
 */
int SendProtocolB352(int socket)
{
    ProtocolB352 frameData;
    ProtocolB352FrameInit(frameData);
    int ret = write(socket, &frameData, sizeof(frameData));
    fsync(socket);
    return ret;
}

/**
 * @brief B38初始化
 * 
 * @param frameData 
 */
void ProtocolB38FrameInit(ProtocolB38& frameData)
{
    frameData = {
      .sync = 0x5AA5,
      .packetLength = 0x000B,
      .cmdId = {},
      .frameType = 0x06,
      .packetType = 0xF2,
      .frameNo = 0x80,
      .channelNo = 0x01,
      .ComplementPackSum = 0x00,
      .reverse = {},
      .CRC16 = 0,
      .End = 0x96
    };
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.CRC16 = GetCheckCRC16((unsigned char*)(&frameData.packetLength), sizeof(frameData) - 5);
}

/**
 * @brief 发送无需补包时B38协议
 * 
 * @param socket 
 * @return int 
 */
int SendProtocolB38(int socket)
{
    ProtocolB38 frameData;
    ProtocolB38FrameInit(frameData);
    deBugFrame((unsigned char*)&frameData, sizeof(frameData));
    int ret = write(socket, &frameData, sizeof(frameData));
    fsync(socket);
    return ret;
}


/**
 * @brief 补包时发送B38协议
 * 
 * @param socket 
 * @return int 
 */
int SendProtocolB38Complement(int socket)
{

}