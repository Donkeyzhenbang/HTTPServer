#include <iostream>
#include <string>
#include "../inc/ClientApp.h"

int main(int argc, char *argv[]) {
    // Determine channel
    int channel_no = 0;
    if (argc == 2) {
        try {
            channel_no = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid channel number" << std::endl;
            return 1;
        }
    }

    std::cout << "Starting ClientApp with channel: " << channel_no << std::endl;

    // Get instance (initializes config) and run
    ClientApp::getInstance().run(channel_no);

    return 0;
}
