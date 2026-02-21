#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include "protocol.h"
#include "utils.h"

// Global Stats
std::atomic<long> g_success_count(0);
std::atomic<long> g_fail_count(0);
std::atomic<bool> g_running(true);

void client_thread_func(const std::string& ip, int port, int id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    // Address struct
    serv_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Prepare packet
    HeartbeatFrame frame;
    frame.sync = 0x5AA5; // Little Endian Machine handles 0x5AA5 as A5 5A
    
    frame.packetLength = sizeof(HeartbeatFrame);
    snprintf(frame.cmdId, 17, "PERF_TEST_%06d", id);
    frame.frameType = 0x09;
    frame.packetType = 0xE6;
    frame.frameNo = 0;
    frame.clocktimeStamp = 0;
    frame.End = 0x96;

    unsigned char buffer[1024];

    while(g_running) {
        // Update Timestamp
        frame.clocktimeStamp++; 

        // Calc CRC
        unsigned char* crc_start = (unsigned char*)&frame.packetLength;
        int crc_len = sizeof(HeartbeatFrame) - 5; // Total - 2(sync) - 2(CRC) - 1(End)
        frame.CRC16 = GetCheckCRC16(crc_start, crc_len);

        if(send(sock, &frame, sizeof(frame), 0) < 0) {
            g_fail_count++;
            close(sock);
            // Reconnect
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        // Wait for response
        int bytes = recv(sock, buffer, 1024, 0);
        if(bytes > 0) {
            g_success_count++;
        } else {
            g_fail_count++;
            close(sock);
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    
    close(sock);
}

int main(int argc, char* argv[]) {
    // Default Args
    std::string ip = "127.0.0.1";
    int port = 52487;
    int threads = 10;
    int duration = 10;

    if(argc > 1) ip = argv[1];
    if(argc > 2) port = std::stoi(argv[2]);
    if(argc > 3) threads = std::stoi(argv[3]);
    if(argc > 4) duration = std::stoi(argv[4]);

    std::cout << "Starting TCP QPS Test against " << ip << ":" << port 
              << " with " << threads << " threads for " << duration << "s..." << std::endl;

    std::vector<std::thread> thread_pool;
    for(int i=0; i<threads; i++) {
        thread_pool.emplace_back(client_thread_func, ip, port, i);
    }

    auto start_time = std::chrono::steady_clock::now();
    
    // Monitor loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long count = g_success_count.exchange(0);
        long fail = g_fail_count.exchange(0);
        
        std::cout << "QPS: " << count << " req/s (Failures: " << fail << ")" << std::endl;

        auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= duration) {
            g_running = false;
        }
    }

    for(auto& t : thread_pool) {
        if(t.joinable()) t.join();
    }

    std::cout << "Test Finished." << std::endl;
    return 0;
}
