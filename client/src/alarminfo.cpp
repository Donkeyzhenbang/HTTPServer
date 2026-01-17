#include <string>
#include <fstream>
#include <sstream>
#include "alarmInfo.h"
#include "json.h"
//读string，创建json对象，与上一个对照，这里是string到json
static Json::Value readJsonFromString(const std::string& mystr)
{
    //1.创建工厂对象
    Json::CharReaderBuilder ReaderBuilder;
    ReaderBuilder["emitUTF8"] = true;

    //2.通过工厂对象创建json阅读器对象
    std::unique_ptr<Json::CharReader> charread(ReaderBuilder.newCharReader());

    //3.创建json对象
    Json::Value root;

    //4.把字符串转变为json对象，数据写入root
    std::string strerr;
    bool isok = charread->parse(mystr.c_str(), mystr.c_str() + mystr.size(), &root, &strerr);
    if (!isok || strerr.size() != 0)//有一个为真就出错，为什么要让size等于零才不出错？
    {
        std::cout<< "json解析出错\n";
    }else
    {
        // cout << "json解析成功\n";
    }

    //5.返回有数据的json对象
    return root;
}
alarmInfoMetaData JsonValue2alarmInfo(Json::Value &root)
{
    alarmInfoMetaData res;
    res.alarmType = root["alarmType"].asInt();
    res.alarmAreaBeginX = root["alarmAreaBeginX"].asInt();
    res.alarmAreaBeginY = root["alarmAreaBeginY"].asInt();
    res.alarmAreaEndX = root["alarmAreaEndX"].asInt();
    res.alarmAreaEndY = root["alarmAreaEndY"].asInt();
    res.alarmCofidence = root["alarmCofidence"].asFloat();
    res.distanceOfChan = root["distanceOfChan"].asFloat();
    res.distanceOfWire = root["distanceOfWire"].asFloat();
    return res;
}
std::vector<alarmInfoMetaData> getAlarmInfo(char* jsonFileName)
{
    std::vector<alarmInfoMetaData>res;
    std::ifstream file(jsonFileName);  // 打开文件
    if (!file.is_open()) {
        // 处理文件打开失败的情况，如输出错误信息等
        std::cerr << "Failed to open file" << std::endl;
        return res;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();  // 将文件内容读取到字符串流中
    std::string jsonContent = buffer.str();  // 将字符串流转换为字符串
    file.close();  // 关闭文件
    Json::Value root = readJsonFromString(jsonContent);
    res = std::vector<alarmInfoMetaData>(root["alarmDetail"].size());
    for(int i =0; i<res.size();  i++) {
        res[i] = JsonValue2alarmInfo(root["alarmDetail"][i]);
        res[i].debug();
    }
    return res;
}
