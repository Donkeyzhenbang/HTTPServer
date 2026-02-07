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
/**
 * @brief 远程图像数据报
 * 
 */
// Moved to base/inc/protocol.h


static int SampleHander(unsigned char* pBuffer, int Length, int fd);
static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd);


/**
 * @brief 回调函数结构体
 * 
 */
struct HandlerFun{
    u_int8 frameType;
    u_int8 packetType;
    int (*func)(unsigned char* pBuffer, int Length, int fd);
};

/**
 * @brief 回调函数注册表
 * 
 */
struct HandlerFun Handlers [] = {
    {.frameType = 0x07, .packetType = 0xEE, SampleHander},  //ProtocolB341的处理函数 这里后面需要修改：接收B342协议进行解析
    {.frameType = 0x06, .packetType = 0xEF, NULL},
    {.frameType = 0x05, .packetType = 0xEF, RecvFileHandler} //这里开始处理接收图片
};

/**
 * @brief 获取连接的队列
 * 
 * @param fd 连接的文件描述符
 * @return MyQueue* 队列指针，如果连接不存在返回nullptr
 */
static MyQueue* get_connection_queue(int fd) {
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (ctx) {
        return ctx->queue.get();
    }
    return nullptr;
}

// Removed static create_connection_context - using base library version
// Removed static remove_connection_context - using base library version



/**
 * @brief 等待B342协议
 * 
 * @param fd 
 * @return int 
 */
int waitForB342(int fd) 
{
    int len = read(fd,buffer,1024);
    if(len < 0) {
        //出错
        printf("B342 Read出错\n");
        return -1;
    }
    int ret;
    if((ret = CheckFrameFull(buffer, len))<0) {
        printf("[Func] waitForB342 帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(buffer,len);
        return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    if(frameType == 0x05 && packetType == 0xEE) {
        printf("[Func] waitForB342 接收到B342\n");
        deBugFrame(buffer,len);
        return 0;
    }
    printf("[Func] waitForB342 收到其他包，没有收到B342协议\n");
    return -2;
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
int recv_and_resolve(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive)
{
    enum Rx_Status {Rx_Sync_1,Rx_Sync_2,Rx_Length_1,Rx_Length_2,Rx_Data,Rx_End};
    enum Rx_Status rx_status = Rx_Sync_1;
    unsigned short DataSize;//应接收的数据字节
    unsigned short RxDataNum = 0;//已接收的数据字节
    bool packetBufferStatus = false;//
    
    // 分离两个计数器
    int packet_timeout_cnt = 0;       // 接收包过程中的超时计数（严格）
    
    while(!packetBufferStatus){
        
        // 1. 检查TCP物理连接是否已由读线程标记为断开
        if(pIsConnectionAlive != NULL && pIsConnectionAlive->load() == false) {
            printf("检测到读线程已断开连接，退出解析\n");
            return -1;
        }

        int size = pQueue->size();
        
        // 2. 队列为空时的处理逻辑
        if(size == 0) {
            struct timespec req = {0, 100000000L}; // 100 毫秒
            nanosleep(&req, (struct timespec *)NULL);

            // --- 核心修改逻辑开始 ---
            if (rx_status == Rx_Sync_1) {
                // Case A: 处于【空闲状态】 (还在等包头第一个字节)
                // 允许无限等待，或者依赖 TCP Keepalive。
                packet_timeout_cnt = 0;  // 重置超时计数器
                continue;
            }
            else {
                // Case B: 处于【接收中状态】 (已经收到了 0xA5，包收到一半卡住了)
                packet_timeout_cnt++;
                
                // 100ms * 30 = 3秒。如果3秒内没把剩下的包传完，认为坏包或客户端挂掉
                if (packet_timeout_cnt >= 30) {
                    printf("接收报文超时中断 (状态机卡在状态: %d)\n", rx_status);
                    return -1; // 报文传输中断，断开连接
                }
            }
            // --- 核心修改逻辑结束 ---
            continue;
        }

        // 3. 队列有数据，开始处理
        // 只要读到了数据，就重置由于卡顿产生的超时计数
        packet_timeout_cnt = 0;

        // 处理队列中的数据
        for(int i=0;i<size;i++) {
            unsigned char rx_temp = pQueue->front();
            pQueue->pop();
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
                    DataSize += rx_temp*256 + 27;
                    pPacket->packetLength =  DataSize;
                    DataSize -= 5;
                    rx_status = Rx_Data;
                    pPacket->packetBuffer[3] = rx_temp;
                    RxDataNum = 0;
                    break;
                case Rx_Data:
                    ++ RxDataNum;
                    pPacket->packetBuffer[3+RxDataNum] = rx_temp;
                    rx_status =  RxDataNum == DataSize?Rx_End:Rx_Data;
                    break;
                case Rx_End:
                    pPacket->packetBuffer[3+RxDataNum+1] = rx_temp;
                    if( rx_temp != 0x96 ) {
                        printf("包尾校验错误: %02X\n", rx_temp);
                        rx_status = Rx_Sync_1;
                        // 校验失败，丢弃当前包，继续接收下一个包
                        i = size;
                        continue;
                    }else{
                        packetBufferStatus = true;
                    }
                    rx_status = Rx_Sync_1;
                    i = size;
                    break;
            }
        }
        
        if(packetBufferStatus) {
            int ret = CheckFrameFull(pPacket->packetBuffer,pPacket->packetLength);
            if(-1 == ret) {
                printf("整体帧校验错误\n");
                return -2;
            }
            return 0;
        }
    }
    return 0;
}
/**
 * @brief 
 * 
 * @param pBuffer 
 * @param Length 
 * @return int 
 */
int sFrameResolver(unsigned char* pBuffer, int Length ,int sockfd)
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
 * @brief 样例处理程序 
 * 
 * @param pBuffer 
 * @param Length 
 * @param fd 
 * @return int 
 */
static int SampleHander(unsigned char* pBuffer, int Length, int fd)
{
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    printf("解析出 frameType = 0x%x, packetType = 0x%x\n",frameType,packetType);
    return 0;    
}

/****************************************************************************/

// 获取当前可执行文件所在目录，例如： /home/jym/.../bin
static std::string get_exe_dir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return "."; // fallback
    buf[len] = '\0';
    std::string full = buf;
    size_t pos = full.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return full.substr(0, pos);
}

// 递归确保目录存在（比较简单、可靠）
static bool ensure_dir_exists(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // create each component
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' || i + 1 == dir.size()) {
            std::string sub = cur;
            if (sub.size() > 1 && sub.back() == '/') sub.pop_back();
            if (sub.empty()) continue;
            if (stat(sub.c_str(), &st) == 0) continue;
            if (mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "[ensure_dir_exists] mkdir failed for " << sub << " errno=" << errno << "\n";
                return false;
            }
        }
    }
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/****************************************************************************/


/**
 * @brief 接收到0x05EF
 * 
 * @param pBuffer 
 * @param Length 
 * @param fd 
 * @return int 
 */
static int RecvFileHandler(unsigned char* pBuffer, int Length, int fd)
{
    printf("接收到B351 05EF\n");
    //解析0x05EF帧，通道号，图像总包数
    struct ProtocolB351 *pB351 = (struct ProtocolB351*)pBuffer;
    //解析出来的通道号
    int channelNo = pB351->channelNo; //通道号
    int packetLen = (pB351->packetHigh << 8) | (pB351->packetLow);//总包数
    printf("PacketLen : %d \n", packetLen);

    //动态内存管理
    vector<bool> recvStatus(packetLen, false);
    // 使用 calloc 初始化为0，防止丢包时写入垃圾数据
    unsigned char* pPhotoBuffer = (unsigned char*)calloc(packetLen, 1024);
    if (!pPhotoBuffer) {
        printf("内存分配失败\n");
        return -1;
    }
    int PhotoFileSize = 0; // 累计接收字节数
    int maxOffset = 0;     // 记录最大有效数据偏移量
    static int pic_num = 0;
    //回复07EF
    SendProtocolB352(fd);
    printf("已发送B352 06EF \n");
    auto start = std::chrono::high_resolution_clock::now();
    //循环接收，直到收到05F1
    u_int8 frameType,packetType;
    //填入指针
    int index = 0;
    
    // 获取当前连接的队列
    MyQueue* pQueue = get_connection_queue(fd);
    if (!pQueue) {
        printf("错误：找不到连接 %d 的队列\n", fd);
        free(pPhotoBuffer);
        return -1;
    }
    
    // 获取连接状态标志
    ConnectionContext* context = find_connection_by_fd(fd);
        if (!context) {
        printf("错误：找不到连接 %d 的上下文\n", fd);
        free(pPhotoBuffer);
        return -1;
    }
    
    do {
        do{

            unique_ptr<Packet_t> pPacket(new Packet_t);//需要这种方式进行初始化
            // unique_ptr<Packet_t> pPacket = make_unique<Packet_t>();//C++14，才有make_unique

            // recv_and_resolve(fd, pPacket.get(), pQueue);
            //! 这里每次解析出来一个协议包 然后进行处理 队列中剩余部分不会动与下次新读进来的组成完整的数据包
            int ret = recv_and_resolve(fd, pPacket.get(), pQueue, &context->is_connection_alive);
            // if(ret < 0) break;
            if (ret < 0) {
                // 接收失败，退出循环
                printf("接收数据失败，错误码: %d\n", ret);
                if (ret == -1) {
                    printf("连接已断开，停止接收图片\n");
                }
                free(pPhotoBuffer);
                return -1;
            }
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
                // 数据部分长度
                int dataLen = pPacket->packetLength - 32 - 8;
                
                //把东西拷贝到图像缓冲区
                if (subpacketNo < packetLen) {
                    memcpy(pPhotoBuffer + subpacketNo * 1024, pPhotoPacket->sample, dataLen);
                    PhotoFileSize += dataLen;
                    
                    int endPos = subpacketNo * 1024 + dataLen;
                    if (endPos > maxOffset) {
                        maxOffset = endPos;
                    }
                    recvStatus[subpacketNo] = true;
                } else {
                    printf("收到包号 %d 超出总包数 %d，丢弃\n", subpacketNo, packetLen);
                }
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
        printf("收到B37协议 出循环 通道号为%d, 总共用时%f 图片接收完毕，无需补包！\n", channelNo, elapsed.count());
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
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", local_time);
    
    // 修改文件名格式，与save_upload_to_web保持一致：ch{channel}_{timestamp}.jpg
    // 这样可以保证前后端统一识别通道
    sprintf(filename,"ch%d_%s.jpg", channelNo, time_str);
    
    //写入图片
    /****************************************************************************/
    // SaveFile(filename, pPhotoBuffer, PhotoFileSize);//这里欠缺一个文件大小
    // mv_sleep(250);//延时保证图片可以顺利读取
    // 构造保存目录：<exe_dir>/web/uploads
    std::string exe_dir = get_exe_dir(); // 可执行文件所在目录
    std::string upload_dir = exe_dir + "/web/uploads";

    // 确保目录存在
    if (!ensure_dir_exists(upload_dir)) {
        fprintf(stderr, "[RecvFileHandler] 无法确保目录存在: %s\n", upload_dir.c_str());
    } else {
        // 目标文件完整路径
        std::string fullpath = upload_dir + "/" + std::string(filename);

        // debug
        printf("[RecvFileHandler] saving image to: %s (received=%d, max_offset=%d, channel=%d)\n", fullpath.c_str(), PhotoFileSize, maxOffset, channelNo);

        // 以二进制方式写入文件
        std::ofstream ofs(fullpath, std::ios::binary);
        if (!ofs) {
            fprintf(stderr, "[RecvFileHandler] 写文件失败: %s, errno=%d\n", fullpath.c_str(), errno);
        } else {
            //以此处maxOffset为准，包含中间可能丢失的0填充部分
            ofs.write(reinterpret_cast<const char*>(pPhotoBuffer), (std::streamsize)maxOffset);
            ofs.close();
        }
    }


    /****************************************************************************/
    SendProtocolB38(fd);
    printf("图片大小 %d, 已发送B38 \n", PhotoFileSize);
    //结束
    //释放图像缓存
    free(pPhotoBuffer);
    // Set processing done flag for this connection
    ConnectionContext* ctx = find_connection_by_fd(fd);
    if (ctx) ctx->is_processing_done = true;
    return 0;
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
        printf("返回值%d, times = %d\n", ret, ++times);
        if (-1 == ret) {
            close(connfd);
            delete pPacket;
            printf("客户端断开连接...\n");
            printf("客户端线程结束...\n");
            remove_connection_context(connfd);
            pthread_exit(NULL);
        }

        sFrameResolver(pPacket->packetBuffer, pPacket->packetLength, connfd);
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

