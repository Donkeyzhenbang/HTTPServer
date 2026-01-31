#ifndef __CLIENTMODELUPGRADE_H__
#define __CLIENTMODELUPGRADE_H__

#include <iostream>
void recv_model(int connfd, int& model_script_channel);

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

#endif // __CLIENTMODELUPGRADE_H__
