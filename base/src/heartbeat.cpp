#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <chrono>
#include <mutex>
#include "../inc/heartbeat.h"
#include "../inc/utils.h"

#define CMD_ID "10370000123456789"

static void InitHeartbeatFrame(HeartbeatFrame &frameData, uint8_t frameType, uint16_t packetLength)
{
    memset(&frameData, 0, sizeof(frameData));
    
    frameData.sync = 0x5AA5;
    frameData.packetLength = packetLength;
    
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.frameType = frameType;
    frameData.packetType = 0xE6;
    frameData.frameNo = 0x00;
    frameData.End = 0x96;

    if (frameType == 0x09) {
        auto now = std::chrono::system_clock::now();
        auto duration_since_epoch = now.time_since_epoch();
        auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();
        frameData.clocktimeStamp = static_cast<uint32_t>(seconds_since_epoch);
    } else {
        // Server response data logic, using 0xFF or similar if needed.
        // For now, keeping it 0/as-is since we zeroed struct.
    }
    
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength), sizeof(frameData) - 5);
}

int SendHeartbeatRequest(int fd)
{
    HeartbeatFrame frame;
    InitHeartbeatFrame(frame, 0x09, (uint16_t)sizeof(frame));
    // deBugFrame((unsigned char*)&frame, sizeof(frame));
    return write(fd, &frame, sizeof(frame));
}

int SendHeartbeatResponse(int fd)
{
    // 静态模板，初始化一次包含所有固定字段
    static HeartbeatFrame templateFrame;
    static bool initialized = false;
    static std::mutex initMutex;

    if (!initialized) {
        std::lock_guard<std::mutex> lock(initMutex);
        if (!initialized) {
            memset(&templateFrame, 0, sizeof(templateFrame));
            templateFrame.sync = 0x5AA5;
            templateFrame.packetLength = sizeof(HeartbeatFrame);
            memcpy(templateFrame.cmdId, CMD_ID, sizeof(templateFrame.cmdId));
            templateFrame.frameType = 0x0A; // Response
            templateFrame.packetType = 0xE6;
            templateFrame.frameNo = 0;
            templateFrame.End = 0x96;
            initialized = true;
        }
    }

    // 复制模板到栈上 (非常快，内联memcpy)
    HeartbeatFrame frame = templateFrame;

    // 仅更新动态字段：当前时间戳
    // 使用 steady_clock 可能更快且足够用于心跳，这里保持 system_clock 以符合协议
    auto now = std::chrono::system_clock::now();
    frame.clocktimeStamp = (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // 更新CRC (只计算一次)
    // 优化：CRC 计算范围
    frame.CRC16 = GetCheckCRC16((unsigned char *)(&frame.packetLength), sizeof(HeartbeatFrame) - 5);

    return write(fd, &frame, sizeof(frame));
}

int ReceiveHeartbeatFrame(int fd, HeartbeatFrame* outFrame)
{
    unsigned char buffer[1024];
    
    // Add Timeout for safety
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval tv = {5, 0}; // 5s timeout
    int sret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (sret <= 0) return -1;

    int len = read(fd, buffer, sizeof(buffer));
    
    if (len <= 0) {
        return -1;
    }
    
    int ret;
    if ((ret = CheckFrameFull(buffer, len)) < 0) {
        std::cerr << "Frame parse error: " << ret << std::endl;
        deBugFrame(buffer, len);
        return -2;
    }
    
    uint8_t frameType, packetType;
    if (getFramePacketType(buffer, &frameType, &packetType) < 0) {
         return -2;
    }
    
    if (packetType != 0xE6) {
        // Not a heartbeat packet
        return -3; 
    }
    
    if (outFrame) {
        memcpy(outFrame, buffer, sizeof(HeartbeatFrame));
    }
    
    return 0; // Success
}
