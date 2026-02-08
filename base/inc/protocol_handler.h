#ifndef __BASE_PROTOCOL_HANDLER_H_
#define __BASE_PROTOCOL_HANDLER_H_

#include "protocol.h"
#include "utils.h" // For u_int8 types
#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>
#include "connection.h" // For MyQueue class

// Function signatures for sending protocols
// Client side mainly uses B342, B351, B37, PhotoData
// Server side mainly uses B341, B352, B38

// Common constants
#define CMD_ID_DEFAULT "10370000123456789"
#define PIC_ID_DEFAULT 0x0001

// Initialization helpers
void ProtocolB341FrameInit(struct ProtocolB341 &frameData, int channelNo);
void ProtocolB342FrameInit(struct ProtocolB342 &frameData);
void ProtocolB351FrameInit(struct ProtocolB351 &frameData, u_int8 channelNo, u_int16 packetLen);
void ProtocolB352FrameInit(struct ProtocolB352 &frameData);
void ProtocolB37FrameInit(struct ProtocolB37 &frameData, unsigned char * pBuffer, int Length, u_int8 channelNo);
void ProtocolB38FrameInit(struct ProtocolB38 &frameData);

// B313 Helper
// Note: Takes raw AlarmInfo structs. Higher level constructs like alarmInfoMetaData should be converted before calling.
// Returns the constructed byte buffer and size.
// Caller must free the buffer.
void ProtocolB313Construct(const std::vector<ProtocolAlarmInfo>& alarms, unsigned char** outBuffer, int* outSize);

// Sending functions (Socket Write)
int SendProtocolB341(int socket, int channelNo);
int SendProtocolB342(int socket);
int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen);
int SendProtocolB352(int socket);
int SendProtocolB37(int socket, unsigned char* pBuffer, int Length, u_int8 channelNo);
int SendProtocolB38(int socket);

// New Sends
int SendProtocolB313(int socket, const std::vector<ProtocolAlarmInfo>& alarms);
int SendPhotoData(int SocketFd, unsigned char* pBuffer, int Length, int channelNo);

// Generic Wait
// Returns 0 on success, <0 on error.
// Fills outStruct if provided and checks size.
int WaitForProtocol(int sockfd, u_int8 expectedFrameType, u_int8 expectedPacketType, void* outStruct, int structSize);

// Generic Receive Packet (Blocking)
// Returns 0 on success, <0 on error.
int RecvPacketBlocking(int sockfd, Packet_t* outPacket);

// Queue based resolver (for threaded/async scenarios)
#include <queue>
#include <atomic>
#include <mutex>
// Need MyQueue defined. It is in connection.h
#include "connection.h" 
int run_protocol_resolver(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive);

#endif // __BASE_PROTOCOL_HANDLER_H_
