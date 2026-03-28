cmake_minimum_required(VERSION 3.10)
project(httpserver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add base library if not already added
if(NOT TARGET base)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../base ${CMAKE_BINARY_DIR}/base_build)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
)

find_package(OpenSSL REQUIRED)

# Try to find ZooKeeper and Hiredis, or link directly (assuming standard paths or apt installed)
find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h HINTS /usr/include /usr/local/include)
find_library(ZOOKEEPER_LIB zookeeper_mt HINTS /usr/lib /usr/local/lib)

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS /usr/include /usr/local/include)
find_library(HIREDIS_LIB hiredis HINTS /usr/lib /usr/local/lib)

if (ZOOKEEPER_INCLUDE_DIR AND ZOOKEEPER_LIB)
    message(STATUS "Found ZooKeeper: ${ZOOKEEPER_INCLUDE_DIR}")
    include_directories(${ZOOKEEPER_INCLUDE_DIR})
else()
    message(WARNING "ZooKeeper not found! Distributed features may fail to compile.")
endif()

if (HIREDIS_INCLUDE_DIR AND HIREDIS_LIB)
    message(STATUS "Found Hiredis: ${HIREDIS_INCLUDE_DIR}")
    include_directories(${HIREDIS_INCLUDE_DIR})
else()
    message(WARNING "Hiredis not found! Distributed features may fail to compile.")
    # Assuming standard path if not found (e.g. /usr/include)
    include_directories(/usr/include)
endif()

find_library(ZMQ_LIB zmq HINTS /usr/lib /usr/local/lib)
if (ZMQ_LIB)
    message(STATUS "Found ZeroMQ: ${ZMQ_LIB}")
    include_directories(/usr/include)
else()
    message(WARNING "ZeroMQ not found!")
endif()

# Explicitly add inc and base/inc
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
)

set(SERVER_CORE_SOURCES
    src/ServerApp.cpp
    src/http_server.cpp
    src/HttpServer.cpp
    src/modelupgrade.cpp
    src/recvfile.cpp
    src/ZkClient.cpp
    src/RedisClient.cpp
    src/DistributedCoord.cpp
)

add_library(server_core STATIC ${SERVER_CORE_SOURCES})
target_link_libraries(server_core
    PRIVATE 
        base
        pthread
        OpenSSL::Crypto
        ${ZOOKEEPER_LIB}
        ${HIREDIS_LIB}
        ${ZMQ_LIB}
)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        server_core
        base
        pthread
        OpenSSL::Crypto
        ${ZMQ_LIB}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

target_compile_options(${PROJECT_NAME} PRIVATE -w -O3 -march=native)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${PROJECT_NAME} PRIVATE -g)
endif()
