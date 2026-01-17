## 使用方法
- 编译出可执行文件之后 ./ImageSend 1 1可更换为1~8
- 1-5 分别表示传递四个通道图片和一次性传递五张
- 6 模拟手动要图测试
- 7 模拟长时间静默连接测试
- 8 模拟模型升级测试


## crypto缺失
```sh
sudo apt-get install libssl-dev
```

## cmake替换Makefile
- 注意引入crypto库即可
- 这里第三方库引入的jsondist就是jsoncpp

```sh
# CMakeLists.txt - 最小化通用版本
cmake_minimum_required(VERSION 3.10)
project(ImageSend)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成位置无关代码（可选，便于共享库）
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 包含目录（关键修复）
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsondist/json
)

# 查找OpenSSL库
find_package(OpenSSL REQUIRED)


# 定义源文件
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/alarminfo.cpp
    src/heartbeat.cpp
    src/receive.cpp
    src/sendfile.cpp
    src/utils.cpp
    third_party/jsondist/jsoncpp.cpp
)

# 自动查找所有源文件（更灵活的方式）
# file(GLOB_RECURSE SOURCES 
#     "*.cpp"
#     "src/*.cpp"
#     "third_party/jsondist/*.cpp"
# )
# 注意：GLOB_RECURSE在添加新文件时自动包含，但CMake不会自动重新生成构建系统

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        pthread
        OpenSSL::Crypto
)



# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# 安装目标（可选）
# install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE -w)  # 禁用警告

# 添加调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${PROJECT_NAME} PRIVATE -g)
endif()
```

## 本工程中基础硬编码 

- 若修改文件架构，记得修改工程中硬编码问题

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