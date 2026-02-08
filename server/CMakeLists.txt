cmake_minimum_required(VERSION 3.10)
project(httpserver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
)

find_package(OpenSSL REQUIRED)

set(SOURCES
    src/main.cpp
    src/ServerApp.cpp
    src/http_server.cpp
    # src/heartbeat.cpp # Moved to base
    src/modelupgrade.cpp
    src/recvfile.cpp
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
