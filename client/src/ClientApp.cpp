#include "../inc/ClientApp.h"
#include "../inc/config.h"
#include "../inc/utils.h"
#include "heartbeat.h" 
#include "../inc/sendfile.h"
#include "../inc/receive.h"
#include "../inc/clientmodelupgrade.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>

// Helper wrapper for measuring time (moved from main) -> Now using utils
// static void measure_time_func... removed

// Wrapper for heartbeat with logging
static void HeartBeatHandleEx(int sockfd) {
    if (SendHeartbeatRequest(sockfd) < 0) {
        std::cerr << "发送心跳请求失败" << std::endl;
        return;
    }
    std::cout << "已发送心跳包 \n";
    
    HeartbeatFrame frame;
    int ret = ReceiveHeartbeatFrame(sockfd, &frame);
    if (ret == 0 && frame.frameType == 0x0A) {
        std::cout << "已收到心跳响应包 \n";
    } else {
        std::cout << "接收心跳响应失败或类型错误 (ret=" << ret << ") \n";
    }
}

// static void mv_sleep(int time) ... removed


ClientApp& ClientApp::getInstance() {
    static ClientApp instance;
    return instance;
}

ClientApp::ClientApp() {
    initConfiguration();
}

void ClientApp::initConfiguration() {
    Config& config = Config::getInstance();
    if (!config.load("../configs/config.json")) {
        std::cerr << "加载配置文件失败，使用默认值" << std::endl;
    }
    
    netIp = config.getString("network.default_ip", "127.0.0.1");
    netPort = config.getInt("network.default_port", 52487);
    stateGridIp = config.getString("network.state_grid_ip", "172.43.0.44");
    photoPaths = config.getPhotoPaths();
    
    if (photoPaths.size() < 4) {
        photoPaths = {
            "../resource/photos/Send_Color.jpg",
            "../resource/photos/Send_IR.jpg",
            "../resource/photos/Send_DoLP.jpg",
            "../resource/photos/Send_Yolo.jpg",
            "../resource/photos/Send_Color.jpg" 
        };
    }
    
    // Check IP logic locally or kept here?
    // main.cpp had logic to switch IP based on local environment. 
    // Ideally this should be in Config or Utils, but we'll keep it simple for now.
    // For now assuming initConfiguration sets defaults.
    // Real IP detection was in main's get_local_ip(). We should port that if needed.
}

void ClientApp::run(int channelNo) {
    Config& config = Config::getInstance();
    std::string aliyun_ip1 = config.getString("network.aliyun_ip", "47.121.121.86");
    std::string aliyun_ip2 = "8.148.67.175";
    
    // Logic to determine environment (Aliyun vs Grid)
    // Assuming simple check for now or reusing the logic if we move get_local_ip here.
    // For refactoring, let's stick to the structure.
    
    std::cout << "正在向服务器传图 (IP: " << netIp << ")\n";
    
    measure_time_func([&]() {
        if (channelNo == 5) {
            std::cout << "指令 5: 批量发送通道 1-4..." << std::endl;
            sendBatchAli(1, 4);
        }
        else if (1 <= channelNo && channelNo <= 4)
            sendSinglePhotoAli(channelNo);
        else if (channelNo == 6) 
            simulateMcuAli(); 
        else if (channelNo == 7) 
            simulateMcuSleepAli(); 
        else if (channelNo == 8) 
            simulateModelUpgrade();
    }, "总任务时间");
}

void ClientApp::sendSinglePhotoGrid(int channelNo) {
    std::cout << "接收通道号为 " << channelNo << std::endl;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, netIp.c_str(), netPort);
    HeartBeatHandleEx(sockfd);
    AutoGetPhotoHander(photoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    close(sockfd);
}

void ClientApp::sendMultiPhotoGrid() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, netIp.c_str(), netPort);
    HeartBeatHandleEx(sockfd);
    mv_sleep(200);
    close(sockfd);
    for (int i = 0; i < photoPaths.size(); i++) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        SocketConnect(sockfd, netIp.c_str(), netPort);
        AutoGetPhotoHander(photoPaths[i].c_str(), i + 1, sockfd);
        mv_sleep(200);
        close(sockfd);
    }
}

void ClientApp::sendSinglePhotoAli(int channelNo) {
    std::cout << "接收通道号为 " << channelNo << std::endl;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(SocketConnect(sockfd, netIp.c_str(), netPort) < 0) {
        std::cerr << "连接失败" << std::endl;
        close(sockfd);
        return;
    }
    HeartBeatHandleEx(sockfd);
    if (channelNo -1 < photoPaths.size())
        AutoGetPhotoHander(photoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    mv_sleep(200);
    close(sockfd);
}

void ClientApp::sendBatchAli(int start, int end) {
    for (int i = start; i <= end; i++) {
        sendSinglePhotoAli(i);
    }
}

void ClientApp::sendMultiPhotoAli() {
    for (int i = 0; i < photoPaths.size(); i++) {
        sendSinglePhotoAli(i + 1);
        std::cout << "第" << i + 1 << "次传图完成" << std::endl;
    }
}

void ClientApp::simulateMcuAli() {
    std::cout << "模拟STM32发送心跳接收B341协议 \n";
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, netIp.c_str(), netPort);
    HeartBeatHandleEx(sockfd);
    int channelNo = waitForB341(sockfd);
    if (channelNo < 0) {
        std::cout << "B341解析失败" << std::endl;
        close(sockfd);
        return;
    }
    SendProtocolB342(sockfd);
    if (channelNo - 1 < photoPaths.size())
        AutoGetPhotoHander(photoPaths[channelNo - 1].c_str(), channelNo, sockfd);
    close(sockfd);
}

void ClientApp::simulateMcuSleepAli() {
    std::cout << "模拟STM32发送心跳接收B341协议 (Long Sleep) \n";
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SocketConnect(sockfd, netIp.c_str(), netPort);
    HeartBeatHandleEx(sockfd);
    sleep(5);
    waitForB341(sockfd);
    SendProtocolB342(sockfd);
    sleep(100);
    close(sockfd);
}

void ClientApp::simulateModelUpgrade() {
    std::cout << "真正执行模型升级 \n";
    int sockfd  = socket(AF_INET, SOCK_STREAM, 0);
    int model_script_channel = 0;
    SocketConnect(sockfd, netIp.c_str(), netPort);
    HeartBeatHandleEx(sockfd);
    
    recv_model(sockfd, model_script_channel); 
    std::cout << "模型升级通道号 model_script_channel: " << model_script_channel << std::endl;
    
    // Logic from original main.cpp
    std::string command;
    bool shouldExecute = false;
    Config& config = Config::getInstance();

    if(model_script_channel == 11) {
        std::string engine_file = config.getString("environment.exposure_engine_file", "../resource/engines/model_CRM_V1_2048x2448.engine");
        std::string target_dir = config.getString("environment.exposure_engine_target_dir", "../../tools");
        std::string update_script = config.getString("environment.scripts.exposure_update_model", "../scripts/exposure_update_model.sh");
        command = update_script + " \"" + engine_file + "\" \"" + target_dir + "\"";
        shouldExecute = true;
    } else if(model_script_channel == 22) {
        std::string engine_file = config.getString("environment.semantic_engine_file", "../resource/engines/model_default.engine");
        std::string target_dir = config.getString("environment.semantic_engine_target_dir", "../../tools");
        std::string update_script = config.getString("environment.scripts.semantic_update_model", "../scripts/semantic_update_model.sh");
        command = update_script + " \"" + engine_file + "\" \"" + target_dir + "\"";
        shouldExecute = true;
    }

    if(shouldExecute) {
        std::cout << "执行命令: " << command << std::endl;
        int status = system(command.c_str());
        (void)status; // Suppress unused warning
    } else {
        std::cerr << "未知的模型升级通道: " << model_script_channel << std::endl;
    }
    
    close(sockfd);
}
