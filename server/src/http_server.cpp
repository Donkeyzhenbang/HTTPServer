// src/http_server.cpp
#include "http_server.h"
#include "httplib.h"
#include "connection.h"  // 包含连接管理器
#include "sendfile.h"
#include "modelupgrade.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>


// 不再需要extern原来的全局变量
// 现在使用connection_manager.h中的函数

// 新增：发送B341指令的函数
static bool send_b341_to_fd(int fd, int channel_no) {
    if (fd < 0) return false;
    
    try {
        // 调用已有的SendProtocolB341函数
        SendProtocolB341(fd, channel_no);
        std::cout << "[HTTP] 已向fd=" << fd << "发送B341指令，通道=" << channel_no << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[HTTP] 发送B341指令失败: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[HTTP] 发送B341指令未知错误" << std::endl;
        return false;
    }
}

// helper: list files in web/uploads
static std::vector<std::string> list_uploaded_files(const std::string &dir) {
    std::vector<std::string> res;
    DIR *d = opendir(dir.c_str());
    if (!d) return res;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        res.push_back(name);
    }
    closedir(d);
    return res;
}

// 新增：获取可执行文件所在目录
static std::string get_exe_dir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        return ".";
    }
    buf[len] = '\0';
    std::string full = buf;
    auto pos = full.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return full.substr(0, pos);
}

// 新增：确保目录存在（递归创建）
static bool ensure_dir_exists(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return true;
        return false;
    }
    
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' || i + 1 == dir.size()) {
            if (cur.empty()) continue;
            std::string tocreate = cur;
            if (tocreate.size() > 1 && tocreate.back() == '/') tocreate.pop_back();
            if (tocreate.empty()) continue;
            if (stat(tocreate.c_str(), &st) == 0) continue;
            if (mkdir(tocreate.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "[HTTP] mkdir failed for " << tocreate << " errno=" << errno << "\n";
                return false;
            }
        }
    }
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// 全局（静态）upload dir computed once
static std::string g_upload_dir;

// 修改 save_upload_to_web：添加通道参数，在文件名中标记通道
static std::string save_upload_to_web(const std::string &filename,
                                      const std::string &content,
                                      int channel = 1) {
    if (g_upload_dir.empty()) {
        std::string exe_dir = get_exe_dir();
        g_upload_dir = exe_dir + "/web/uploads";
    }

    if (!ensure_dir_exists(g_upload_dir)) {
        std::cerr << "[HTTP] ensure_dir_exists failed: " << g_upload_dir << std::endl;
        return "";
    }

    // 使用时间戳格式化文件名，并添加通道标记
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    std::ostringstream oss;
    
    // 文件名格式：通道1_时间_原始文件名
    oss << "ch" << channel << "_" 
        << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << filename;
    
    std::string saved = oss.str();
    std::string path = g_upload_dir + "/" + saved;

    std::cerr << "[HTTP] saving upload to: " << path << " (size=" << content.size() 
              << ", channel=" << channel << ")\n";

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[HTTP] 无法打开写入文件: " << path << " errno=" << errno << "\n";
        return "";
    }
    ofs.write(content.data(), (std::streamsize)content.size());
    ofs.close();

    return saved;
}

// 新增：根据通道号过滤文件
// 新增：根据通道号过滤文件
static std::vector<std::string> filter_files_by_channel(const std::vector<std::string>& files, int channel) {
    // if (channel <= 0) return files; // 通道为0或负数时返回所有文件
    if (channel == 7) {
        // 默认通道：返回所有不是以 ch{数字}_ 开头的文件
        std::vector<std::string> filtered;
        std::regex channel_pattern("^ch\\d+_.*");  // 匹配 ch{数字}_ 开头的文件
        std::regex channel_pattern2("^channel_\\d+-.*");  // 匹配 channel_{数字}- 开头的文件
        
        for (const auto& file : files) {
            if (!std::regex_match(file, channel_pattern) && 
                !std::regex_match(file, channel_pattern2) &&
                file.find("CH") != 0 &&  // 不以 CH 开头
                file.find("ch") == std::string::npos &&  // 不包含"通道"字样
                file.find("channel") == std::string::npos) {  // 不包含"channel"字样
                filtered.push_back(file);
            }
        }
        return filtered;
    }
    
    std::vector<std::string> filtered;
    std::string pattern1 = "ch" + std::to_string(channel) + "_";  // 新格式：ch1_
    std::string pattern2 = "channel_" + std::to_string(channel) + "-";  // 旧格式：channel_1-（兼容性）
    
    for (const auto& file : files) {
        // 检查文件名是否包含通道标记
        if (file.find(pattern1) == 0 || 
            file.find(pattern2) == 0 ||
            file.find("CH" + std::to_string(channel) + "_") == 0 ||
            file.find("通道" + std::to_string(channel)) != std::string::npos ||
            file.find("channel" + std::to_string(channel)) != std::string::npos) {
            filtered.push_back(file);
        }
    }
    
    return filtered;
}

// 新增：获取连接统计信息的HTML
static std::string get_connections_html() {
    size_t total_connections = get_connection_count();
    auto connections = get_all_connections();
    
    std::time_t now_time = std::time(nullptr);
    std::tm now_tm;
    localtime_r(&now_time, &now_tm);
    
    std::ostringstream oss;
    oss << "<div style='margin-bottom: 20px; padding: 15px; background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%); "
        << "border-radius: 8px; border: 1px solid #a5d6a7; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.05);'>";
    oss << "<h3 style='margin-top: 0; color: #2e7d32; border-bottom: 1px solid #c8e6c9; padding-bottom: 8px;'>连接统计</h3>";
    oss << "<div style='margin-bottom: 15px; font-size: 1.1em;'>";
    oss << "<strong style='color: #388e3c;'>总连接数:</strong> "
        << "<span style='color: #2e7d32; font-weight: bold; font-size: 1.2em; margin-left: 8px;'>" 
        << total_connections << "</span>";
    oss << "<span style='margin-left: 20px; color: #388e3c;'>已注册设备:</span> "
        << "<span style='color: #2e7d32; font-weight: bold; margin-left: 8px;'>"
        << get_all_device_ids().size() << "</span>";
    oss << "</div>";
    
    if (connections.empty()) {
        oss << "<div style='color: #388e3c; font-style: italic; padding: 20px; text-align: center; "
            << "background: rgba(200, 230, 201, 0.5); border-radius: 4px;'>暂无活跃连接</div>";
    } else {
        oss << "<div style='max-height: 300px; overflow-y: auto;'>";
        oss << "<table style='width: 100%; border-collapse: collapse; font-size: 0.9em;'>";
        oss << "<thead>";
        oss << "<tr style='background: #c8e6c9; color: #1b5e20;'>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>文件描述符</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>设备/连接信息</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>状态</th>";
        oss << "</tr>";
        oss << "</thead>";
        oss << "<tbody>";
        
        for (const auto& conn : connections) {
            bool is_registered = conn.second.find("未注册") == std::string::npos;
            
            oss << "<tr style='border-bottom: 1px solid #c8e6c9;'>";
            oss << "<td style='padding: 10px; color: #1b5e20;'>" << conn.first << "</td>";
            oss << "<td style='padding: 10px; color: " 
                << (is_registered ? "#2e7d32" : "#388e3c") << ";'>"
                << conn.second << "</td>";
            oss << "<td style='padding: 10px;'>";
            if (is_registered) {
                oss << "<span style='background: #4caf50; color: #ffffff; padding: 3px 8px; "
                    << "border-radius: 12px; font-size: 0.8em; font-weight: bold;'>已注册</span>";
            } else {
                oss << "<span style='background: #81c784; color: #ffffff; padding: 3px 8px; "
                    << "border-radius: 12px; font-size: 0.8em;'>未注册</span>";
            }
            oss << "</td>";
            oss << "</tr>";
        }
        
        oss << "</tbody>";
        oss << "</table>";
        oss << "</div>";
    }
    
    oss << "<div style='margin-top: 15px; color: #66bb6a; font-size: 0.8em; text-align: right;'>";
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &now_tm);
    oss << "更新于: " << time_buf;
    oss << "</div>";
    oss << "</div>";
    
    return oss.str();
}

void start_http_server() {
    httplib::Server svr;

    //! 静态资源挂载 静态资源挂载
    svr.set_mount_point("/", "./web");
    svr.set_payload_max_length(500 * 1024 * 1024); // 500MB

    // 主页路由，显示连接信息
    svr.Get("/", [](const httplib::Request &req, httplib::Response &res) {
        // 读取原始的 index.html
        std::ifstream ifs("./web/index.html");
        if (!ifs) {
            res.status = 500;
            res.set_content("找不到 index.html", "text/plain");
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
        
        // 在页面顶部插入连接信息
        size_t pos = content.find("<body>");
        if (pos != std::string::npos) {
            pos += 6; // 移动到 <body> 标签后面
            std::string connections_html = get_connections_html();
            content.insert(pos, connections_html);
        }
        
        res.set_content(content, "text/html");
    });

    // POST /upload - 修改为支持通道参数
    svr.Post("/upload", [](const httplib::Request &req, httplib::Response &res) {
        if (!req.form.has_file("image")) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"no file field 'image'"})", "application/json");
            return;
        }
        
        auto file = req.form.get_file("image", 0);
        if (file.filename.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"empty filename"})", "application/json");
            return;
        }
        
        // 获取通道参数，默认为通道1
        int channel = 1;
        if (req.has_param("channel")) {
            try {
                channel = std::stoi(req.get_param_value("channel", 0));
                if (channel < 1) channel = 1;
                if (channel > 6) channel = 6; // 支持最多6个通道
            } catch (...) {
                // 保持默认值
            }
        }
        
        std::string saved = save_upload_to_web(file.filename, file.content, channel);
        if (saved.empty()) {
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"save failed"})", "application/json");
            return;
        }
        
        std::ostringstream j;
        j << "{\"ok\":true,\"filename\":\"" << saved << "\",\"url\":\"/uploads/" 
          << saved << "\",\"channel\":" << channel << "}";
        res.set_content(j.str(), "application/json");
    });

    // GET /api/images - 添加通道过滤支持
    svr.Get("/api/images", [](const httplib::Request &req, httplib::Response &res) {
        auto list = list_uploaded_files("web/uploads");
        
        // 检查是否有通道过滤参数
        if (req.has_param("channel")) {
            try {
                int channel = std::stoi(req.get_param_value("channel", 0));
                if (channel > 0) {
                    list = filter_files_by_channel(list, channel);
                }
            } catch (...) {
                // 参数错误，返回所有文件
            }
        }
        
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << list[i] << "\"";
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    // GET /api/devices -> 获取已注册设备的设备ID列表
    svr.Get("/api/devices", [](const httplib::Request &req, httplib::Response &res) {
        auto devices = get_all_device_ids();
        
        std::ostringstream oss;
        oss << "{";
        oss << "\"total\":" << devices.size() << ",";
        oss << "\"devices\":[";
        for (size_t i = 0; i < devices.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << devices[i] << "\"";
        }
        oss << "],";
        oss << "\"connections\":" << get_connection_count();
        oss << "}";
        res.set_content(oss.str(), "application/json");
    });

    // GET /api/connections -> 获取详细的连接信息
    svr.Get("/api/connections", [](const httplib::Request &req, httplib::Response &res) {
        auto connections = get_all_connections();
        auto devices = get_all_device_ids();
        
        std::ostringstream oss;
        oss << "{";
        oss << "\"total_connections\":" << get_connection_count() << ",";
        oss << "\"registered_devices\":" << devices.size() << ",";
        oss << "\"connections\":[";
        
        for (size_t i = 0; i < connections.size(); ++i) {
            if (i) oss << ",";
            oss << "{";
            oss << "\"fd\":" << connections[i].first << ",";
            oss << "\"device_id\":\"" << connections[i].second << "\",";
            oss << "\"status\":\"" 
                << (connections[i].second.find("未注册") == std::string::npos ? "registered" : "unregistered")
                << "\"";
            oss << "}";
        }
        
        oss << "]}";
        res.set_content(oss.str(), "application/json");
    });

    // 健康检查端点
    svr.Get("/health", [](const httplib::Request &req, httplib::Response &res) {
        size_t conn_count = get_connection_count();
        size_t device_count = get_all_device_ids().size();
        
        std::time_t now_time = std::time(nullptr);
        std::tm now_tm;
        localtime_r(&now_time, &now_tm);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &now_tm);
        
        std::ostringstream oss;
        oss << "{";
        oss << "\"status\":\"ok\",";
        oss << "\"connections\":" << conn_count << ",";
        oss << "\"registered_devices\":" << device_count << ",";
        oss << "\"timestamp\":\"" << time_buf << "\"";
        oss << "}";
        res.set_content(oss.str(), "application/json");
    });

    // 新增：测试端点，用于模拟设备注册
    svr.Get("/api/test/add_connection", [](const httplib::Request &req, httplib::Response &res) {
        static int test_fd_counter = 1000; // 从1000开始模拟fd
        
        // 生成模拟设备ID
        char test_device_id[17];
        snprintf(test_device_id, sizeof(test_device_id), "TEST%08X", rand() % 0xFFFFFFFF);
        
        std::lock_guard<std::mutex> lock(connection_manager_mutex);
        int fd = test_fd_counter++;
        
        // 添加到连接管理器
        connection_manager[fd] = std::make_unique<ConnectionContext>(fd);
        
        // 随机决定是否注册设备ID
        if (rand() % 2 == 0) {
            connection_manager[fd]->setDeviceId(test_device_id);
        }
        
        std::ostringstream oss;
        oss << "{";
        oss << "\"fd\":" << fd << ",";
        if (connection_manager[fd]->hasDeviceId()) {
            oss << "\"device_id\":\"" << test_device_id << "\",";
        }
        oss << "\"total_connections\":" << connection_manager.size() << ",";
        oss << "\"message\":\"test connection added\"";
        oss << "}";
        
        res.set_content(oss.str(), "application/json");
    });

    // POST /api/request_snapshot (保持原有逻辑，兼容旧前端)
    svr.Post("/api/request_snapshot", [](const httplib::Request &req, httplib::Response &res) {
        std::string device;
        std::string channel = "1";
        
        // 尝试从查询参数获取
        if (req.has_param("device")) device = req.get_param_value("device", 0);
        if (req.has_param("channel")) channel = req.get_param_value("channel", 0);
        
        // 尝试从body解析
        if (device.empty() && !req.body.empty()) {
            // 简单JSON解析
            auto pos = req.body.find("\"device\"");
            if (pos != std::string::npos) {
                auto colon = req.body.find(':', pos);
                if (colon != std::string::npos) {
                    auto q1 = req.body.find('"', colon);
                    if (q1 != std::string::npos) {
                        auto q2 = req.body.find('"', q1 + 1);
                        if (q2 != std::string::npos && q2 > q1+1) {
                            device = req.body.substr(q1+1, q2-q1-1);
                        }
                    }
                }
            }
        }
        
        if (device.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"missing device\"}", "application/json");
            return;
        }
        
        // 从连接管理器查找设备
        auto* conn_ctx = find_connection_by_device_id(device);
        
        if (!conn_ctx) {
            res.set_content("{\"ok\":false,\"error\":\"device not connected or not registered\"}", "application/json");
            return;
        }
        
        // 生成协议命令（保持原有文本命令格式，用于兼容）
        std::ostringstream proto;
        proto << "CMD:SNAPSHOT;CH:" << channel << "\n";
        std::string proto_s = proto.str();
        
        // 发送命令
        ssize_t n = ::send(conn_ctx->connfd, proto_s.c_str(), proto_s.size(), 0);
        if (n <= 0) {
            res.set_content("{\"ok\":false,\"error\":\"send failed\"}", "application/json");
            return;
        }
        
        res.set_content("{\"ok\":true,\"message\":\"command sent successfully\",\"device\":\"" 
                        + device + "\",\"channel\":" + channel + "}", "application/json");
    });

    // 新增：POST /api/send_b341 - 发送B341指令
    svr.Post("/api/send_b341", [](const httplib::Request &req, httplib::Response &res) {
        std::string device;
        int channel = 1;
        
        // 从JSON body解析
        if (!req.body.empty()) {
            try {
                // 简单JSON解析
                auto pos = req.body.find("\"device\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find('"', colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find('"', q1 + 1);
                            if (q2 != std::string::npos && q2 > q1+1) {
                                device = req.body.substr(q1+1, q2-q1-1);
                            }
                        }
                    }
                }
                
                pos = req.body.find("\"channel\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find_first_of("0123456789", colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find_first_not_of("0123456789", q1);
                            std::string channel_str = req.body.substr(q1, q2 - q1);
                            channel = std::stoi(channel_str);
                            if (channel < 1) channel = 1;
                            if (channel > 6) channel = 6; // 支持最多6个通道
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[HTTP] 解析请求失败: " << e.what() << std::endl;
            }
        }
        
        if (device.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"missing device\"}", "application/json");
            return;
        }
        
        // 从连接管理器查找设备
        auto* conn_ctx = find_connection_by_device_id(device);
        
        if (!conn_ctx) {
            res.set_content("{\"ok\":false,\"error\":\"device not connected or not registered\"}", "application/json");
            return;
        }
        
        // 发送B341指令
        bool success = send_b341_to_fd(conn_ctx->connfd, channel);
        
        if (!success) {
            res.set_content("{\"ok\":false,\"error\":\"failed to send B341 command\"}", "application/json");
            return;
        }
        
        res.set_content("{\"ok\":true,\"message\":\"B341 command sent successfully\",\"device\":\"" 
                        + device + "\",\"channel\":" + std::to_string(channel) + "}", "application/json");
    });

    // 新增：POST /api/send_b341_by_fd - 直接通过fd发送B341指令
    svr.Post("/api/send_b341_by_fd", [](const httplib::Request &req, httplib::Response &res) {
        int fd = -1;
        int channel = 1;
        
        // 从JSON body解析
        if (!req.body.empty()) {
            try {
                // 简单JSON解析
                auto pos = req.body.find("\"fd\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find_first_of("0123456789", colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find_first_not_of("0123456789", q1);
                            std::string fd_str = req.body.substr(q1, q2 - q1);
                            fd = std::stoi(fd_str);
                        }
                    }
                }
                
                pos = req.body.find("\"channel\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find_first_of("0123456789", colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find_first_not_of("0123456789", q1);
                            std::string channel_str = req.body.substr(q1, q2 - q1);
                            channel = std::stoi(channel_str);
                            if (channel < 1) channel = 1;
                            if (channel > 6) channel = 6; // 支持最多6个通道
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[HTTP] 解析请求失败: " << e.what() << std::endl;
            }
        }
        
        if (fd < 0) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"missing or invalid fd\"}", "application/json");
            return;
        }
        
        // 检查连接是否存在
        auto* conn_ctx = find_connection_by_fd(fd);
        if (!conn_ctx) {
            res.set_content("{\"ok\":false,\"error\":\"fd not found or connection not active\"}", "application/json");
            return;
        }
        
        // 发送B341指令
        bool success = send_b341_to_fd(fd, channel);
        
        if (!success) {
            res.set_content("{\"ok\":false,\"error\":\"failed to send B341 command\"}", "application/json");
            return;
        }
        
        res.set_content("{\"ok\":true,\"message\":\"B341 command sent successfully\",\"fd\":" 
                        + std::to_string(fd) + ",\"channel\":" + std::to_string(channel) + "}", "application/json");
    });


    svr.Post("/api/upload_model", [](const httplib::Request &req, httplib::Response &res) {
        try {
            // 检查是否为multipart表单数据
            if (!req.is_multipart_form_data()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"不是multipart表单数据"})", "application/json");
                return;
            }
            
            std::string device;
            std::string model_filename;
            std::string model_content;
            bool has_device = false;
            bool has_model = false;
            
            // 检查device字段（文本字段） - get_field 返回 std::string
            if (req.form.has_field("device")) {
                device = req.form.get_field("device", 0);  // 直接返回字符串，不是对象
                has_device = true;
                std::cout << "[HTTP] 获取设备ID: " << device << std::endl;
            }
            
            // 检查model字段（文件字段） - get_file 返回 FormData 对象
            if (req.form.has_file("model")) {
                auto model_file = req.form.get_file("model", 0);  // 返回 FormData 对象
                model_filename = model_file.filename;
                model_content = model_file.content;
                has_model = true;
                std::cout << "[HTTP] 获取模型文件: " << model_filename 
                        << " (" << model_content.size() << " bytes)" << std::endl;
            }
            
            if (!has_device || device.empty()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"缺少设备ID"})", "application/json");
                return;
            }
            
            if (!has_model || model_content.empty()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"缺少模型文件"})", "application/json");
                return;
            }
            
            std::cout << "[HTTP] 成功解析请求 - 设备: " << device 
                    << ", 模型文件: " << model_filename 
                    << ", 大小: " << model_content.size() << " 字节" << std::endl;
            
            // 查找设备连接
            auto* conn_ctx = find_connection_by_device_id(device);
            if (!conn_ctx) {
                res.set_content(R"({"ok":false,"error":"设备未连接或未注册"})", "application/json");
                return;
            }
            
            // 保存模型文件到临时位置
            std::string temp_model_path = "web/engines/model_upgrade_" + device + "_" + 
                                        std::to_string(std::time(nullptr)) + ".engine";
            
            try {
                std::ofstream model_file(temp_model_path, std::ios::binary);
                if (!model_file) {
                    std::cerr << "[HTTP] 无法创建临时文件: " << temp_model_path << std::endl;
                    res.set_content(R"({"ok":false,"error":"无法创建临时文件"})", "application/json");
                    return;
                }
                
                model_file.write(model_content.data(), model_content.size());
                model_file.close();
                
                std::cout << "[HTTP] 模型文件已保存到: " << temp_model_path 
                        << " (" << model_content.size() << " 字节)" << std::endl;
                
                // 检查测试图片是否存在
                // std::string test_image_path = "web/uploads/test_image.jpg";
                // struct stat buffer;
                // if (stat(test_image_path.c_str(), &buffer) != 0) {
                //     test_image_path = "web/uploads/default.jpg";
                //     if (stat(test_image_path.c_str(), &buffer) != 0) {
                //         res.set_content(R"({"ok":true,"message":"模型文件上传成功，但未进行抓拍测试（缺少测试图片）"})", "application/json");
                //         std::remove(temp_model_path.c_str());
                //         return;
                //     }
                // }
                
                // 调用自动抓拍函数，使用通道1
                std::cout << "[HTTP] 开始模型升级后抓拍测试" << std::endl;
                // int ret = SendModelToDevice(test_image_path.c_str(), 1, conn_ctx->connfd);
                int ret = SendModelToDevice(temp_model_path.c_str(), 1, conn_ctx->connfd);
                
                if (ret == 0) {
                    res.set_content(R"({"ok":true,"message":"模型文件上传成功，抓拍测试完成"})", "application/json");
                } else {
                    res.set_content(R"({"ok":true,"message":"模型文件上传成功，但抓拍测试失败"})", "application/json");
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[HTTP] 异常: " << e.what() << std::endl;
                res.set_content(R"({"ok":true,"message":"模型文件上传成功，但抓拍测试异常"})", "application/json");
            }
            
            //! 清理临时文件 暂时先不清理
            // std::remove(temp_model_path.c_str());
            
        } catch (const std::exception& e) {
            std::cerr << "[HTTP] /api/upload_model 处理异常: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"服务器内部错误"})", "application/json");
        }
    });

    // 新增：POST /api/test_capture_after_upgrade - 模型升级后测试抓拍
    svr.Post("/api/test_capture_after_upgrade", [](const httplib::Request &req, httplib::Response &res) {
        std::string device;
        int channel = 1;
        
        // 从JSON body解析
        if (!req.body.empty()) {
            try {
                auto pos = req.body.find("\"device\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find('"', colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find('"', q1 + 1);
                            if (q2 != std::string::npos && q2 > q1+1) {
                                device = req.body.substr(q1+1, q2-q1-1);
                            }
                        }
                    }
                }
                
                pos = req.body.find("\"channel\"");
                if (pos != std::string::npos) {
                    auto colon = req.body.find(':', pos);
                    if (colon != std::string::npos) {
                        auto q1 = req.body.find_first_of("0123456789", colon);
                        if (q1 != std::string::npos) {
                            auto q2 = req.body.find_first_not_of("0123456789", q1);
                            std::string channel_str = req.body.substr(q1, q2 - q1);
                            channel = std::stoi(channel_str);
                            if (channel < 1) channel = 1;
                            if (channel > 6) channel = 6;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[HTTP] 解析请求失败: " << e.what() << std::endl;
            }
        }
        
        if (device.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"missing device"})", "application/json");
            return;
        }
        
        // 查找设备连接
        auto* conn_ctx = find_connection_by_device_id(device);
        if (!conn_ctx) {
            res.set_content(R"({"ok":false,"error":"device not connected or not registered"})", "application/json");
            return;
        }
        
        // 使用默认测试图片路径
        std::string test_image_path = "web/uploads/test_image.jpg";
        
        // 检查文件是否存在
        struct stat buffer;
        if (stat(test_image_path.c_str(), &buffer) != 0) {
            test_image_path = "web/uploads/default.jpg";
            
            if (stat(test_image_path.c_str(), &buffer) != 0) {
                res.set_content(R"({"ok":false,"error":"test image not found"})", "application/json");
                return;
            }
        }
        
        // SendModelToDevice
        int ret = SendModelToDevice(test_image_path.c_str(), channel, conn_ctx->connfd);
        
        if (ret == 0) {
            res.set_content(R"({"ok":true,"message":"capture test completed successfully"})", "application/json");
        } else {
            res.set_content(R"({"ok":false,"error":"capture test failed"})", "application/json");
        }
    });

    std::cout << "[HTTP] Server starting on 0.0.0.0:8080\n";
    std::cout << "[HTTP] Available endpoints:\n";
    std::cout << "[HTTP]   GET  /                     - 主页面（显示连接信息）\n";
    std::cout << "[HTTP]   GET  /api/devices          - 获取设备ID列表\n";
    std::cout << "[HTTP]   GET  /api/connections      - 获取详细连接信息\n";
    std::cout << "[HTTP]   POST /api/request_snapshot - 发送快照命令\n";
    std::cout << "[HTTP]   POST /api/send_b341        - 发送B341指令\n";
    std::cout << "[HTTP]   POST /api/upload_model     - 上传模型文件到设备\n";
    std::cout << "[HTTP]   GET  /health               - 健康检查\n";
    std::cout << "[HTTP]   GET  /api/test/add_connection - 测试：添加模拟连接\n";
    
    svr.listen("0.0.0.0", 8080);
}