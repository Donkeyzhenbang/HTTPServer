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

set(SERVER_CORE_SOURCES
    src/ServerApp.cpp
    src/http_server.cpp
    src/modelupgrade.cpp
    src/recvfile.cpp
)

add_library(server_core STATIC ${SERVER_CORE_SOURCES})
target_link_libraries(server_core
    PRIVATE 
        base
        pthread
        OpenSSL::Crypto
)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        server_core
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
