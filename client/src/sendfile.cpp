#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include "sendfile.h"
#include "protocol_handler.h" // Base library
#include "utils.h"
#include "alarmInfo.h"

// Map alarm types from application specific to protocol specific
static const int AlarmMap[10] = {5, 4, 1, 2, 3, 0, 0, 7, 6, 0};

void SendImageAnalysis(int sockfd) {
    std::vector<alarmInfoMetaData> res = getAlarmInfo("../configs/TestJson.json");
    
    if(res.empty()) {
        std::cout << "Yolo图片无危险因素，无需上报" << std::endl;
        return;
    }

    std::vector<ProtocolAlarmInfo> alarms;
    alarms.reserve(res.size());
    
    for(const auto &metaData : res){
        ProtocolAlarmInfo info;
        memset(&info, 0, sizeof(info));
        
        // Mapping logic
        if (metaData.alarmType < 10 && metaData.alarmType >= 0)
            info.alarmType = static_cast<u_int8_t>(AlarmMap[metaData.alarmType]);
        else
            info.alarmType = 0; 
            
        info.alarmCofidence = static_cast<u_int8_t>(metaData.alarmCofidence);
        info.alarmAreaBeginX = static_cast<u_int8_t>(metaData.alarmAreaBeginX);
        info.alarmAreaBeginY = static_cast<u_int8_t>(metaData.alarmAreaBeginY);
        info.alarmAreaEndX = static_cast<u_int8_t>(metaData.alarmAreaEndX);
        info.alarmAreaEndY = static_cast<u_int8_t>(metaData.alarmAreaEndY);
        info.distanceOfChan = static_cast<u_int16_t>(metaData.distanceOfChan); 
        info.distanceOfWire = static_cast<u_int16_t>(metaData.distanceOfWire); 
        alarms.push_back(info);
    }

    SendProtocolB313(sockfd, alarms);
    std::cout << "发送B313图像分析帧" << std::endl;
}
