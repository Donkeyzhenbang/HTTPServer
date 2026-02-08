cmake_minimum_required(VERSION 3.10)
project(GWServerTests)

set(CMAKE_CXX_STANDARD 17)

# Include Google Test
# Try finding system installed GTest first
find_package(GTest)

if(GTest_FOUND)
    message(STATUS "Found GTest: ${GTest_VERSION}")
else()
    # If find_package fails, it might be source-only install (common on Ubuntu)
    if(EXISTS "/usr/src/googletest")
        message(STATUS "Found GTest source at /usr/src/googletest")
        add_subdirectory(/usr/src/googletest "${CMAKE_BINARY_DIR}/googletest-build")
        include_directories("/usr/src/googletest/googletest/include")
    elseif(EXISTS "/usr/src/gtest")
        message(STATUS "Found GTest source at /usr/src/gtest")
        add_subdirectory(/usr/src/gtest "${CMAKE_BINARY_DIR}/googletest-build")
        include_directories("/usr/src/gtest/include")
    endif()
endif()

# Enable testing
enable_testing()

# Link 'base' (we need to make sure base target exists)
# Use the same build directory for base as the tests
if(NOT TARGET base)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../base ${CMAKE_BINARY_DIR}/base_build)
endif()

# Import Server Core Library
if(NOT TARGET server_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../server ${CMAKE_BINARY_DIR}/server_build)
endif()

# Import Client Core Library
if(NOT TARGET client_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../client ${CMAKE_BINARY_DIR}/client_build)
endif()

# --- Common Protocol Tests ---
add_executable(run_protocol_tests
    test_protocol.cpp
)
target_include_directories(run_protocol_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
)
target_link_libraries(run_protocol_tests PRIVATE base GTest::gtest_main pthread)

# --- HTTP / Server Tests ---
add_executable(run_server_tests
    test_main.cpp
    test_http.cpp
)
target_include_directories(run_server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/inc
)
target_link_libraries(run_server_tests PRIVATE server_core base GTest::gtest_main pthread)

# --- Integration Tests ---
add_executable(run_integration_tests
    test_main.cpp
    test_integration_transfer.cpp
)
target_include_directories(run_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../client/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../client/third_party/jsondist/json
)
target_link_libraries(run_integration_tests PRIVATE server_core client_core base GTest::gtest_main pthread)

# --- Perf Tests ---
add_executable(run_perf_tests
    test_perf.cpp
)
target_include_directories(run_perf_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../base/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/inc
)
target_link_libraries(run_perf_tests PRIVATE server_core base GTest::gtest_main pthread)


