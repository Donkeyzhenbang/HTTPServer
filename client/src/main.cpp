#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <pthread.h>
#include "heartbeat.h"
#include "utils.h"
#include "sendfile.h"
#include "receive.h"

const char* NET_IP = "127.0.0.1";
// const char* NET_IP = "47.121.121.86";
static char state_grid_ip[] = "172.43.0.44";
int NET_PORT =  52487; //!单片机需要连接1037端口 两个不同的服务

const char* Photo[] = {
    // "image1.jpg",
    "../resource/photos/Send_Color.jpg",
    "../resource/photos/Send_IR.jpg",
    "../resource/photos/Send_DoLP.jpg",
    "../resource/photos/Send_Yolo.jpg"
};

void HeartBeatHandleEx(int sockfd)
{
    SendHeartbeat(sockfd);
    std::cout << "已发送心跳包 \n" ;
    waitForHeartBeat(sockfd);
    std::cout << "已收到心跳包 \n" ;
}

void SendSinglePhotoGrid(int& channelNo)
{
    std::cout << "接收通道号为 "  << channelNo << std::endl;
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP, NET_PORT);
    HeartBeatHandleEx(sockfd);
    AutoGetPhotoHander(Photo[channelNo - 1], channelNo, sockfd);
    close(sockfd);
}

void SendMultiPhotoGrid()
{
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "IP: " << NET_IP << " Port: " << NET_PORT << std::endl;
    SocketConnect(sockfd, NET_IP, NET_PORT);
    HeartBeatHandleEx(sockfd);
    mv_sleep(200);
    close(sockfd);
    for(int i = 0; i < 4; i ++){
        int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
        SocketConnect(sockfd, NET_IP, NET_PORT);
        AutoGetPhotoHander(Photo[i], i + 1, sockfd);
        mv_sleep(200);
        close(sockfd);
    }
}

void SendSinglePhotoAli(int& channelNo)
{
    std::cout << "接收通道号为 "  << channelNo << std::endl;
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP, NET_PORT);
    HeartBeatHandleEx(sockfd);
    AutoGetPhotoHander(Photo[channelNo - 1], channelNo, sockfd);
    mv_sleep(200);
    close(sockfd);
}

void SendMultiPhotoAli()
{
    for(int i = 0; i < 4; i ++){
        int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
        int status = SocketConnect(sockfd, NET_IP, NET_PORT);
        HeartBeatHandleEx(sockfd);
        AutoGetPhotoHander(Photo[i], i + 1, sockfd);
        mv_sleep(200);
        std::cout << "第" << i + 1 << "次传图完成" << std::endl;
        close(sockfd);
    }

}

void SimulateMcuAli()
{
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP, NET_PORT);
    HeartBeatHandleEx(sockfd);
    int channelNo = waitForB341(sockfd);
    if(channelNo < 0){
        std::cout << "B341解析失败" << std::endl;
        return;
    }
    SendProtocolB342(sockfd);
    AutoGetPhotoHander(Photo[channelNo - 1], channelNo, sockfd);
    //! 模拟单片机
    // sleep(100);
    close(sockfd);
}

void SimulateMcuSleepAli()
{
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP, NET_PORT);
    HeartBeatHandleEx(sockfd);
    sleep(5);
    // SendHeartbeat(sockfd);
    // std::cout << "已发送心跳包 \n" ;
    // sleep(5);
    // SendHeartbeat(sockfd);
    // std::cout << "已发送心跳包 \n" ;
    waitForB341(sockfd);
    SendProtocolB342(sockfd);
    //! 模拟单片机睡眠重连
    sleep(100);
    close(sockfd);
}

void SimulateModelUpgrade()
{
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    // int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    // SocketConnect(sockfd, NET_IP, NET_PORT);
    // HeartBeatHandleEx(sockfd);
    // int ch = waitForB351(sockfd);

    // 接收完引擎文件 存在 ../resource/engines/model... 
    // 调用脚本文件 完成 将客户端接收的model拷贝到 指定路径 并执行make clean && make -j
    std::string engine_file = "../resource/engines/model.engine";
    std::string target_dir = "../../tools";
    
    // 调用脚本文件 完成 将客户端接收的model拷贝到 指定路径 
    std::string command = "../scripts/update_model.sh \"" + engine_file + "\" \"" + target_dir + "\"";
    system(command.c_str());
    
    // close(sockfd);
}

int get_local_ip()
{
    FILE* pipe = popen("sh ../scripts/get_local_ip.sh", "r");
    if (!pipe)
    {
        std::cerr << "无法执行脚本" << std::endl;
        return -1;
    }
    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe)!= NULL)
    {
        output += buffer;
    }
    pclose(pipe);

    // 分割获取的字符串为local_ip和external_ip
    std::stringstream ss(output);
    std::string local_ip;
    std::getline(ss, local_ip);
    if(local_ip == "10.100.75"){ //只判断前24位
        NET_IP = state_grid_ip;
        NET_PORT = 28084;
        std::cout << "国网专属4G卡， 已更换IP为" << NET_IP << "端口号为" << NET_PORT << std::endl;
    }
    else{
        std::cout << "MV自用测试卡，连接阿里云服务器" << NET_IP << "端口号为" << NET_PORT << std::endl;
    }
    return 0;
}



int main(int argc, char* argv[])
{
    // freopen("output.log", "a", stdout);  // 输出到日志文件
    //!完整流程
    get_local_time();
    int ret = get_local_ip();
    if(strcmp(NET_IP, "8.148.67.175") == 0 || strcmp(NET_IP, "127.0.0.1") == 0 || strcmp(NET_IP, "47.121.121.86") == 0){
            std::cout << "正在向阿里云服务器传图 \n";
            measure_time_func([&](){
            int channelNo = std::stoi(argv[1]); //orin nano与stm32通信得到通道号
            if(1 <= channelNo && channelNo <= 4) //通道1至通道6 按需要传图
                SendSinglePhotoAli(channelNo);
            else if(channelNo == 5) //通道号为7时 全部上图信号
                SendMultiPhotoAli();
            else if(channelNo == 6)
                SimulateMcuAli();
            else if(channelNo == 7)
                SimulateMcuSleepAli();
            else if(channelNo == 8)
                SimulateModelUpgrade();
        },"传图时间");
    }
    else if(strcmp(NET_IP, "172.43.0.44") == 0){
        std::cout << "正在向国网统一视频平台传图 \n";
        measure_time_func([&](){
            int channelNo = std::stoi(argv[1]); //orin nano与stm32通信得到通道号
            if(1 <= channelNo && channelNo <= 4) //通道1至通道6 按需要传图
                SendSinglePhotoGrid(channelNo);
            else if(channelNo == 5) //通道号为7时 全部上图信号
                SendMultiPhotoGrid();
        },"传图时间");
    }
    
    return 0;
}
