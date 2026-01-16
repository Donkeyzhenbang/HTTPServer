#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>  //用于 IP 地址处理和端口号等网络编程功能
#include <chrono>
#include <memory>
#include <mutex>
#include "utils.h"
#include "heartbeat.h"
#include "connection.h"

#define CMD_ID "10370000123456789"  // CMD_ID，17位编码

/**
 * @brief 心跳协议帧
 * 
 */
struct __attribute__((packed)) HeartbeatFrame {
    u_int16 sync; // 报文头：5AA5H
    u_int16 packetLength; // 报文长度
    char cmdId[17]; // CMD_ID，17位编码
    u_int8 frameType; // 帧类型，0aH
    u_int8 packetType; // 报文类型，E6H
    u_int8 frameNo; // 帧序列号
    u_int8 data; // 当前时间
    u_int16 CRC16;// 将sync到clocktimeStamp这部分进行校验
    u_int8 End;  //默认等于0x96
};

/**
 * @brief 初始化心跳帧，后面就可以拿这个心跳帧去发送了
 * 
 * @param frameData 
 */
void HeartbeatFrameInit(HeartbeatFrame &frameData)
{
    //before C++20 按名称指定结构体成员的初始化顺序
    frameData = {
        .sync = 0x5AA5,
        .packetLength = 0x0001, //31
        .cmdId = {},  //后面重新初始化
        .frameType = 0x0a,
        .packetType = 0xE6,
        .frameNo = 0x00, //0x80
        .data = 0xff, //后面重新初始化
        .CRC16 = 0,    //后面重新初始化，从packetLength开始
        .End = 0x96
    }; 
    //cmdId填充
    memcpy(frameData.cmdId, CMD_ID,sizeof(frameData.cmdId)); 
    frameData.CRC16 = GetCheckCRC16((unsigned char *)(&frameData.packetLength),sizeof(frameData)-5);
}
/**
 * @brief 
 * 
 * @param socket 
 * @return int 返回值小于零，代表发生错误。大于代表正常发送的数量
 */
int SendHeartbeat(int socket)
{
    HeartbeatFrame frameData;
    HeartbeatFrameInit(frameData);
    // deBugFrame((unsigned char*)&frameData, sizeof(frameData));
    int ret = write(socket,&frameData,sizeof(frameData));
    fsync(socket);//刷新缓冲区
    return ret;
    // return CheckFrameFull((unsigned char*)&frameData,sizeof(frameData));
}


/**
 * @brief 等待心跳包协议并处理设备ID注册
 * 
 * @param fd 连接的文件描述符
 * @return int 0: 成功接收到心跳包并注册设备ID
 *             -1: 连接断开或读取错误
 *             -2: 收到其他包或协议错误
 */
int waitForHeartBeat(int fd) 
{
    unsigned char buffer[1024];
    int len = read(fd, buffer, sizeof(buffer));
    
    if (len <= 0) {
        if (len == 0) {
            // 连接正常关闭
            std::cout << "[心跳处理] 连接正常关闭: fd=" << fd << std::endl;
        } else {
            // 读取错误
            printf("Heart Socket Read出错: fd=%d\n", fd);
        }
        
        // 从连接管理器中移除连接
        {
            std::lock_guard<std::mutex> lock(connection_manager_mutex);
            auto it = connection_manager.find(fd);
            if (it != connection_manager.end()) {
                std::string device_id = it->second->getDeviceId();
                std::cout << "[心跳处理] 移除连接: fd=" << fd 
                          << ", 设备ID=" << (device_id.empty() ? "未知" : device_id) 
                          << ", 剩余连接数: " << (connection_manager.size() - 1) << std::endl;
                connection_manager.erase(it);
            }
        }
        
        close(fd);
        return -1;
    }
    
    int ret;
    if ((ret = CheckFrameFull(buffer, len)) < 0) {
        printf("帧解析出错，不完整，错误码%d\n", ret);
        deBugFrame(buffer, len);
        return -2;
    }
    
    u_int8 frameType, packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    
    if (frameType == 0x09 && packetType == 0xE6) {
        // 解析心跳帧
        HeartbeatFrame* heartbeat_frame = reinterpret_cast<HeartbeatFrame*>(buffer);
        
        // 检查同步头（网络字节序转换）
        u_int16 sync = ntohs(heartbeat_frame->sync);
        if (sync != 0xA55A) {
            printf("无效的同步头: 0x%04X\n", sync);
            return -2;
        }
        
        // 获取设备ID
        char device_id[18] = {0};
        memcpy(device_id, heartbeat_frame->cmdId, 17);
        device_id[17] = '\0'; // 确保以null结尾
        
        printf("接收到心跳协议: fd=%d, 设备ID=%s\n", fd, device_id);
        
        // 注册设备ID到连接管理器
        {
            std::lock_guard<std::mutex> lock(connection_manager_mutex);
            auto it = connection_manager.find(fd);
            if (it != connection_manager.end()) {
                // 连接已存在，更新设备ID
                it->second->setDeviceId(device_id);
                
                // 直接计算已注册设备数（避免再次调用可能获取锁的函数）
                int registered_count = 0;
                for (const auto& pair : connection_manager) {
                    if (pair.second->hasDeviceId()) {
                        registered_count++;
                    }
                }
                
                std::cout << "[心跳处理] 设备注册成功: fd=" << fd 
                          << ", 设备ID=" << device_id 
                          << ", 当前已注册设备数: " << registered_count << std::endl;
            } else {
                // 连接不存在，先创建连接上下文
                std::cout << "[心跳处理] 警告: 连接未在管理器中，先创建: fd=" << fd << std::endl;
                connection_manager[fd] = std::make_unique<ConnectionContext>(fd);
                connection_manager[fd]->setDeviceId(device_id);
                
                // 直接计算已注册设备数
                int registered_count = 0;
                for (const auto& pair : connection_manager) {
                    if (pair.second->hasDeviceId()) {
                        registered_count++;
                    }
                }
                
                std::cout << "[心跳处理] 新设备注册成功: fd=" << fd 
                          << ", 设备ID=" << device_id 
                          << ", 当前已注册设备数: " << registered_count << std::endl;
            }
        }
        
        deBugFrame(buffer, len);
        return 0;
    }
    
    printf("收到其他包，没有收到心跳协议\n");
    return -2;
}


