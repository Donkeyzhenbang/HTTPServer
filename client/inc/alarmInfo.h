#ifndef __ALARMINFO_H__
#define __ALARMINFO_H__
#include <vector>
#include <iostream>
struct alarmInfoMetaData{
    int alarmType;          //隐患目标类型
    int alarmAreaBeginX;    //起始坐标x
    int alarmAreaBeginY;    //起始坐标y
    int alarmAreaEndX;      //末端坐标x    
    int alarmAreaEndY;      //末端坐标y
    float alarmCofidence;     //置信度
    float distanceOfChan;   //距离
    float distanceOfWire;
    void debug() {
        std::cout<<"alarmType="<<alarmType<<std::endl;  
        std::cout<<"alarmAreaBeginX="<<alarmAreaBeginX<<std::endl;
        std::cout<<"alarmAreaBeginY="<<alarmAreaBeginY<<std::endl;
        std::cout<<"alarmAreaEndX="<<alarmAreaEndX<<std::endl;
        std::cout<<"alarmAreaEndY="<<alarmAreaEndY<<std::endl;
        std::cout<<"alarmCofidence="<<alarmCofidence<<std::endl;
        std::cout<<"distanceOfChan="<<distanceOfChan<<std::endl;
        std::cout<<"distanceOfWire="<<distanceOfWire<<std::endl;
    }
};
std::vector<alarmInfoMetaData> getAlarmInfo(char* jsonFileName);
#endif // !1
