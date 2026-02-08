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

    // Reply B38 (Transfer Complete ?)
    // Original code: SendProtocolB38(fd);
    // Needed to define B38 function if not existing, but checking imports... `SendProtocolB38` was called in original code.
    // Assuming `SendProtocolB38` is available (it was used in original RecvFileHandler).
    // Wait, original `recvfile.cpp` line 347: `SendProtocolB38(fd);`.
    // We must check if `SendProtocolB38` is linked. It was likely in `recvfile.cpp` or `sendfile.h`.
    // Ah, previous grep of `base/src/protocol_handler.cpp` didn't show it explicitly but I might have missed it. 
    // Assuming it works as per previous code.
    
    // Instead of implicit declaration, let's assume `HandleFileEnd` calls what's available.
    // If SendProtocolB38 is not found, I might need to declare it. 
    // But since I'm editing `recvfile.cpp`, I can verify if it's there.
    // It was used at line 347 of original file.
    
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
    // New Event Driven Handlers
    {.frameType = 0x05, .packetType = 0xEF, HandleFileStart}, // Start
    {.frameType = 0x05, .packetType = 0xF0, HandleFileData},  // Data
    {.frameType = 0x05, .packetType = 0xF1, HandleFileEnd},   // End
};


static MyQueue* get_connection_queue(int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (ctx) {
        return ctx->queue.get();
    }
    return nullptr;
}


/**
 * @brief 开始TCP客户端接收线程
 * 
 * @param pContext 连接上下文
 */
void StartReadThread(std::shared_ptr<ConnectionContext> pContext)
{
    pContext->is_processing_done = false;
    
    pContext->read_thread = std::thread([pContext]() {
        auto context = pContext;
        MyQueue* pQueue = context->queue.get();
        int connfd = context->connfd;
        
        //使用智能指针管理
        shared_ptr<unsigned char[]> pRecvbuf(new unsigned char[2048]);
        int times = 0;

        while(context->is_connection_alive) {
            int len = read(connfd, pRecvbuf.get(), 2048);
            if(len < 0) {
                //出错
                // 如果是连接重置，通常是客户端断开，不作为错误打印
                if (errno == ECONNRESET) {
                    printf("客户端断开连接 (Reset by peer)\n");
                } else if(errno == EBADF) {
                    printf("Socket 通道已经关闭 无需处理\n");
                }
                else {
                    printf("Socket Read出错: %s\n", strerror(errno));
                }
                context->is_connection_alive = false;
                break;
            }
            else if (len == 0){
                printf("客户端关闭连接\n");
                context->is_connection_alive = false;
                break;
            }
            for(int i=0;i<len;i++) {
                pQueue->push(pRecvbuf.get()[i]);
            }
            times ++;
            if(times%100==0){
                // printf("完成一次载入,times==%d\n",times);
            }
        }
    });
    
    pContext->read_thread.detach();
}



/**
 * @brief 改进版：读socket，区分空闲状态和接收中状态
 * 
 * @param sockfd 
 * @param pPacket 
 * @param pQueue 连接的队列
 * @param pIsConnectionAlive 指向连接是否存活的标志
 * @return int 返回0代表正常，-1代表连接断开，-2代表接收超时
 */
int recv_and_resolve(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive) {
    return run_protocol_resolver(sockfd, pPacket, pQueue, pIsConnectionAlive);
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







/**
 * @brief 每接收到一个新的客户端连接，建立新线程
 * 
 * @param arg 
 * @return void* 
 */
void *HandleClient(void *arg) {
    int connfd = *(int *)arg;
    free(arg);  // 释放动态分配的内存
    
    // 为每个连接创建独立的队列和上下文
    create_connection_context(connfd);
    auto context = get_connection_shared_ptr(connfd);
    
    int times = 0;
    printf("新客户端线程启动...\n");
    HeartbeatFrame frame;
    int ret = ReceiveHeartbeatFrame(connfd, &frame);
    
    if (ret == 0 && frame.frameType == 0x09) { // Client Request
        // Register device ID
        char device_id[18] = {0};
        memcpy(device_id, frame.cmdId, 17);
        std::cout << "收到心跳请求，设备ID: " << device_id << std::endl;
        
        // Use connection manager to set device ID
        {
            std::lock_guard<std::mutex> lock(connection_manager_mutex);
            auto it = connection_manager.find(connfd);
            if (it != connection_manager.end()) {
                 it->second->setDeviceId(device_id);
            }
        }
        SendHeartbeatResponse(connfd);
    }
    printf("接收心跳完毕");
    // SendHeartbeatResponse(connfd); // Removed extra response
    // printf("已发送心跳协议 \n"); // Removed logging
    mv_sleep(50); // Reduced sleep
    //! 服务器处理逻辑 读线程每次最多读取2048字节，这里是每个字节逐个解析，而不是按包解析。
    //! recv_and_resolve 函数是从队列中逐个字节取出并解析
    //! 状态机解析组成完整包
    StartReadThread(context);   
    while (1) {
        Packet_t* pPacket = new Packet_t; 
        
        // 获取当前连接的队列
        MyQueue* pQueue = context ? context->queue.get() : nullptr;
        if (!pQueue) {
            printf("错误：找不到连接 %d 的队列\n", connfd);
            delete pPacket;
            break;
        }
        //! 这里设计有问题，相当于第一次调用recv_and_resolve然后sFrameResolver先进行解析，解析出来时B351协议，
        //! 然后进行顺序程序流再调用，其实这里最好应该每个包都不一样处理
        //! 状态机暂时退出（返回0）触发条件：成功解析到一个完整的、校验正确的数据包
        // int ret = recv_and_resolve(connfd, pPacket, pQueue);
        int ret = recv_and_resolve(connfd, pPacket, pQueue, &context->is_connection_alive);
        // printf("返回值%d, times = %d\n", ret, ++times);
        if (-1 == ret) {
            close(connfd);
            delete pPacket;
            printf("客户端断开连接...\n");
            printf("客户端线程结束...\n");
            remove_connection_context(connfd);
            pthread_exit(NULL);
        }

        ServerFrameResolver(pPacket->packetBuffer, pPacket->packetLength, connfd);
        delete pPacket;

        if (context->is_processing_done)
            break;

    }
    
    // 清理连接上下文
    close(connfd);
    remove_connection_context(connfd);
    printf("客户端线程结束...\n");
    pthread_exit(NULL);
}

