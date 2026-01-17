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
        ${JSONCPP_LIBRARIES}
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