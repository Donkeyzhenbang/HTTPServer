cmake_minimum_required(VERSION 3.10)
project(ImageSend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add base library if not already added
if(NOT TARGET base)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../base ${CMAKE_BINARY_DIR}/base_build)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsondist/json
)

find_package(OpenSSL REQUIRED)

set(CLIENT_CORE_SOURCES
    src/ClientApp.cpp
    src/config.cpp
    src/alarminfo.cpp
    src/receive.cpp
    src/sendfile.cpp
    src/clientmodelupgrade.cpp
    third_party/jsondist/jsoncpp.cpp
)

add_library(client_core STATIC ${CLIENT_CORE_SOURCES})
target_link_libraries(client_core
    PRIVATE 
        base
        pthread
        OpenSSL::Crypto
)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        client_core
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
