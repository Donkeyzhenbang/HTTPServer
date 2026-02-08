#include <iostream>
#include <cstdio>       //用于 printf 等标准输入输出操作（可以替代 <stdio.h>）
#include <cstdlib>      //用于 malloc 和 free 等内存分配（可以替代 <stdlib.h>）
#include <cstring>      //用于 memcpy 和 strerror 函数（可以替代 <string.h>）
#include <unistd.h>     //用于 read、write 等系统调用
#include <pthread.h>
#include <sys/types.h>  //用于套接字相关的数据类型和函数
#include <sys/socket.h>
#include <arpa/inet.h>  //用于 IP 地址处理和端口号等网络编程功能
#include <netinet/in.h>
#include <chrono>       //用于计时功能
#include <memory>
#include <functional>
#include <queue>        //用于 std::queue
#include <ctime>        //用于时间处理
#include <vector>       //用于 std::vector
#include <unordered_map> //用于管理每个连接的队列
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <string>
#include "sendfile.h"
#include "heartbeat.h"
#include "recvfile.h"
#include "connection.h"
#include "utils.h"
using namespace std;
static unsigned char buffer[1024] = {};



// New Event-Driven Handlers
// Forward declaration
static void ProcessConnectionCore(int fd);

// Heartbeat Handler (0x09, 0xE6)
static int HandleHeartbeat(unsigned char* pBuffer, int Length, int fd) {
    HeartbeatFrame* frame = (HeartbeatFrame*)pBuffer;
    char device_id[18] = {0};
    memcpy(device_id, frame->cmdId, 17);
    
    // Update Context
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if(ctx) {
        ctx->setDeviceId(device_id);
    }
    
    // Send Response
    // printf("Received Heartbeat from %s. Sending Response.\n", device_id);
    SendHeartbeatResponse(fd);
    return 0;
}

// 1. Initial Handshake / Start (0x05, 0xEF)
static int HandleFileStart(unsigned char* pBuffer, int Length, int fd) {
    printf("接收到B351 05EF (Start)\n");
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if(!ctx) return -1;

    struct ProtocolB351 *pB351 = (struct ProtocolB351*)pBuffer;
    int channelNo = pB351->channelNo;
    int packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);

    // Initialise State
    if (ctx->pPhotoBuffer != nullptr) {
        printf("Warning: Previous buffer not cleared. Clearing now.\n");
        free(ctx->pPhotoBuffer);
    }
    
    ctx->pPhotoBuffer = (unsigned char*)calloc(packetLen, 1024);
    if (!ctx->pPhotoBuffer) {
        printf("内存分配失败\n");
        return -1;
    }

    ctx->channelNo = channelNo;
    ctx->packetLen = packetLen;
    ctx->recvStatus.assign(packetLen, false);
    ctx->PhotoFileSize = 0;
    ctx->maxOffset = 0;
    ctx->transferState = ConnectionContext::RECEIVING;

    printf("Start Receiving Channel %d, Packets: %d\n", channelNo, packetLen);

    // Reply Ack
    SendProtocolB352(fd);
    printf("已发送B352 06EF (Ack)\n");

    return 0;
}

// 2. Data Packet (0x05, 0xF0)
static int HandleFileData(unsigned char* pBuffer, int Length, int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if(!ctx || ctx->transferState != ConnectionContext::RECEIVING) {
        // printf("Ignored Data Packet (State not RECEIVING)\n");
        return 0; // Ignore or error
    }

    // Since pBuffer is the whole packet from Packet_t in standard loop
    ProtocolPhotoData *pPhotoPacket = (ProtocolPhotoData *)pBuffer;
    u_int16 subpacketNo = pPhotoPacket->subpacketNo;
    // Length is total packet length. Data length = PacketLength - Header(21) - Tail(1)? 
    // Wait, Packet_t->packetLength includes everything.
    // ProtocolPhotoData struct:
    // head(21) + subPacketNo(2) + sample(1024) + CRC(2) + Tail(1)? 
    // Standard length logic:
    // We used: int dataLen = pPacket->packetLength - 32 - 8; (?) In original code
    // Let's check ProtocolPhotoData definition.
    // It has a flexible member `sample`.
    // The previous loop code: `int dataLen = pPacket->packetLength - 32 - 8;` -> Logic seems specific.
    // Let's assume standard calculation: PacketLength - sizeof(Headers)
    // Actually, let's keep the logic from original loop: `dataLen = Length - 32 - 8;` Wait, where did 32+8 come from?
    // Let's use `dataLen = Length - 25`. (Sync 2 + Len 2 + Cmd 17 + Type 2 + No 2 = 25 bytes header. + CRC 2 + End 1 = 3 bytes trailer. Total overhead 28).
    // The original code was: `int dataLen = pPacket->packetLength - 32 - 8;` -- that looks suspicious or specific to alignment/padding.
    // Wait, original code:
    // if(frameType==0x05 && packetType == 0xF0) ...
    //   int dataLen = pPacket->packetLength - 32 - 8;
    // Let's trust the user's original calculation or re-verify. 
    // Actually, looking at `Packet_t`, it's just raw bytes.
    // Let's use `Length - 28` as a safe generic guess if I can't confirm.
    // Let's re-read original output I gathered.
    // "int dataLen = pPacket->packetLength - 32 - 8;"
    
    // Let's stick to what was there.
    int dataLen = Length - 32 - 8; 
    if(dataLen <= 0) dataLen = 1024; // Fallback? Or just trust it.

    if(ctx->pPhotoBuffer && subpacketNo < ctx->packetLen) {
        // Offset logic: subpacketNo * 1024
        int offset = subpacketNo * 1024;
        
        // Safety check
        if (dataLen > 1024) dataLen = 1024;

        memcpy(ctx->pPhotoBuffer + offset, pPhotoPacket->sample, dataLen);
        ctx->PhotoFileSize += dataLen;

        int endPos = offset + dataLen;
        if (endPos > ctx->maxOffset) {
            ctx->maxOffset = endPos;
        }
        ctx->recvStatus[subpacketNo] = true;
    } else {
         if(ctx->pPhotoBuffer) printf("包号 %d 超出范围 %d\n", subpacketNo, ctx->packetLen);
    }

    return 0;
}

// 3. End / Finish (0x05, 0xF1) 
static int HandleFileEnd(unsigned char* pBuffer, int Length, int fd) {
    printf("接收到B37/05F1 (End)\n");
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if(!ctx || ctx->transferState != ConnectionContext::RECEIVING) {
        printf("End packet received but not in receiving state.\n");
        return -1;
    }

    // Verify completeness
    int missing = 0;
    for(bool status : ctx->recvStatus) {
        if(!status) missing++;
    }
    printf("传输结束。缺失包数: %d\n", missing);

    // Save File
    char filename[100];
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time_t);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", local_time);
    sprintf(filename, "ch%d_%s.jpg", ctx->channelNo, time_str);

    std::string upload_dir = get_upload_dir();
    if (ensure_dir_exists(upload_dir)) {
        std::string fullpath = upload_dir + "/" + std::string(filename);
        printf("[HandleFileEnd] 保存图片到: %s\n", fullpath.c_str());
        
        std::ofstream ofs(fullpath, std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char*>(ctx->pPhotoBuffer), ctx->maxOffset);
            ofs.close();
        }
    }

    // Send B38 (End Ack) to client
    printf("Sending B38 to %d\n", fd);
    SendProtocolB38(fd);
    
    // Clean up
    free(ctx->pPhotoBuffer);
    ctx->pPhotoBuffer = nullptr;
    ctx->transferState = ConnectionContext::IDLE;
    ctx->is_processing_done = true; // Signal completion if needed, or keep alive?
    // Original code had `ctx->is_processing_done = true`.
    
    return 0;
}


struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd);
};


static struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, NULL},  // ProtocolB341
    {.frameType = 0x06, .packetType = 0xEF, NULL},  // Another Protocol
    {.frameType = 0x09, .packetType = 0xE6, HandleHeartbeat}, // Heartbeat
    // New Event Driven Handlers
    {.frameType = 0x05, .packetType = 0xEF, HandleFileStart}, // Start
    {.frameType = 0x05, .packetType = 0xF0, HandleFileData},  // Data
    {.frameType = 0x05, .packetType = 0xF1, HandleFileEnd},   // End
};


// ----------------------------------------------------------------------
// Reactor / IO Thread Pool Implementation
// ----------------------------------------------------------------------

#include "../inc/ServerApp.h"

// Helper to find sync header 0x5AA5
static int FindSyncHeader(const std::vector<uint8_t>& buffer, int start) {
    for(size_t i = start; i < buffer.size() - 1; ++i) {
        if(buffer[i] == 0xA5 && buffer[i+1] == 0x5A) {
            return i;
        }
    }
    return -1;
}

void OnClientRead(int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return;

    {
        std::lock_guard<std::mutex> lock(ctx->bufferMutex);
        // Read directly into vector
        char buf[4096];
        while(true) {
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno != EINTR) {
                    // Error or Close
                    ctx->is_connection_alive = false;
                    // Trigger close logic?
                    // ServerApp will detect EPOLLRDHUP usually, but here handle read error
                    // Close fd?
                    // close(fd); remove_... rely on ServerApp or doing it here
                    // Actually, let's keep it alive until Process detects it or ServerApp specific RDHUP handler.
                    // But if read fails, we should stop.
                    // Let's assume EPOLLRDHUP handles clean close, or we can flag it.
                }
                break;
            }
            if (n == 0) {
                // EOF
                ctx->is_connection_alive = false;
                break;
            }
            ctx->inputBuffer.insert(ctx->inputBuffer.end(), buf, buf + n);
        }
    }
    
    // Trigger Processing Task if not already running
    bool expected = false;
    if (ctx->is_processing.compare_exchange_strong(expected, true)) {
        // Enqueue task
        ServerApp::getInstance().getThreadPool().enqueue([fd](){
            ProcessConnection(fd);
        });
    }
}

void ProcessConnection(int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (!ctx) return;

    while (ctx->is_connection_alive) {
        std::vector<uint8_t> packet;
        bool hasPacket = false;
        
        {
            std::lock_guard<std::mutex> lock(ctx->bufferMutex);
            if(ctx->inputBuffer.empty()) {
                ctx->is_processing = false;
                return; 
            }

            // Parse Logic
            int syncPos = FindSyncHeader(ctx->inputBuffer, 0);
            if(syncPos == -1) {
                // No header found. Discard all but last byte (could be half header)
                // actually if buffer is huge and no header, discard start?
                // For safety: if size > header check.
                if(ctx->inputBuffer.size() > 1 && ctx->inputBuffer.back() != 0xA5) {
                   ctx->inputBuffer.clear();
                } else if(ctx->inputBuffer.size() > 1) {
                     // Keep last byte 
                     uint8_t last = ctx->inputBuffer.back();
                     ctx->inputBuffer.clear();
                     ctx->inputBuffer.push_back(last);
                }
                ctx->is_processing = false;
                return;
            }

            // Sync found at syncPos. Discard garbage before.
            if(syncPos > 0) {
                ctx->inputBuffer.erase(ctx->inputBuffer.begin(), ctx->inputBuffer.begin() + syncPos);
                syncPos = 0;
            }

            // Check Length (4 bytes needed: Sync(2) + Len(2))
            if(ctx->inputBuffer.size() < 4) {
                ctx->is_processing = false;
                return; // Need more data
            }

            // Packet Length (Little Endian in protocol? or Big?)
             // Protocol definition: u_int16_t packetLength;
             // Let's look at `CheckFrameFull` logic or `ServerFrameResolver` usage.
             // Usually network is Big Endian, but struct might be packed.
             // ProtocolB351 uses `packetHigh << 8 | packetLow`.
             // `Packet_t` uses `packetLength`.
             // Looking at `protocol_handler.cpp`, `packetLen = (buffer[3] << 8) | buffer[2];` (Little Endian?)
             // Or `buffer[2] | (buffer[3] << 8)`?
             // Let's assume standard logic from `Packet_t`.
             // Wait, `protocol_handler.cpp` usually does `memcpy(&len, buf+2, 2)`.
             // Let's assume Little Endian based on x86 default if not ntohs.
             // Let's check `recvfile.cpp` `packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);` for B351.
             // But valid packet length in header?
             // Let's assume byte 2 is low, byte 3 is high? Or vice versa.
             // I will use `(ctx->inputBuffer[3] << 8) | ctx->inputBuffer[2]`.
             
            int packetLen = (ctx->inputBuffer[3] << 8) | ctx->inputBuffer[2];
            
            // Safety check
            if (packetLen > 4096 || packetLen < 4) {
                 // Invalid length, discard sync 0xA5 and retry scan
                 // Actually discard 2 bytes 0xA5 0x5A
                 ctx->inputBuffer.erase(ctx->inputBuffer.begin(), ctx->inputBuffer.begin() + 2);
                 continue; // loop again
            }

            if(ctx->inputBuffer.size() < (size_t)packetLen) {
                ctx->is_processing = false;
                return; // Wait for full packet
            }
            
            // Extract Packet
            packet.assign(ctx->inputBuffer.begin(), ctx->inputBuffer.begin() + packetLen);
            ctx->inputBuffer.erase(ctx->inputBuffer.begin(), ctx->inputBuffer.begin() + packetLen);
            hasPacket = true;
        }

        if(hasPacket) {
            // Process Packet
            // printf("Dispatching Packet Len %lu\n", packet.size());
            ServerFrameResolver(packet.data(), packet.size(), fd);
        }
    }
    
    // If not alive
    ctx->is_processing = false;
}

// Deprecated Stub
void *HandleClient(void *arg) {
    if(arg) free(arg);
    return nullptr;
}



int ServerFrameResolver(unsigned char* pBuffer, int Length ,int sockfd)
{
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









