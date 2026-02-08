#include <gtest/gtest.h>
#include <chrono>
#include "protocol_handler.h"
#include "recvfile.h"

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(PerfTest, CRC_Calculation_QPS) {
    unsigned char buffer[1024];
    for(int i=0;i<1024;i++) buffer[i] = i%256;
    
    auto start = std::chrono::high_resolution_clock::now();
    int iterations = 100000;
    
    for(int i=0; i<iterations; i++) {
        GetCheckCRC16(buffer, 1024);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end-start;
    double qps = iterations / diff.count();
    
    std::cout << "CRC16 (1KB) QPS: " << qps << std::endl;
    ASSERT_GT(qps, 1000.0); // Expect at least 1000 ops/sec
}
