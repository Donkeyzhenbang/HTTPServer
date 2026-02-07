#pragma once
#include <vector>
#include <string>

class ClientApp {
public:
    static ClientApp& getInstance();
    void run(int channelNo);

private:
    ClientApp();
    ~ClientApp() = default;
    
    void initConfiguration();
    
    // Simulation / Task methods
    void sendSinglePhotoGrid(int channelNo);
    void sendMultiPhotoGrid();
    void sendSinglePhotoAli(int channelNo);
    void sendMultiPhotoAli();
    void sendBatchAli(int start, int end);
    void simulateMcuAli();
    void simulateMcuSleepAli();
    void simulateModelUpgrade();
    
    std::vector<std::string> photoPaths;
    std::string netIp;
    int netPort;
    std::string stateGridIp;
};
