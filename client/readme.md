## 本工程中基础硬编码 
- 若修改文件架构，记得修改

```cpp
main.cpp:24
const char* Photo[] = {
    // "image1.jpg",
    "../resource/Send_Color.jpg",
    "../resource/Send_IR.jpg",
    "../resource/Send_DoLP.jpg",
    "../resource/Send_Yolo.jpg"
};

main.cpp:147
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

sendfile.cpp:555
void SendImageAnalysis(int sockfd)   //入口参数struct 
{
    //拿到json解析结果 1、施工机械 2、吊车 3、塔吊 4、验货 5、防尘网 6、人员 7、线路异物
    std::vector<alarmInfoMetaData> res = getAlarmInfo("../configs/TestJson.json");
    std::cout << "Json解析结果：隐患目标个数为 " << res.size() << std::endl;
    // for(int i = 0; i < res.size(); i ++) {
    //     res[i].debug();
    // }
    if(res.size() != 0){
        //!发送图像分析帧 已经确定非0
        SendB313Protocol(sockfd, res);
    }else{
        std::cout << "Yolo图片无危险因素，无需上报" << std::endl;
    }

}

````