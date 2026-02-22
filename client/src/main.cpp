#include <iostream>
#include <string>
#include <unistd.h>
#include "../inc/ClientApp.h"

int main(int argc, char *argv[]) {
    int channel_no = 1;
    std::string ip = "127.0.0.1";
    int port = 52487;

    int opt;
    while ((opt = getopt(argc, argv, "c:i:p:")) != -1) {
        switch (opt) {
            case 'c':
                channel_no = std::stoi(optarg);
                break;
            case 'i':
                ip = optarg;
                break;
            case 'p':
                port = std::stoi(optarg);
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " -c <channel> -i <ip> -p <port>" << std::endl;
                return 1;
        }
    }
    
    // Support legacy usage "ImageSend <channel>" if no option flags used 
    if (argc == 2 && argv[1][0] != '-') {
         try {
            channel_no = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid channel number" << std::endl;
            return 1;
        }
    }

    std::cout << "Starting ClientApp with channel: " << channel_no 
              << ", Target: " << ip << ":" << port << std::endl;

    ClientApp::getInstance().setNetAddress(ip, port);
    ClientApp::getInstance().run(channel_no);

    return 0;
}
