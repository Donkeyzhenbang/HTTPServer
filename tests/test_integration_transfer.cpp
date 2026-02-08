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
#include "ServerApp.h"
#include "ClientApp.h"

namespace fs = std::filesystem;

// Helper to create directories and return expected file size
size_t create_test_env(const std::string& source_img_path) {
    system("mkdir -p ../configs");
    system("mkdir -p ../resource/photos");
    system("mkdir -p ../resource/uploads");

    // Create config.json with PORT 52487 (Server Default)
    std::ofstream config("../configs/config.json");
    if(config.is_open()) {
        config << "{\n"
               << "  \"network\": {\n"
               << "    \"default_ip\": \"127.0.0.1\",\n"
               << "    \"default_port\": 52487\n"
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

    // 1. Start Server Logic
    // We run the actual ServerApp which uses Reactor/EventLoop
    std::thread server_thread([](){
        ServerApp::getInstance().run();
    });
    server_thread.detach(); // Allow it to run in background (EventLoop blocks)

    // Give server time to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 2. Start Client Thread
    // ClientApp connects to localhost:52487 based on config.json
    std::thread client_thread([]() {
        ClientApp::getInstance().run(1);
    });

    // 3. Wait for client to completion
    if(client_thread.joinable()) {
        client_thread.join();
    }

    // Stop EventLoop (won't unblock accept wait immediately, but cleans flag)
    ServerApp::getInstance().getEventLoop().Stop();
    
    // 4. Verify File Transfer
    // Check if file exists in uploads that contains "test_img"
    bool found = false;
    std::string upload_path = "../resource/uploads";
    
    // Give OS time to flush writes if any
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if(fs::exists(upload_path)) {
        for(const auto& entry : fs::directory_iterator(upload_path)) {
            std::string name = entry.path().filename().string();
            // Server renames file to chX_timestamp.jpg (e.g. ch1_2024...)
            // So we check for .jpg and correct size
            if(name.find(".jpg") != std::string::npos) {
                found = true;
                std::cout << "Found uploaded file: " << name << " Size: " << entry.file_size() << std::endl;
                // Verify size
                if (entry.file_size() == expected_size) {
                    // Pass
                } else {
                    std::cout << "Size mismatch! Expected " << expected_size << " Got " << entry.file_size() << std::endl;
                }
                EXPECT_EQ(entry.file_size(), expected_size);
                
                // Cleanup
                fs::remove(entry.path());
                break;
            }
        }
    }
    
    EXPECT_TRUE(found) << "Analysis: Did not find uploaded .jpg in " << upload_path;
}
