#ifndef __BASE_PROTOCOL_H_
#define __BASE_PROTOCOL_H_

#include <sys/types.h>
#include <cstdint>

// Protocol structures shared between client and server

struct __attribute__((packed)) HeartbeatFrame {
    u_int16_t sync; // 报文头：5AA5H
    u_int16_t packetLength; // 报文长度
    char cmdId[17]; // CMD_ID，17位编码
    u_int8_t frameType; // 帧类型，09H
    u_int8_t packetType; // 报文类型，E6H
    u_int8_t frameNo; // 帧序列号
    u_int32_t clocktimeStamp; // 当前时间
    u_int16_t CRC16;// 将sync到clocktimeStamp这部分进行校验
    u_int8_t End;  //默认等于0x96
};

struct __attribute__((packed)) ProtocolPhotoData{
    u_int16_t sync;
    u_int16_t packetLength;

    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t channelNo;
    u_int16_t packetNo; // 总包数
    u_int16_t subpacketNo; // 子包包号
    int prefix_sample[2];
    char sample[1024]; //!数据区暂定 后面修改  

    u_int16_t CRC16;
    u_int8_t End;
};

struct __attribute__((packed)) ProtocolB351{
    u_int16_t sync;
    u_int16_t packetLength;

    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t channelNo;
    u_int8_t packetHigh;
    u_int8_t packetLow;
    char reverse[8];

    u_int16_t CRC16;
    u_int8_t End;
};

struct __attribute__((packed)) ProtocolB352{ 
    u_int16_t sync;        //报文头：5AA5H
    u_int16_t packetLength;//报文长度：28字节
    char cmdId[17];      //CMD_ID,17位编码
    u_int8_t frameType;    //帧类型，06H（远程图像数据响应报）
    u_int8_t packetType;   //报文类型，EFH（拍摄装置请求上送）
    u_int8_t frameNo;      //帧序列号
    u_int8_t uploadStatus; //FFH：允许；00H：不允许

    u_int16_t CRC16;       //CRC16校验
    u_int8_t End;          //报文尾
};

struct __attribute__((packed)) ProtocolB341{
    u_int16_t sync;
    u_int16_t packetLength;

    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t channelNo;
    char reverse[10];

    u_int16_t CRC16;
    u_int8_t End;
};

struct __attribute__((packed)) ProtocolB37{
    u_int16_t sync;        //报文头：5AA5H
    u_int16_t packetLength;//报文长度

    char cmdId[17];  //CMD_ID,17位编码
    u_int8_t frameType;    //帧类型，05H（远程图像数据报）
    u_int8_t packetType;   //报文类型，F1H（远程图像数据上送结束标记报）
    u_int8_t frameNo;      //帧序列号，80H（主动上传最高位为1）
    u_int8_t channelNo;    //通道号，1或2
    u_int32_t timeStamp;   //本图像拍摄时间
    char MD5[32];    //文件MD5码
    char reserve[8]; //前1字节表示文件类型：0 图片，1 视频。后7字节备用

    u_int16_t CRC16;       //CRC16校验
    u_int8_t End;          //报文尾
};

struct __attribute__((packed)) ProtocolB342{
    u_int16_t sync;
    u_int16_t packetLength;

    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t commandStatus;

    u_int16_t CRC16;
    u_int8_t End;
};

struct __attribute__((packed)) ProtocolB38{
    u_int16_t sync;
    u_int16_t packetLength;

    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t channelNo;
    u_int16_t ComplementPackSum;
    char reverse[8];

    u_int16_t CRC16;
    u_int8_t End;
};

struct __attribute__((packed)) ProtocolAlarmInfo{
    u_int8_t alarmType;
    u_int8_t alarmCofidence;
    u_int8_t alarmAreaBeginX;
    u_int8_t alarmAreaBeginY;
    u_int8_t alarmAreaEndX;
    u_int8_t alarmAreaEndY;
    u_int16_t distanceOfChan;
    u_int16_t distanceOfWire;
};

struct __attribute__((packed)) ProtocolB313Header{
    u_int16_t sync;
    u_int16_t packetLength;
    char cmdId[17];
    u_int8_t frameType;
    u_int8_t packetType;
    u_int8_t frameNo;
    u_int8_t channelNo;
    u_int8_t prePosition;
    u_int32_t timeStamp;
    u_int8_t alarmNum;
};
// Note: B313 is variable length. Followed by alarmNum * ProtocolAlarmInfo, then CRC16, End.

// Generic buffer packet structure used for parsing (moved from recvfile.h)
typedef struct Packet_t {
    int packetLength;
    unsigned char packetBuffer[2048];
} Packet_t;

#endif
