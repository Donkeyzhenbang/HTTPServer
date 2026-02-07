#include "../inc/ServerApp.h"
#include <iostream>

int main() {
    std::cout << "Starting ServerApp..." << std::endl;
    ServerApp::getInstance().run();
    return 0;
}
