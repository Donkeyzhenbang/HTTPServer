#include "../inc/protocol_handler.h"
#include "../inc/utils.h"
#include "../inc/heartbeat.h"
#include <sys/socket.h>
#include "../inc/connection.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <atomic>
#include <time.h>
#include <chrono>

// Helper to calculate CRC and fill the field
template<typename T>
void packet_crc_cal(T* frame) {
    frame->CRC16 = GetCheckCRC16((unsigned char *)(&frame->packetLength), sizeof(T)-5);
}

void ProtocolB341FrameInit(struct ProtocolB341 &frameData, int channelNo) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    
    // Total Size: 38 bytes
    frameData.packetLength = sizeof(ProtocolB341);
    
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x07; // 07H matches Server expectation
    frameData.packetType = 0xEE;
    frameData.frameNo = 0x00;
    frameData.channelNo = (u_int8)channelNo;
    
    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

void ProtocolB342FrameInit(struct ProtocolB342 &frameData) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    frameData.packetLength = sizeof(ProtocolB342); // 28 bytes
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x05;
    frameData.packetType = 0xEE;
    frameData.frameNo = 0x00; 
    frameData.commandStatus = 0xFF; // Success
    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

void ProtocolB351FrameInit(struct ProtocolB351 &frameData, u_int8 channelNo, u_int16 packetLen) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    frameData.packetLength = sizeof(ProtocolB351); // 38 bytes
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x05;
    frameData.packetType = 0xEF;
    frameData.frameNo = 0x00;
    frameData.channelNo = channelNo;
    frameData.packetHigh = static_cast<u_int8>((packetLen >> 8) & 0xFF);
    frameData.packetLow = static_cast<u_int8>(packetLen & 0xFF);
    
    // Reserve: PIC_ID
    frameData.reverse[0] = (char)(PIC_ID_DEFAULT & 0xFF);
    frameData.reverse[1] = (char)((PIC_ID_DEFAULT >> 8) & 0xFF);
    frameData.reverse[2] = (char)((PIC_ID_DEFAULT >> 16) & 0xFF);
    frameData.reverse[3] = (char)(PIC_ID_DEFAULT >> 24);
    
    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

void ProtocolB352FrameInit(struct ProtocolB352 &frameData) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    frameData.packetLength = sizeof(ProtocolB352); // 28 bytes
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x06; // Response
    frameData.packetType = 0xEF;
    frameData.frameNo = 0x80; 
    frameData.uploadStatus = 0xFF; // Allow/Success
    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

void ProtocolB37FrameInit(struct ProtocolB37 &frameData, unsigned char * pBuffer, int Length, u_int8 channelNo) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    frameData.packetLength = sizeof(ProtocolB37); // 65 bytes
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x05;
    frameData.packetType = 0xF1;
    frameData.frameNo = 0x80;
    frameData.channelNo = channelNo;
    
    // Time
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    frameData.timeStamp = (u_int32_t)timestamp; 

    // MD5
    char* md5Ptr = ComputeBufferMd5(pBuffer, Length);
    if(md5Ptr) {
        memcpy(frameData.MD5, md5Ptr, 32);
    }

    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

void ProtocolB38FrameInit(struct ProtocolB38 &frameData) {
    memset(&frameData, 0, sizeof(frameData));
    frameData.sync = 0x5AA5;
    frameData.packetLength = sizeof(ProtocolB38); // 38 bytes
    memcpy(frameData.cmdId, CMD_ID_DEFAULT, 17);
    frameData.frameType = 0x06; 
    frameData.packetType = 0xF2;
    frameData.frameNo = 0x80;
    frameData.channelNo = 0x01;
    frameData.ComplementPackSum = 0x00;
    
    frameData.End = 0x96;
    packet_crc_cal(&frameData);
}

// ----------------------------------------------------------------------------
// Send Implementations
// ----------------------------------------------------------------------------

int SendProtocolB341(int socket, int channelNo) {
    ProtocolB341 frameData;
    ProtocolB341FrameInit(frameData, channelNo);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    // fsync(socket); // fsync not valid for sockets
    return ret;
}

int SendProtocolB342(int socket) {
    ProtocolB342 frameData;
    ProtocolB342FrameInit(frameData);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    // fsync(socket);
    return ret;
}

int SendProtocolB351(int socket, u_int8 channelNo, u_int16 packetLen) {
    ProtocolB351 frameData;
    ProtocolB351FrameInit(frameData, channelNo, packetLen);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    // fsync(socket);
    return ret;
}

int SendProtocolB352(int socket) {
    ProtocolB352 frameData;
    ProtocolB352FrameInit(frameData);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    if(ret > 0) std::cout << "已发送B352" << std::endl;
    else std::cerr << "发送B352失败" << std::endl;
    // fsync(socket);
    return ret;
}

int SendProtocolB37(int socket, unsigned char* pBuffer, int Length, u_int8 channelNo) {
    ProtocolB37 frameData;
    ProtocolB37FrameInit(frameData, pBuffer, Length, channelNo);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    if(ret > 0) std::cout << "发送B37结束" << std::endl;
    // fsync(socket);
    return ret;
}

int SendProtocolB38(int socket) {
    ProtocolB38 frameData;
    ProtocolB38FrameInit(frameData);
    int ret = send(socket, &frameData, sizeof(frameData), MSG_NOSIGNAL);
    // fsync(socket);
    return ret;
}

// ----------------------------------------------------------------------------
// New Protocol Implementation (B313, PhotoData)
// ----------------------------------------------------------------------------

void ProtocolB313Construct(const std::vector<ProtocolAlarmInfo>& alarms, unsigned char** outBuffer, int* outSize) {
    ProtocolB313Header header;
    memset(&header, 0, sizeof(header));
    
    int alarmCount = alarms.size();
    if (alarmCount > 255) alarmCount = 255;
    
    int dataSize = alarmCount * sizeof(ProtocolAlarmInfo);
    // Header (fixed) + Data + CRC(2) + End(1)
    *outSize = sizeof(ProtocolB313Header) + dataSize + 3;
    
    *outBuffer = (unsigned char*)malloc(*outSize);
    if (!*outBuffer) { *outSize = 0; return; }
    
    unsigned char* ptr = *outBuffer;
    
    // Fill Header
    header.sync = 0x5AA5;
    header.packetLength = *outSize; 
    memcpy(header.cmdId, CMD_ID_DEFAULT, 17);
    header.frameType = 0x05;
    header.packetType = 0xFE;
    header.frameNo = 0x00;
    header.channelNo = 0x04; 
    header.prePosition = 0xFF;
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    header.timeStamp = (u_int32_t)timestamp;
    header.alarmNum = (u_int8_t)alarmCount;
    
    memcpy(ptr, &header, sizeof(header));
    ptr += sizeof(header);
    
    // Data
    if (alarmCount > 0) {
        memcpy(ptr, alarms.data(), dataSize);
        ptr += dataSize;
    }
    
    // CRC
    u_int16_t crc = GetCheckCRC16(*outBuffer + 2, *outSize - 5);
    
    *ptr = crc & 0xFF; ptr++;
    *ptr = (crc >> 8) & 0xFF; ptr++;
    *ptr = 0x96;
}

int SendProtocolB313(int socket, const std::vector<ProtocolAlarmInfo>& alarms) {
    unsigned char* buffer = nullptr;
    int size = 0;
    ProtocolB313Construct(alarms, &buffer, &size);
    if (!buffer) return -1;
    
    int ret = send(socket, buffer, size, MSG_NOSIGNAL);
    if(ret > 0) printf("发送B313图像分析帧\n");
    else perror("SendB313 failed");
    
    free(buffer);
    return ret;
}

int SendPhotoData(int SocketFd, unsigned char* pBuffer, int Length, int channelNo) {
    ProtocolPhotoData PhotoDataPacket;
    memset(&PhotoDataPacket, 0, sizeof(PhotoDataPacket));
    
    // Set Sync Word
    PhotoDataPacket.sync = 0x5AA5;
    
    memcpy(PhotoDataPacket.cmdId, CMD_ID_DEFAULT, 17);
    PhotoDataPacket.frameType = 0x05;
    PhotoDataPacket.packetType = 0xF0;
    PhotoDataPacket.channelNo = channelNo;
    PhotoDataPacket.packetNo = Length / 1024 + 1;
    
    int PacketNums = PhotoDataPacket.packetNo;
    int tailPacketDataLength = Length % 1024;
    
    for(int i=0; i <PacketNums; i++) {
        PhotoDataPacket.frameNo = i;
        PhotoDataPacket.subpacketNo = i;
        PhotoDataPacket.prefix_sample[0] = 3; 
        PhotoDataPacket.prefix_sample[1] = 1024 * i; 
        
        int write_len = 0;
        if(i != PacketNums-1) {
            write_len = sizeof(PhotoDataPacket);
            PhotoDataPacket.packetLength = sizeof(PhotoDataPacket);
            memcpy(PhotoDataPacket.sample, pBuffer + i*1024, 1024);
            packet_crc_cal(&PhotoDataPacket);
            PhotoDataPacket.End = 0x96;
        } else {
             write_len = tailPacketDataLength + 40; 
             PhotoDataPacket.packetLength = tailPacketDataLength + 40;
             memset(PhotoDataPacket.sample, 0, 1024); // clear
             memcpy(PhotoDataPacket.sample, pBuffer + i*1024, tailPacketDataLength);
             
             u_int16_t c = GetCheckCRC16((unsigned char *)(&PhotoDataPacket.packetLength), PhotoDataPacket.packetLength - 5);
             
             unsigned char* raw = (unsigned char*)&PhotoDataPacket;
             raw[write_len-3] = c & 0xFF;
             raw[write_len-2] = (c >> 8) & 0xFF;
             raw[write_len-1] = 0x96;
        }
        
        ssize_t ret = send(SocketFd, &PhotoDataPacket, write_len, MSG_NOSIGNAL);
        if(ret < 0) {
            perror("send error");
            return -1;
        }
        usleep(1000); 
    }
    printf("Length : %d, PacketNums: %d, tailPacketDataLength: %d \n",Length, PacketNums, tailPacketDataLength);
    return 0;
}

// ----------------------------------------------------------------------------
// Receive / Resolver Implementation
// ----------------------------------------------------------------------------

typedef enum 
{
    Rx_Sync_1,
    Rx_Sync_2,
    Rx_Length_1,
    Rx_Length_2,
    Rx_Data,
    Rx_End
}RxStatus;

int run_protocol_resolver(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive) {
    int i=0;
    RxStatus rx_status = Rx_Sync_1;
    u_int16_t DataSize = 0;
    u_int16_t RxDataNum = 0;
    u_int8_t rx_temp = 0;
    bool packetBufferStatus = false;
    
    if (!pPacket || !pQueue) return -1;
    
    int packet_timeout_cnt = 0;

    while(!packetBufferStatus) {        
        if(pIsConnectionAlive != NULL && pIsConnectionAlive->load() == false) {
            return -1;
        }

        int size = pQueue->size();
        if(size == 0) {
           struct timespec req = {0, 10000000L};
           nanosleep(&req, NULL);
           
            if (rx_status == Rx_Sync_1) {
                packet_timeout_cnt = 0;
            } else {
                packet_timeout_cnt++;
                if (packet_timeout_cnt >= 300) {
                    rx_status = Rx_Sync_1;
                    return -1;
                }
            }
            continue;
        }

        for(i=0; i<size; i++) {
            rx_temp = pQueue->front();
            pQueue->pop();
            
            switch(rx_status){
                case Rx_Sync_1:
                    rx_status = (rx_temp == 0xA5) ? Rx_Sync_2 : Rx_Sync_1;
                    if (rx_status == Rx_Sync_2) pPacket->packetBuffer[0] = rx_temp;
                    break;
                case Rx_Sync_2:
                    rx_status = (rx_temp == 0x5A) ? Rx_Length_1 : Rx_Sync_1;
                    if (rx_status == Rx_Length_1) pPacket->packetBuffer[1] = rx_temp;
                    break;
                case Rx_Length_1:
                    DataSize = rx_temp;
                    rx_status = Rx_Length_2;
                    pPacket->packetBuffer[2] = rx_temp;
                    break;
                case Rx_Length_2:
                    DataSize += rx_temp * 256;
                    pPacket->packetLength = DataSize;
                    
                    if (DataSize < 5 || DataSize > 2048) {
                        rx_status = Rx_Sync_1;
                        continue;
                    }
                    
                    DataSize -= 5;
                    pPacket->packetBuffer[3] = rx_temp;
                    RxDataNum = 0;
                    
                    if (DataSize == 0) rx_status = Rx_End;
                    else rx_status = Rx_Data;
                    break;

                case Rx_Data:
                    ++RxDataNum;
                    if ((3 + RxDataNum) < 2048) {
                        pPacket->packetBuffer[3+RxDataNum] = rx_temp;
                    }
                    rx_status = (RxDataNum == DataSize) ? Rx_End : Rx_Data;
                    break;
                    
                case Rx_End:
                     if ((3 + RxDataNum + 1) < 2048)
                        pPacket->packetBuffer[3+RxDataNum+1] = rx_temp;
                     
                     if( rx_temp != 0x96 ) {
                         rx_status = Rx_Sync_1;
                     } else {
                         packetBufferStatus = true;
                     }
                     rx_status = Rx_Sync_1;
                     break;
            }
            if (packetBufferStatus) break;
        }
        
        if(packetBufferStatus) {
            int ret = CheckFrameFull(pPacket->packetBuffer, pPacket->packetLength);
            if(-1 == ret) {
                packetBufferStatus = false;
                return -2;
            }
            return 0;
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Blocking Receive Implementation
// ----------------------------------------------------------------------------

int RecvPacketBlocking(int sockfd, Packet_t* outPacket) {
    if(!outPacket) return -1;
    
    RxStatus rx_status = Rx_Sync_1;
    u_int16_t DataSize = 0;
    u_int16_t RxDataNum = 0;
    u_int8_t rx_temp = 0;
    bool packetBufferStatus = false;
    
    // We loop until one full packet is read or error
    while(!packetBufferStatus) {
        ssize_t n = read(sockfd, &rx_temp, 1);
        if(n <= 0) {
             return -1; // Error or Closed
        }
        
        switch(rx_status){
            case Rx_Sync_1:
                rx_status = (rx_temp == 0x5A) ? Rx_Sync_2 : Rx_Sync_1; // CAREFUL: Protocol says Sync is 5AA5 (Low High? or High Low?)
                // Header struct: u_int16_t sync.
                // 0x5AA5. Little Endian: A5 5A. Big Endian: 5A A5.
                // Network byte order?
                // run_protocol_resolver uses:
                // case Rx_Sync_1: rx_status = (rx_temp == 0xA5) ? Rx_Sync_2 : Rx_Sync_1;
                // case Rx_Sync_2: rx_status = (rx_temp == 0x5A) ? Rx_Length_1 : Rx_Sync_1;
                // So it expects A5 then 5A. => Little Endian 0x5AA5.
                // My logic above must match.
                
                // Recorrecting logic to match run_protocol_resolver:
                rx_status = (rx_temp == 0xA5) ? Rx_Sync_2 : Rx_Sync_1;
                if (rx_status == Rx_Sync_2) outPacket->packetBuffer[0] = rx_temp;
                break;
                
            case Rx_Sync_2:
                rx_status = (rx_temp == 0x5A) ? Rx_Length_1 : Rx_Sync_1;
                if (rx_status == Rx_Length_1) outPacket->packetBuffer[1] = rx_temp;
                break;
                
            case Rx_Length_1:
                DataSize = rx_temp;
                rx_status = Rx_Length_2;
                outPacket->packetBuffer[2] = rx_temp;
                break;
                
            case Rx_Length_2:
                DataSize += rx_temp * 256;
                outPacket->packetLength = DataSize;
                
                // Sanity check
                if (DataSize < 5 || DataSize > 2048) {
                    rx_status = Rx_Sync_1;
                    continue; // Reset state
                }
                
                DataSize -= 5;
                outPacket->packetBuffer[3] = rx_temp;
                RxDataNum = 0;
                
                if (DataSize == 0) rx_status = Rx_End;
                else rx_status = Rx_Data;
                break;

            case Rx_Data:
                ++RxDataNum;
                if ((3 + RxDataNum) < 2048) {
                    outPacket->packetBuffer[3+RxDataNum] = rx_temp;
                }
                rx_status = (RxDataNum == DataSize) ? Rx_End : Rx_Data;
                break;
                
            case Rx_End:
                 if ((3 + RxDataNum + 1) < 2048)
                    outPacket->packetBuffer[3+RxDataNum+1] = rx_temp;
                 
                 if( rx_temp != 0x96 ) {
                     rx_status = Rx_Sync_1;
                 } else {
                     packetBufferStatus = true;
                 }
                 rx_status = Rx_Sync_1; // Ready for next if called again, but we return.
                 break;
        }
    }
    
    if(packetBufferStatus) {
        int ret = CheckFrameFull(outPacket->packetBuffer, outPacket->packetLength);
        if(-1 == ret) return -2;
        return 0;
    }
    
    return -1;
}

int WaitForProtocol(int sockfd, u_int8 expectedFrameType, u_int8 expectedPacketType, void* outStruct, int structSize) {
    Packet_t packet;
    // Try loop to skip heartbeats?
    int retry = 10;
    while(retry--) {
        int ret = RecvPacketBlocking(sockfd, &packet);
        if(ret < 0) return ret;
        
        u_int8 ft, pt;
        getFramePacketType(packet.packetBuffer, &ft, &pt);
        // printf("WaitForProtocol expected 0x%x 0x%x, got 0x%x 0x%x\n", expectedFrameType, expectedPacketType, ft, pt);
        
        if(ft == expectedFrameType && pt == expectedPacketType) {
             if(outStruct) {
                 if (packet.packetLength > structSize)
                    memcpy(outStruct, packet.packetBuffer, structSize);
                 else
                    memcpy(outStruct, packet.packetBuffer, packet.packetLength);
             }
             return 0;
        }
        // If Heartbeat? (0x09/0x0A, 0xE6)
        if (pt == 0xE6) {
            // Heartbeat, ignore and wait
            continue;
        }
        // Otherwise mismatch
        // return -1;
    }
    return -1;
}
