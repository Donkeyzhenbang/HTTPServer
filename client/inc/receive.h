#ifndef __RECEIVE_H_
#define __RECEIVE_H_
void StartReadThread(int* pSocketId);
int AutoGetPhotoHander(const char* filename, int channelNo, int SocketFd);
int waitForHeartBeat(int fd);
int waitForB341(int fd);
int waitForB351(int fd);
struct __attribute__((packed)) ProtocolB352{ 
    u_int16 sync;        //报文头：5AA5H
    u_int16 packetLength;//报文长度：28字节
    char cmdId[17];      //CMD_ID,17位编码
    u_int8 frameType;    //帧类型，06H（远程图像数据响应报）
    u_int8 packetType;   //报文类型，EFH（拍摄装置请求上送）
    u_int8 frameNo;      //帧序列号
    u_int8 uploadStatus; //FFH：允许；00H：不允许

    u_int16 CRC16;       //CRC16校验
    u_int8 End;          //报文尾
};
#endif // !1

