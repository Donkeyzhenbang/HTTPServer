cmake_minimum_required(VERSION 3.10)
project(base)

set(CMAKE_CXX_STANDARD 17)

# Include directories for the library itself
include_directories(inc)

# Source files
set(SOURCES
    src/utils.cpp
    src/connection.cpp
    src/heartbeat.cpp
    src/protocol_handler.cpp
)

# Header files (optional, for IDE support)
set(HEADERS
    inc/utils.h
    inc/connection.h
    inc/protocol.h
    inc/protocol_handler.h

    inc/heartbeat.h
)

# Create the static library
add_library(base STATIC ${SOURCES} ${HEADERS})

# Export include directory for consumers
target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

# Link dependencies if any (pthread, openssl/crypto)
find_package(OpenSSL REQUIRED)
target_link_libraries(base OpenSSL::Crypto pthread)
