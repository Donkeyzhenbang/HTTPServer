#ifndef __BASE_UTILS_H_
#define __BASE_UTILS_H_

#include <cstdint>
#include <functional>
#include <memory>
// Defines for u_int types if not available, but usually sys/types.h has them in Linux
#include <sys/types.h>

// If u_int8 is not defined (standard C++ uses uint8_t)
// But since the project uses u_int8, we ensure they are available or typedef them 
// if we want to stick to project style or migrate. 
// For now, let's assume <sys/types.h> provides them or we typedef them to standard types.
// The user code uses u_int8, u_int16, u_int32.

typedef unsigned char u_int8;
typedef unsigned short u_int16;
typedef unsigned int u_int32;

extern u_int32 GlobalTimeStamp;
extern bool GlobalFlag;

/**
 * @brief Calculate CRC16 checksum
 */
unsigned short GetCheckCRC16(unsigned char* pBuffer, int Length);

/**
 * @brief Connect to a socket
 */
int SocketConnect(int sockfd, const char* addr, uint16_t port);

/**
 * @brief Check if a frame is full/valid (basic check)
 */
int CheckFrameFull(unsigned char* pBuffer, int Length);

/**
 * @brief Get Packet Types from buffer
 */
int getFramePacketType(unsigned char* pBuffer, u_int8 *pFrameType, u_int8 *pPacketType);

/**
 * @brief Compute MD5 of a buffer
 */
char* ComputeBufferMd5(unsigned char* pBuffer, int Length);

/**
 * @brief Debug Helper
 */
void deBugFrame(unsigned char* pBuffer, int Length);

/**
 * @brief Measure execution time of a function
 */
void measure_time_func(std::function<void(void)> func, const char* Information);

/**
 * @brief Sleep for milliseconds (or appropriate unit)
 */
void mv_sleep(int time);

/**
 * @brief Get local time
 */
void get_local_time();

/**
 * @brief Save buffer to file
 */
int SaveFile(const char *filename, unsigned char* pBuffer, size_t length);

#endif
