#include <iostream>
#include <unistd.h>
#include <getopt.h>
#include "../inc/ServerApp.h"

// Parse Command Line Arguments
void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -p <port>        TCP listen port (default: 52487)\n"
              << "  -w <web_port>    HTTP listen port (default: 8080)\n"
              << "  -z <zk_path>     ZooKeeper connection string (e.g. 127.0.0.1:2181)\n"
              << "  -r <redis_host>  Redis host:port (e.g. 127.0.0.1:6379)\n"
              << "  -i <local_ip>    Local IP address to register (default: 127.0.0.1)\n"
              << "  -h               Show this help message\n";
}

int main(int argc, char* argv[]) {
    int tcpPort = 52487;
    int httpPort = 8080;
    std::string zkHost = "";
    std::string redisHost = "";
    std::string localIp = "127.0.0.1";

    int opt;
    while ((opt = getopt(argc, argv, "p:w:z:r:i:h")) != -1) {
        switch (opt) {
            case 'p':
                tcpPort = std::stoi(optarg);
                break;
            case 'w':
                httpPort = std::stoi(optarg);
                break;
            case 'z':
                zkHost = optarg;
                break;
            case 'r':
                redisHost = optarg;
                break;
            case 'i':
                localIp = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    std::cout << "Starting Gateway Server..." << std::endl;
    std::cout << "  TCP Port:   " << tcpPort << std::endl;
    std::cout << "  HTTP Port:  " << httpPort << std::endl;
    std::cout << "  Local IP:   " << localIp << std::endl;
    if (!zkHost.empty()) std::cout << "  ZooKeeper:  " << zkHost << std::endl;
    if (!redisHost.empty()) std::cout << "  Redis:      " << redisHost << std::endl;

    ServerApp::getInstance().Init(tcpPort, httpPort, zkHost, redisHost, localIp);
    ServerApp::getInstance().run();

    return 0;
}
