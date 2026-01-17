#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <map>

class Config {
public:
    // 单例模式
    static Config& getInstance() {
        static Config instance;
        return instance;
    }
    
    // 加载配置文件
    bool load(const std::string& filename = "config.json");
    
    // 获取配置值
    std::string getString(const std::string& key, const std::string& defaultValue = "");
    int getInt(const std::string& key, int defaultValue = 0);
    bool getBool(const std::string& key, bool defaultValue = false);
    std::vector<std::string> getStringArray(const std::string& key);
    
    // 获取网络配置
    std::string getNetworkIP();
    int getNetworkPort();
    
    // 获取照片路径
    std::vector<std::string> getPhotoPaths();
    
    // 检查是否为国网环境
    bool isStateGridEnvironment(const std::string& localIP);
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::map<std::string, std::string> configMap;
    
    // 解析 JSON 文件
    bool parseJsonFile(const std::string& filename);
    // 解析 INI 文件
    bool parseIniFile(const std::string& filename);
};

#endif // CONFIG_H