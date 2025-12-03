// src/http_server.cpp
#include "http_server.h"
#include "httplib.h"
#include "connection.h"  // 包含连接管理器
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

// 不再需要extern原来的全局变量
// 现在使用connection_manager.h中的函数

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

// 修改 save_upload_to_web：使用绝对 path
static std::string save_upload_to_web(const std::string &filename,
                                      const std::string &content) {
    if (g_upload_dir.empty()) {
        std::string exe_dir = get_exe_dir();
        g_upload_dir = exe_dir + "/web/uploads";
    }

    if (!ensure_dir_exists(g_upload_dir)) {
        std::cerr << "[HTTP] ensure_dir_exists failed: " << g_upload_dir << std::endl;
        return "";
    }

    // 使用时间戳格式化文件名
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);  // 使用线程安全的版本
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << filename;
    std::string saved = oss.str();
    std::string path = g_upload_dir + "/" + saved;

    std::cerr << "[HTTP] saving upload to: " << path << " (size=" << content.size() << ")\n";

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[HTTP] 无法打开写入文件: " << path << " errno=" << errno << "\n";
        return "";
    }
    ofs.write(content.data(), (std::streamsize)content.size());
    ofs.close();

    return saved;
}

// 新增：获取连接统计信息的HTML
static std::string get_connections_html() {
    size_t total_connections = get_connection_count();
    auto connections = get_all_connections();
    
    std::time_t now_time = std::time(nullptr);
    std::tm now_tm;
    localtime_r(&now_time, &now_tm);
    
    std::ostringstream oss;
    oss << "<div style='margin-bottom: 20px; padding: 15px; background: linear-gradient(135deg, #022b2f 0%, #011a1d 100%); "
        << "border-radius: 8px; border: 1px solid #0f8b8f; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);'>";
    oss << "<h3 style='margin-top: 0; color: #06b3b0; border-bottom: 1px solid #024; padding-bottom: 8px;'>连接统计</h3>";
    oss << "<div style='margin-bottom: 15px; font-size: 1.1em;'>";
    oss << "<strong style='color: #a8eae6;'>总连接数:</strong> "
        << "<span style='color: #06b3b0; font-weight: bold; font-size: 1.2em; margin-left: 8px;'>" 
        << total_connections << "</span>";
    oss << "<span style='margin-left: 20px; color: #a8eae6;'>已注册设备:</span> "
        << "<span style='color: #06b3b0; font-weight: bold; margin-left: 8px;'>"
        << get_all_device_ids().size() << "</span>";
    oss << "</div>";
    
    if (connections.empty()) {
        oss << "<div style='color: #a8eae6; font-style: italic; padding: 20px; text-align: center; "
            << "background: rgba(2, 36, 40, 0.5); border-radius: 4px;'>暂无活跃连接</div>";
    } else {
        oss << "<div style='max-height: 300px; overflow-y: auto;'>";
        oss << "<table style='width: 100%; border-collapse: collapse; font-size: 0.9em;'>";
        oss << "<thead>";
        oss << "<tr style='background: #024; color: #a8eae6;'>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #06b3b0;'>文件描述符</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #06b3b0;'>设备/连接信息</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #06b3b0;'>状态</th>";
        oss << "</tr>";
        oss << "</thead>";
        oss << "<tbody>";
        
        for (const auto& conn : connections) {
            bool is_registered = conn.second.find("未注册") == std::string::npos;
            
            oss << "<tr style='border-bottom: 1px solid #024;'>";
            oss << "<td style='padding: 10px; color: #c7f7f7;'>" << conn.first << "</td>";
            oss << "<td style='padding: 10px; color: " 
                << (is_registered ? "#06b3b0" : "#a8eae6") << ";'>"
                << conn.second << "</td>";
            oss << "<td style='padding: 10px;'>";
            if (is_registered) {
                oss << "<span style='background: #028b86; color: #021212; padding: 3px 8px; "
                    << "border-radius: 12px; font-size: 0.8em; font-weight: bold;'>已注册</span>";
            } else {
                oss << "<span style='background: #555; color: #ddd; padding: 3px 8px; "
                    << "border-radius: 12px; font-size: 0.8em;'>未注册</span>";
            }
            oss << "</td>";
            oss << "</tr>";
        }
        
        oss << "</tbody>";
        oss << "</table>";
        oss << "</div>";
    }
    
    oss << "<div style='margin-top: 15px; color: #7ab7b5; font-size: 0.8em; text-align: right;'>";
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &now_tm);
    oss << "更新于: " << time_buf;
    oss << "</div>";
    oss << "</div>";
    
    return oss.str();
}

void start_http_server() {
    httplib::Server svr;

    // 静态资源挂载
    svr.set_mount_point("/", "./web");
    svr.set_payload_max_length(50 * 1024 * 1024); // 50MB

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

    // POST /upload
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
        
        std::string saved = save_upload_to_web(file.filename, file.content);
        if (saved.empty()) {
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"save failed"})", "application/json");
            return;
        }
        
        std::ostringstream j;
        j << "{\"ok\":true,\"filename\":\"" << saved << "\",\"url\":\"/uploads/" << saved << "\"}";
        res.set_content(j.str(), "application/json");
    });

    // GET /api/images
    svr.Get("/api/images", [](const httplib::Request &req, httplib::Response &res) {
        auto list = list_uploaded_files("web/uploads");
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

    // POST /api/request_snapshot
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
        
        // 生成协议命令
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

    std::cout << "[HTTP] Server starting on 0.0.0.0:8080\n";
    std::cout << "[HTTP] Available endpoints:\n";
    std::cout << "[HTTP]   GET  /                     - 主页面（显示连接信息）\n";
    std::cout << "[HTTP]   GET  /api/devices          - 获取设备ID列表\n";
    std::cout << "[HTTP]   GET  /api/connections      - 获取详细连接信息\n";
    std::cout << "[HTTP]   POST /api/request_snapshot - 发送快照命令\n";
    std::cout << "[HTTP]   GET  /health               - 健康检查\n";
    std::cout << "[HTTP]   GET  /api/test/add_connection - 测试：添加模拟连接\n";
    
    svr.listen("0.0.0.0", 8080);
}