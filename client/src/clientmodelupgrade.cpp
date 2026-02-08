#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <chrono>
#include <memory>
#include <functional>
#include <queue>
#include <ctime>
#include <vector>
#include "utils.h"
#include "heartbeat.h"
#include "sendfile.h"
#include "receive.h"
#include "clientmodelupgrade.h"
#include "connection.h"
#include "protocol_handler.h"

#define CMD_ID "10370000123456789"
#define PIC_ID 0x0001
using namespace std;

static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd, int& model_script_channel);

struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd, int& model_script_channel);
};

static struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, NULL},
    {.frameType = 0x06, .packetType = 0xEF, NULL},
    {.frameType = 0x05, .packetType = 0xEF, RecvFileHandler}
};

static MyQueue* get_connection_queue(int fd) {
    auto it = connection_manager.find(fd);
    if (it != connection_manager.end()) {
        return it->second->queue.get();
    }
    return nullptr;
}

int ClientFrameResolver(unsigned char* pBuffer, int Length ,int sockfd, int& model_script_channel)
{
    u_int8 frameType, packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    for(int i = 0; i < sizeof(Handlers) / sizeof(HandlerFun); i ++) {
        if(Handlers[i].frameType == frameType && Handlers[i].packetType == packetType) {   
            if(Handlers[i].func != NULL) {
                Handlers[i].func(pBuffer, Length, sockfd, model_script_channel);
            }
            return 0;
        }
    }
    printf("未处理协议 frameType = 0x%x, packetType = 0x%x\n",frameType, packetType);
    return -1;
}

void StartReadThread(ConnectionContext* pContext)
{
    pContext->read_thread = std::thread([pContext]() {
        ConnectionContext* context = pContext;
        MyQueue* pQueue = context->queue.get();
        int connfd = context->connfd;
        
        shared_ptr<unsigned char[]> pRecvbuf(new unsigned char[2048]);
        int times = 0;

        while(context->is_connection_alive) {
            int len = read(connfd, pRecvbuf.get(), 2048);
            if(len < 0) {
                printf("%s\n", strerror(errno)); 
                if(errno == EBADF) {
                    printf("Socket 通道已经关闭 无需处理\n");
                }
                else {
                    printf("Socket Read出错\n");
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
        }
    });
    
    pContext->read_thread.detach();
}

static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd, int& model_script_channel)
{
    printf("接收到B351 05EF\n");
    struct ProtocolB351 *pB351 = (struct ProtocolB351*)pBuffer;
    model_script_channel = pB351->channelNo;
    int packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);
    printf("PacketLen : %d \n", packetLen);

    vector<bool> recvStatus(packetLen, false);
    unsigned char* pPhotoBuffer = (unsigned char*)malloc( 1024 * packetLen);
    if (!pPhotoBuffer) return -1;
    
    int PhotoFileSize = 0;
    
    SendProtocolB352(fd);
    printf("已发送B352 06EF \n");
    
    auto start = std::chrono::high_resolution_clock::now();
    u_int8 frameType,packetType;
    int index = 0;
    
    MyQueue* pQueue = get_connection_queue(fd);
    if (!pQueue) {
        printf("错误：找不到连接 %d 的队列\n", fd);
        free(pPhotoBuffer);
        return -1;
    }
    
    ConnectionContext* context = find_connection_by_fd(fd);
        if (!context) {
        printf("错误：找不到连接 %d 的上下文\n", fd);
        free(pPhotoBuffer);
        return -1;
    }
    
    do {
        do{
            unique_ptr<Packet_t> pPacket(new Packet_t);
            int ret = run_protocol_resolver(fd, pPacket.get(), pQueue, &context->is_connection_alive);
            if (ret < 0) {
                printf("接收数据失败，错误码: %d\n", ret);
                if (ret == -1) {
                    printf("连接已断开，停止接收图片\n");
                }
                free(pPhotoBuffer);
                return -1;
            }
            
            getFramePacketType(pPacket->packetBuffer, &frameType, &packetType);
            index ++;
            
            if(frameType==0x05 && packetType == 0xF0) {
                ProtocolPhotoData *pPhotoPacket = (ProtocolPhotoData *)pPacket->packetBuffer;
                u_int16 subpacketNo = pPhotoPacket->subpacketNo;
                
                int dataLen = pPacket->packetLength - 33; 
                if (dataLen > 0 && dataLen <= 1024) {
                     memcpy(pPhotoBuffer+subpacketNo*1024, pPhotoPacket->sample, dataLen);
                     PhotoFileSize += dataLen;
                     recvStatus[subpacketNo] = true;
                } else if (dataLen > 1024) {
                     printf("Error: Data length > 1024\n");
                }
            }
        }while(!(frameType==0x05 && packetType == 0xF1));
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        printf("收到B37协议 出循环 通道号为%d, 总共用时%f 图片接收完毕，无需补包！\n", model_script_channel, elapsed.count());
    }while(0);
    
    int cnt = 0;
    for(size_t i=0; i<recvStatus.size(); i++) {
        if(!recvStatus[i]) {
            printf("%zu包,缺失\n",i);
            cnt ++;
        }
    }
    printf("缺失包数为 %d \n", cnt);

    char filename[100];
    sprintf(filename,"model_CRM_V1_2048x2448.engine");
    SaveFile(filename, pPhotoBuffer, PhotoFileSize);

    SendProtocolB38(fd);
    printf("图片大小 %d, 已发送B38 \n", PhotoFileSize);
    free(pPhotoBuffer);
    return 0;
}

void recv_model(int connfd, int& model_script_channel) {
    ConnectionContext* context = create_connection_context(connfd);
    printf("新客户端线程启动...\n");
    mv_sleep(200);
    StartReadThread(context);   
    while (1) {
        Packet_t* pPacket = new Packet_t; 
        MyQueue* pQueue = get_connection_queue(connfd);
        if (!pQueue) {
            printf("错误：找不到连接 %d 的队列\n", connfd);
            delete pPacket;
            break;
        }
        int ret = run_protocol_resolver(connfd, pPacket, pQueue, &context->is_connection_alive);
        printf("返回值%d\n", ret);
        if (-1 == ret) {
            close(connfd);
            delete pPacket;
            printf("客户端断开连接...\n");
            printf("客户端线程结束...\n");
            remove_connection_context(connfd);
            pthread_exit(NULL);
        }

        ClientFrameResolver(pPacket->packetBuffer, pPacket->packetLength, connfd, model_script_channel);
        delete pPacket;
    }
    remove_connection_context(connfd);
    close(connfd);
    printf("客户端线程结束...\n");
    pthread_exit(NULL);
}
