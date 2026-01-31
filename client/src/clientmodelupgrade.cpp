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
#include "utils.h"
#include "heartbeat.h"
#include "sendfile.h"
#include "receive.h"
#include "clientmodelupgrade.h"

#define CMD_ID "10370000123456789"  // CMD_ID，17位编码
#define PIC_ID 0x0001               // 图片ID，4字节

using namespace std;
static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd, int& model_script_channel);


typedef struct Packet_t {
    int packetLength;
    unsigned char packetBuffer[2048];
}Packet_t;
/**
 * @brief 回调函数结构体
 * 
 */
struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd, int& model_script_channel);
};

/**
 * @brief 回调函数注册表
 * 
 */
static struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, NULL},  //ProtocolB341的处理函数 这里后面需要修改：接收B342协议进行解析
    {.frameType = 0x06, .packetType = 0xEF, NULL},
    {.frameType = 0x05, .packetType = 0xEF, RecvFileHandler} //这里开始处理接收图片
};

/**
 * @brief 定义接收队列需要加锁：read线程一直在push；主线程在pop
 * 
 */
class MyQueue{
    queue<unsigned char> que;
    pthread_spinlock_t spin;//定义自旋锁
public:
    MyQueue() {
        pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    }
    ~MyQueue() {
        pthread_spin_destroy(&spin);
    }
    size_t size(void) {
        size_t size = 0;
        pthread_spin_lock(&spin);
        size = que.size();
        pthread_spin_unlock(&spin);
        return size;
    }
    void push(const unsigned char &__x) {
        pthread_spin_lock(&spin);
        que.push(__x);
        pthread_spin_unlock(&spin);
    }
    void pop() {
        pthread_spin_lock(&spin);
        que.pop();
        pthread_spin_unlock(&spin);
    }
    unsigned char front() {
        unsigned char x = 0;
        pthread_spin_lock(&spin);
        x = que.front();
        pthread_spin_unlock(&spin);
        return x;
    }
};
static MyQueue que_buf;//所有的都会从这过



int sFrameResolver(unsigned char* pBuffer, int Length ,int sockfd, int& model_script_channel)
{
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    //根据帧类型和包类型，调用不同的处理函数
    //查表，然后处理吗？
    for(int i=0;i<sizeof(Handlers)/sizeof(HandlerFun);i++) {
        if(Handlers[i].frameType == frameType && Handlers[i].packetType == packetType) {   
            if(Handlers[i].func!=NULL) {
                Handlers[i].func(pBuffer,Length,sockfd, model_script_channel);
            }
            return 0;
        }
    }
    //没找到，处理
    printf("未处理协议 frameType = 0x%x, packetType = 0x%x\n",frameType,packetType);
    return -1;
}

/**
 * @brief 开始TCP客户端接收线程
 * 
 * @param pSocketId 
 */
void StartReadThread(int* pSocketId, pthread_t* tId)
{
    GlobalFlag = false;
    // pthread_t tId;
    int ret = pthread_create(tId,NULL,[](void*sockid)->void* {
        //使用智能指针管理
        shared_ptr<unsigned char[]> pRecvbuf(new unsigned char[2048]);
        int times = 0;

        // 使子线程响应取消请求
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
        while(1) {
            int fd = *(int*)sockid;
            int len = read(fd, pRecvbuf.get(), 2048);
            if(len < 0) {
                //出错
                printf("%s\n", strerror(errno)); 
                if(errno == EBADF) {
                    printf("Socket 通道已经关闭 无需处理\n");
                }
                else {
                    printf("Socket Read出错\n");
                }
                return (void*) 0;
                break;
            }
            else if (len == 0){
                printf("客户端关闭连接\n");
                sleep(3);
                exit(EXIT_FAILURE);
                return (void*)0;
            }
            for(int i=0;i<len;i++) {
                que_buf.push(pRecvbuf.get()[i]);
            }
            times ++;
            if(times%100==0){
                // printf("完成一次载入,times==%d\n",times);
            }
            // 检查是否有取消请求
            pthread_testcancel();
        }
        return (void*) 0;
    }, pSocketId);
    if(ret){
        //出错
        return;
    }
    pthread_detach(*tId);
}

/**
 * @brief 读socket，必须解析出一个包，才能够返回，否则就会一直阻塞在这里运行。
 * 
 * @param sockfd 
 * @param pPacket 
 * @return int 返回0代表正常，其他代表错误
 */
int recv_and_resolve(int sockfd, Packet_t* pPacket)
{
    enum Rx_Status {Rx_Sync_1,Rx_Sync_2,Rx_Length_1,Rx_Length_2,Rx_Data,Rx_End};
    enum Rx_Status rx_status = Rx_Sync_1;
    unsigned short DataSize;//应接收的数据字节
    unsigned short RxDataNum = 0;//已接收的数据字节
    bool packetBufferStatus = false;//
    int cnt = 0;
    while(!packetBufferStatus){
        
        int size = que_buf.size();
        // printf("管道里还有多少字节%d\n",size);
        if(size == 0) {
            // sleep(1);
            struct timespec req = {0, 200000000L}; // 100 毫秒
            nanosleep(&req, (struct timespec *)NULL);
            cnt ++;
        }
        if(cnt >= 30) //!注意这里死循环 会造成bug:客户端主动断联服务器对应线程无法退出
            return -1;
        for(int i=0;i<size;i++) {
            unsigned char rx_temp = que_buf.front();
            que_buf.pop();
            switch(rx_status){
                case Rx_Sync_1://接收包头
                    rx_status = rx_temp == 0xA5 ? Rx_Sync_2:Rx_Sync_1;
                    pPacket->packetBuffer[0] = rx_temp;
                    break;
                case Rx_Sync_2:
                    rx_status = rx_temp == 0x5A ? Rx_Length_1:Rx_Sync_1;
                    pPacket->packetBuffer[1] = rx_temp;
                    break;
                case Rx_Length_1://收到
                    DataSize = rx_temp;
                    rx_status = Rx_Length_2;
                    pPacket->packetBuffer[2] = rx_temp;
                    break;
                case Rx_Length_2:
                    DataSize += rx_temp*256 + 27;//!注意这里分包也要对包长进行处理更改
                    pPacket->packetLength =  DataSize;
                    DataSize -= 5;
                    rx_status = Rx_Data;
                    pPacket->packetBuffer[3] = rx_temp;
                    // printf("DataSize : %d \n", DataSize);
                    RxDataNum = 0;
                    break;
                case Rx_Data:
                    ++ RxDataNum;
                    pPacket->packetBuffer[3+RxDataNum] = rx_temp;
                    // printf("RxDataNum : %d \n", RxDataNum);  
                    rx_status =  RxDataNum == DataSize?Rx_End:Rx_Data;
                    break;
                case Rx_End:
                    // printf("RxDataNum : %d \n", RxDataNum);   
                    pPacket->packetBuffer[3+RxDataNum+1] = rx_temp;
                    if( rx_temp != 0x96 ) {
                        printf("中间错误\n");
                        return -2;
                    }else{
                        packetBufferStatus = true;
                    }
                    rx_status = Rx_Sync_1;
                    i = size;//退出循环
                    break;
            }
        }
        if(packetBufferStatus) {//调试接口
            // packetBufferStatus = 0;
            // printf("完成分包\n");
            int ret = CheckFrameFull(pPacket->packetBuffer,pPacket->packetLength);
            // deBugFrame(pPacket->packetBuffer,pPacket->packetLength);
            if(-1 == ret) {
                printf("校验错误\n");
                deBugFrame(pPacket->packetBuffer,pPacket->packetLength);
                return -3;
            }
            // printf("退出分包程序\n");
        }
    }
    return 0;
}



void ProtocolB352FrameInit(ProtocolB352& frameData)
{
    frameData.sync = 0x5AA5;
    frameData.packetLength = 0x0001;
    // cmdId 通过 memcpy 设置，这里跳过
    frameData.frameType = 0x06;
    frameData.packetType = 0xEF;
    frameData.frameNo = 0x00;
    frameData.uploadStatus = 0xFF;
    frameData.CRC16 = 0;
    frameData.End = 0x96;
    
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.CRC16 = GetCheckCRC16((unsigned char*)(&frameData.packetLength), sizeof(frameData) - 5);
}



int SendProtocolB352(int socket)
{
    ProtocolB352 frameData;
    ProtocolB352FrameInit(frameData);
    int ret = write(socket, &frameData, sizeof(frameData));
    fsync(socket);
    return ret;
}

void ProtocolB38FrameInit(ProtocolB38& frameData)
{
    frameData.sync = 0x5AA5;
    frameData.packetLength = 0x000B;
    // cmdId 通过 memcpy 设置，这里跳过
    frameData.frameType = 0x06;
    frameData.packetType = 0xF2;
    frameData.frameNo = 0x80;
    frameData.channelNo = 0x01;
    frameData.ComplementPackSum = 0x00;
    frameData.CRC16 = 0;
    frameData.End = 0x96;
    memcpy(frameData.cmdId, CMD_ID, sizeof(frameData.cmdId));
    frameData.CRC16 = GetCheckCRC16((unsigned char*)(&frameData.packetLength), sizeof(frameData) - 5);
}

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
 * @brief 接收到0x05EF
 * 
 * @param pBuffer 
 * @param Length 
 * @param fd 
 * @return int 
 */
static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd, int& model_script_channel)
{
    printf("接收到B351 05EF\n");
    //解析0x05EF帧，通道号，图像总包数
    struct ProtocolB351 *pB351 = (struct ProtocolB351*)pBuffer;
    //解析出来的通道号
    model_script_channel = pB351->channelNo; //通道号 模型升级 11多曝光融合 22弱光增强 33异物检测 44yolo 55去雾
    int packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);//总包数
    printf("PacketLen : %d \n", packetLen);

    //动态内存管理
    vector<bool> recvStatus(packetLen, false);
    unsigned char* pPhotoBuffer = (unsigned char*)malloc( 1024 * packetLen);//每个包里面，最多携带1k的数据
    int PhotoFileSize = 0;
    static int pic_num = 0;
    //回复07EF
    SendProtocolB352(fd);
    printf("已发送B352 06EF \n");
    auto start = std::chrono::high_resolution_clock::now();
    //循环接收，直到收到05F1
    u_int8 frameType,packetType;
    //填入指针
    int index = 0;
    do {
        do{
            // Packet_t* pPacket = new Packet_t; 
            unique_ptr<Packet_t> pPacket(new Packet_t);//需要这种方式进行初始化
            // unique_ptr<Packet_t> pPacket = make_unique<Packet_t>();//C++14，才有make_unique

            recv_and_resolve(fd,pPacket.get());
            //从包中解析
            getFramePacketType(pPacket->packetBuffer, &frameType, &packetType);
            // printf("frameType=0x%x, packetType=0x%x\n",frameType,packetType);
            index ++;
            // printf("index = %d\n",index);
            //如果为05F0，则往图像缓冲区里写东西，下次循环再继续
            if(frameType==0x05 && packetType == 0xF0) {
                //解析、子包包号
                ProtocolPhotoData *pPhotoPacket = (ProtocolPhotoData *)pPacket->packetBuffer;
                //图像数据段的长度，
                u_int16 subpacketNo = pPhotoPacket->subpacketNo;
                //把东西拷贝到图像缓冲区
                memcpy(pPhotoBuffer+subpacketNo*1024,pPhotoPacket->sample,pPacket->packetLength-32-8); //!这里第一个参数是缓冲区内存 1032会溢出
                PhotoFileSize += pPacket->packetLength - 32 - 8; //!注意这里图片大小

                // printf("PhotoFileSize :%d, pPacket->packetLength : %d, 包数%d \n", PhotoFileSize, pPacket->packetLength, pic_num++);
                recvStatus[subpacketNo] = true;
            }
            //如果为0x05F1，则退出这个循环了
            if(frameType==0x05 && packetType == 0xF1) {
                //这里暂不处理
            }
            // delete pPacket;
        }while(!(frameType==0x05 && packetType == 0xF1));//当收到发送结束图片
        //补包流程
        //检查
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        printf("收到B37协议 出循环 模型通道号为%d, 总共用时%f 图片接收完毕，无需补包！\n", model_script_channel, elapsed.count());
    }while(0);//当检查图片完整通过，退出
    int cnt = 0;
    for(int i=0;i<recvStatus.size();i++) {
        if(!recvStatus[i]) {
            printf("%d包,缺失\n",i);
            cnt ++;
        }
    }
    printf("缺失包数为 %d \n", cnt);
    //图片名缓冲
    char filename[100];
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::time_t now_time_t = static_cast<std::time_t>(seconds);
    std::tm* local_time = std::localtime(&now_time_t);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y%m%d-%H%M%S", local_time);
    sprintf(filename,"model_CRM_V1_2048x2448.engine");
    //写入图片
    SaveFile(filename, pPhotoBuffer, PhotoFileSize);//这里欠缺一个文件大小
    // mv_sleep(250);//延时保证图片可以顺利读取
    SendProtocolB38(fd);
    printf("图片大小 %d, 已发送B38 \n", PhotoFileSize);
    //结束
    //释放图像缓存
    free(pPhotoBuffer);
    GlobalFlag = true;
    return 0;
}


void recv_model(int connfd, int& model_script_channel) {
    pthread_t read_thread;
    printf("新客户端线程启动...\n");
    mv_sleep(200);
    StartReadThread(&connfd, &read_thread);   
    while (1) {
        Packet_t* pPacket = new Packet_t; 
        int ret = recv_and_resolve(connfd, pPacket);
        printf("返回值%d\n", ret);
        if (-1 == ret) {
            close(connfd);
            delete pPacket;
            printf("客户端断开连接...\n");
            printf("客户端线程结束...\n");

            pthread_cancel(read_thread);
            pthread_exit(NULL);
        }

        sFrameResolver(pPacket->packetBuffer, pPacket->packetLength, connfd, model_script_channel);
        delete pPacket;

        if (GlobalFlag)
            break;

    }
    pthread_cancel(read_thread);
    close(connfd);
    printf("客户端线程结束...\n");
    pthread_exit(NULL);
}