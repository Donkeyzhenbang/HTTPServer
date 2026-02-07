cmake_minimum_required(VERSION 3.10)
project(ImageSend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsondist/json
)

find_package(OpenSSL REQUIRED)

set(SOURCES
    src/ClientApp.cpp
    src/main.cpp
    src/config.cpp
    src/alarminfo.cpp
    # src/heartbeat.cpp # Moved to base
    src/receive.cpp
    src/sendfile.cpp
    src/clientmodelupgrade.cpp
    third_party/jsondist/jsoncpp.cpp
)

add_subdirectory(../base base_build)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        base
        pthread
        OpenSSL::Crypto
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

target_compile_options(${PROJECT_NAME} PRIVATE -w)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${PROJECT_NAME} PRIVATE -g)
endif()
