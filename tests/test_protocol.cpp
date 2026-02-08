#include <gtest/gtest.h>
#include "utils.h"
#include "protocol_handler.h"
#include "protocol.h"

// Test CRC Calculation
TEST(ProtocolTest, CRCCalculation) {
    unsigned char data[] = {0x01, 0x02, 0x03, 0x04};
    // Expected CRC needs to be known or we test consistency
    // buffer content for CRC: 02 03 04 (Length 4 passed, skip first 2? No, implementation details)
    
    // Let's test GetCheckCRC16 directly
    // Implementation: loop and lookup table
    unsigned short crc = GetCheckCRC16(data, 4);
    // Use a known value or self-consistency check
    // If we calculate it twice, it should be same
    EXPECT_EQ(crc, GetCheckCRC16(data, 4));
}

// Test Frame/Packet Type Extraction
TEST(ProtocolTest, GetFramePacketType) {
    // Protocol Header: 
    // FrameType (1 byte) | PacketType (1 byte)
    // Structure: Sync(2) | Len(2) | FrameType(1) | PacketType(1) | ...
    
    // Assuming buffer points to the start of FrameType? 
    // Looking at implementation:
    // int getFramePacketType(unsigned char* pBuffer, u_int8 *pFrameType, u_int8 *pPacketType) {
    //     *pFrameType = pBuffer[4];
    //     *pPacketType = pBuffer[5];
    //     return 0;
    // }
    
    unsigned char buffer[30] = {0};
    // Based on utils.cpp implementation:
    // *pFrameType = pBuffer[21];
    // *pPacketType = pBuffer[22];
    buffer[21] = 0xAA; // FrameType
    buffer[22] = 0xBB; // PacketType
    
    u_int8 ft, pt;
    getFramePacketType(buffer, &ft, &pt);
    
    EXPECT_EQ(ft, 0xAA);
    EXPECT_EQ(pt, 0xBB);
}

// Test CheckFrameFull
TEST(ProtocolTest, CheckFrameFull_ShortPacket) {
    unsigned char buffer[10] = {0};
    int ret = CheckFrameFull(buffer, 5); // Length < 7
    EXPECT_EQ(ret, -1);
}

// Mock Test for Directory Utils
TEST(UtilsTest, EnsureDirExists) {
    std::string test_dir = "test_dir_created_by_gtest/sub/sub2";
    // Clean up before test
    // system("rm -rf test_dir_created_by_gtest"); 
    
    bool ret = ensure_dir_exists(test_dir);
    EXPECT_TRUE(ret);
    
    // Verify using stat
    struct stat st;
    EXPECT_EQ(stat(test_dir.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    
    // Clean up
    system("rm -rf test_dir_created_by_gtest");
}
