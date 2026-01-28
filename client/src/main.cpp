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
#include "config.h"  // 添加配置头文件
#include "clientmodelupgrade.h"

// 使用配置管理器
Config& config = Config::getInstance();

// 全局变量改为从配置中获取
std::string NET_IP;
int NET_PORT;
std::string state_grid_ip;
std::vector<std::string> PhotoPaths;

void initConfiguration() {
    // 加载配置文件 优先会从配置文件中获取，不必再硬编码
    if (!config.load("../configs/config.json")) {
        std::cerr << "加载配置文件失败，使用默认值" << std::endl;
    }
    
    // 初始化配置变量
    NET_IP = config.getString("network.default_ip", "127.0.0.1");
    NET_PORT = config.getInt("network.default_port", 52487);
    state_grid_ip = config.getString("network.state_grid_ip", "172.43.0.44");
    PhotoPaths = config.getPhotoPaths();
    std::cout << PhotoPaths[0] << std::endl;
    
    // 确保至少有4个照片路径
    if (PhotoPaths.size() < 4) {
        PhotoPaths = {
            "../resource/photos/Send_Color.jpg",
            "../resource/photos/Send_IR.jpg",
            "../resource/photos/Send_DoLP.jpg",
            "../resource/photos/Send_Yolo.jpg"
        };
    }
}

void HeartBeatHandleEx(int sockfd) {
    SendHeartbeat(sockfd);
    std::cout << "已发送心跳包 \n";
    waitForHeartBeat(sockfd);
    std::cout << "已收到心跳包 \n";
}

void SendSinglePhotoGrid(int& channelNo) {
    std::cout << "接收通道号为 " << channelNo << std::endl;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    AutoGetPhotoHander(PhotoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    close(sockfd);
}

void SendMultiPhotoGrid() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "IP: " << NET_IP << " Port: " << NET_PORT << std::endl;
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    mv_sleep(200);
    close(sockfd);
    for (int i = 0; i < PhotoPaths.size(); i++) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
        AutoGetPhotoHander(PhotoPaths[i].c_str(), i + 1, sockfd);
        mv_sleep(200);
        close(sockfd);
    }
}

void SendSinglePhotoAli(int& channelNo) {
    std::cout << "接收通道号为 " << channelNo << std::endl;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    AutoGetPhotoHander(PhotoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    mv_sleep(200);
    close(sockfd);
}

void SendMultiPhotoAli() {
    for (int i = 0; i < PhotoPaths.size(); i++) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        int status = SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
        HeartBeatHandleEx(sockfd);
        AutoGetPhotoHander(PhotoPaths[i].c_str(), i + 1, sockfd);
        mv_sleep(200);
        std::cout << "第" << i + 1 << "次传图完成" << std::endl;
        close(sockfd);
    }
}

void SimulateMcuAli() {
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    int channelNo = waitForB341(sockfd);
    if (channelNo < 0) {
        std::cout << "B341解析失败" << std::endl;
        return;
    }
    SendProtocolB342(sockfd);
    AutoGetPhotoHander(PhotoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    close(sockfd);
}

void SimulateMcuSleepAli() {
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    sleep(5);
    waitForB341(sockfd);
    SendProtocolB342(sockfd);
    sleep(100);
    close(sockfd);
}

void SimulateModelUpgrade() {
    std::cout << "真正执行模型升级 \n";

    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, NET_IP.c_str(), NET_PORT);
    HeartBeatHandleEx(sockfd);
    // int ch = waitForB351(sockfd);
    recv_model(sockfd);

    // 接收完引擎文件 存在 ../resource/engines/model... 
    // 调用脚本文件 完成 将客户端接收的model拷贝到 指定路径 并执行make clean && make -j


    std::string engine_file = config.getString("environment.engine_file", "../resource/engines/model_CRM_V1_2048x2448.engine");
    std::string target_dir = config.getString("environment.engine_target_dir", "../../tools");
    std::string update_script = config.getString("environment.scripts.update_model", "../scripts/update_model.sh");
    std::string command = update_script + " \"" + engine_file + "\" \"" + target_dir + "\"";
    system(command.c_str());
    
    close(sockfd);
}

int get_local_ip() {
    std::string script_path = config.getString("environment.scripts.get_local_ip", "../scripts/get_local_ip.sh");
    
    std::string command = "sh " + script_path;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "无法执行脚本: " << command << std::endl;
        return -1;
    }
    
    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        output += buffer;
    }
    pclose(pipe);

    std::stringstream ss(output);
    std::string local_ip;
    std::getline(ss, local_ip);
    
    if (config.isStateGridEnvironment(local_ip)) {
        NET_IP = state_grid_ip;
        NET_PORT = config.getInt("network.state_grid_port", 28084);
        std::cout << "国网专属4G卡，已更换IP为" << NET_IP << "端口号为" << NET_PORT << std::endl;
    } else {
        std::cout << "MV自用测试卡，连接阿里云服务器" << NET_IP << "端口号为" << NET_PORT << std::endl;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // 初始化配置
    initConfiguration();
    
    get_local_time();
    int ret = get_local_ip();
    
    std::string current_ip = NET_IP;
    std::string aliyun_ip1 = config.getString("network.aliyun_ip", "47.121.121.86");
    std::string aliyun_ip2 = "8.148.67.175";
    
    if (current_ip == aliyun_ip1 || current_ip == aliyun_ip2 || current_ip == "127.0.0.1") {
        std::cout << "正在向阿里云服务器传图 \n";
        measure_time_func([&]() {
            int channelNo = std::stoi(argv[1]);
            if (1 <= channelNo && channelNo <= PhotoPaths.size())
                SendSinglePhotoAli(channelNo);
            else if (channelNo == 5)
                SendMultiPhotoAli();
            else if (channelNo == 6) // 模拟手动要图测试
                SimulateMcuAli(); 
            else if (channelNo == 7) // 模拟长时间静默连接测试
                SimulateMcuSleepAli(); 
            else if (channelNo == 8) // 模拟模型升级测试
                SimulateModelUpgrade(); 
        }, "传图时间");
    } else if (current_ip == state_grid_ip) {
        std::cout << "正在向国网统一视频平台传图 \n";
        measure_time_func([&]() {
            int channelNo = std::stoi(argv[1]);
            if (1 <= channelNo && channelNo <= PhotoPaths.size())
                SendSinglePhotoGrid(channelNo);
            else if (channelNo == 5)
                SendMultiPhotoGrid();
        }, "传图时间");
    }
    
    return 0;
}