#include <gtest/gtest.h>
#include <thread>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vector>
#include <filesystem>

// Include necessary headers from project
#include "ClientApp.h"
#include "utils.h"
#include "recvfile.h"

namespace fs = std::filesystem;

// Helper to create directories and return expected file size
size_t create_test_env(const std::string& source_img_path) {
    system("mkdir -p ../configs");
    system("mkdir -p ../resource/photos");
    system("mkdir -p ../resource/uploads");

    // Create config.json
    std::ofstream config("../configs/config.json");
    if(config.is_open()) {
        config << "{\n"
               << "  \"network\": {\n"
               << "    \"default_ip\": \"127.0.0.1\",\n"
               << "    \"default_port\": 9988\n"
               << "  },\n"
               << "  \"photos\": {\n"
               << "      \"paths\": [\n"
               << "        \"" << source_img_path << "\",\n"
               << "        \"" << source_img_path << "\",\n"
               << "        \"" << source_img_path << "\",\n"
               << "        \"" << source_img_path << "\"\n"
               << "      ]\n"
               << "  }\n"
               << "}";
        config.close();
    }
    
    // Create Config for Alarm
    std::ofstream alarmConfig("../configs/TestJson.json");
    if(alarmConfig.is_open()) {
        alarmConfig << "[\n"
                    << "  {\"alarmType\": 0, \"alarmCofidence\": 80}\n"
                    << "]";
        alarmConfig.close();
    }

    // Check if source image exists, if so use it
    if(fs::exists(source_img_path)) {
        std::cout << "[TestEnv] Using existing image: " << source_img_path << " (" << fs::file_size(source_img_path) << " bytes)" << std::endl;
        return fs::file_size(source_img_path);
    }

    // Create dummy image (10KB) if not exists
    std::cout << "[TestEnv] Creating dummy image (10KB)..." << std::endl;
    std::ofstream img(source_img_path, std::ios::binary);
    std::vector<char> buffer(1024 * 10, 'A');
    img.write(buffer.data(), buffer.size());
    img.close();
    return 10240;
}

TEST(IntegrationTest, FullImageTransfer) {
    // Print CWD
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("Current working dir: %s\n", cwd);

    std::string img_path = "../resource/photos/test_img.jpg";
    size_t expected_size = create_test_env(img_path);

    // Debug: List files to ensure existence
    // system("ls -R ../configs ../resource");

    // 1. Setup Server Socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GT(server_fd, 0);
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(9988);

    ASSERT_EQ(bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(server_fd, 5), 0);

    // 2. Start Client Thread
    std::thread client_thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ClientApp::getInstance().run(1);
    });

    // 3. Accept Connection
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_conn = accept(server_fd, (struct sockaddr*)&client_addr, &len);
    ASSERT_GT(client_conn, 0);

    // 4. Run Server Handler in a thread to handle pthread_exit
    int* pFd = (int*)malloc(sizeof(int));
    *pFd = client_conn;
    
    std::thread server_thread(HandleClient, pFd);

    // 5. Cleanup
    if(client_thread.joinable()) {
        client_thread.join();
    }
    if(server_thread.joinable()) {
        server_thread.join();
    }
    close(server_fd);

    // 6. Verify File Transfer
    // Check if file exists in uploads that contains "test_img"
    bool found = false;
    std::string upload_path = "../resource/uploads";
    
    if(fs::exists(upload_path)) {
        for(const auto& entry : fs::directory_iterator(upload_path)) {
            std::string name = entry.path().filename().string();
            // Server renames file to chX_timestamp.jpg (e.g. ch1_2024...)
            // So we check for .jpg and correct size
            if(name.find(".jpg") != std::string::npos) {
                found = true;
                std::cout << "Found uploaded file: " << name << " Size: " << entry.file_size() << std::endl;
                // Verify size
                EXPECT_EQ(entry.file_size(), expected_size);
                
                // Cleanup
                fs::remove(entry.path());
                break;
            }
        }
    }
    
    EXPECT_TRUE(found) << "Analysis: Did not find uploaded .jpg in " << upload_path;
}
