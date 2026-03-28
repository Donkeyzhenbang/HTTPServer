// src/http_server.cpp
#include "http_server.h"
#include "httplib.h"
#include "connection.h"  // 包含连接管理器
#include "sendfile.h"
#include "modelupgrade.h"
#include "../inc/ServerApp.h" // Access Redis
#include "../inc/recvfile.h"
#include <iostream>
#include <zmq.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <mutex>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>
using namespace std;

zmq::context_t g_zmq_ctx(1);



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
// Func moved to base/src/utils.cpp

// 新增：确保目录存在（递归创建）
// Func moved to base/src/utils.cpp

// 全局（静态）upload dir computed once
// static std::string g_upload_dir; // Removed, using get_upload_dir() from base

// 修改 save_upload_to_web：添加通道参数，在文件名中标记通道
static std::string save_upload_to_web(const std::string &filename,
                                      const std::string &content,
                                      int channel = 1) {
    std::string upload_dir = get_upload_dir();

    if (!ensure_dir_exists(upload_dir)) {
        std::cerr << "[HTTP] ensure_dir_exists failed: " << upload_dir << std::endl;
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
    std::string path = upload_dir + "/" + saved;

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

// 新增：获取连接统计信息的HTML (分布式全网视图)
static std::string get_connections_html() {
    // 1. 获取本地连接
    auto local_connections = get_all_connections(); // map<fd, description>
    
    // 2. 从描述中提取本地设备ID (用于去重)
    std::unordered_set<std::string> local_device_ids;
    for(const auto& conn : local_connections) {
        size_t start = conn.second.find('(');
        size_t end = conn.second.find(')');
        if(start != std::string::npos && end != std::string::npos) {
             std::string id = conn.second.substr(start+1, end-start-1);
             if(id != "未注册") local_device_ids.insert(id);
        }
    }

    // 3. 获取 Redis 全局在线设备
    struct RemoteNodeInfo { std::string ip; int port; int http_port; };
    std::map<std::string, RemoteNodeInfo> remote_devices;
    
    if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
         std::vector<std::string> keys = redis->Keys("device:online:*");
         for(const auto& k : keys) {
             std::string dev_id = k.substr(14); // len("device:online:")
             
             // 如果本地已经有了，跳过（本地优先展示）
             if(local_device_ids.count(dev_id)) continue;

             std::string val = redis->Get(k);
             
             // 简单解析 JSON
             RemoteNodeInfo info = {"未知", 0, 0};
             size_t p1 = val.find("\"ip\":\"");
             if(p1 != std::string::npos) {
                 size_t p2 = val.find("\"", p1+6);
                 if(p2 != std::string::npos) info.ip = val.substr(p1+6, p2-p1-6);
             }
             p1 = val.find("\"port\":");
             if(p1 != std::string::npos) {
                 info.port = std::stoi(val.substr(p1+7));
             }
             p1 = val.find("\"http_port\":");
             if(p1 != std::string::npos) {
                 info.http_port = std::stoi(val.substr(p1+12)); // 修正解析
             }
             remote_devices[dev_id] = info;
         }
    }
    
    std::time_t now_time = std::time(nullptr);
    std::tm now_tm;
    localtime_r(&now_time, &now_tm);
    
    std::ostringstream oss;
    oss << "<div style='margin-bottom: 20px; padding: 15px; background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%); "
        << "border-radius: 8px; border: 1px solid #a5d6a7; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.05);'>";
    oss << "<h3 style='margin-top: 0; color: #2e7d32; border-bottom: 1px solid #c8e6c9; padding-bottom: 8px;'>全网连接统计 (分布式视图)</h3>";
    oss << "<div style='margin-bottom: 15px; font-size: 1.1em;'>";
    oss << "<strong style='color: #388e3c;'>当前节点连接:</strong> "
        << "<span style='color: #2e7d32; font-weight: bold; font-size: 1.2em; margin-left: 8px;'>" 
        << local_connections.size() << "</span>";
    oss << "<span style='margin-left: 20px; color: #388e3c;'>全网远程设备:</span> "
        << "<span style='color: #1565c0; font-weight: bold; margin-left: 8px;'>"
        << remote_devices.size() << "</span>";
    oss << "</div>";
    
    if (local_connections.empty() && remote_devices.empty()) {
        oss << "<div style='color: #388e3c; font-style: italic; padding: 20px; text-align: center; "
            << "background: rgba(200, 230, 201, 0.5); border-radius: 4px;'>暂无活跃连接</div>";
    } else {
        oss << "<div style='max-height: 400px; overflow-y: auto;'>";
        oss << "<table style='width: 100%; border-collapse: collapse; font-size: 0.9em;'>";
        oss << "<thead>";
        oss << "<tr style='background: #c8e6c9; color: #1b5e20;'>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>位置/FD</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>设备信息</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>节点地址</th>";
        oss << "<th style='padding: 10px; text-align: left; border-bottom: 2px solid #2e7d32;'>状态</th>";
        oss << "</tr>";
        oss << "</thead>";
        oss << "<tbody>";
        
        // 1. 显示本地连接
        for (const auto& conn : local_connections) {
            bool is_registered = conn.second.find("未注册") == std::string::npos;
            oss << "<tr style='border-bottom: 1px solid #c8e6c9; background-color: rgba(255,255,255,0.6);'>";
            oss << "<td style='padding: 10px; color: #1b5e20;'><strong>[本机]</strong> FD=" << conn.first << "</td>";
            oss << "<td style='padding: 10px; color: " << (is_registered ? "#2e7d32" : "#388e3c") << ";'>" << conn.second << "</td>";
            oss << "<td style='padding: 10px; color: #666;'>127.0.0.1 (Local)</td>";
            oss << "<td style='padding: 10px;'><span style='background: #4caf50; color: #ffffff; padding: 3px 8px; border-radius: 12px; font-size: 0.8em;'>在线</span></td>";
            oss << "</tr>";
        }

        // 2. 显示远程连接
        for (const auto& kv : remote_devices) {
            oss << "<tr style='border-bottom: 1px solid #b3e5fc; background-color: #e1f5fe;'>";
            oss << "<td style='padding: 10px; color: #0277bd;'><strong>[远程]</strong> Redis</td>";
            oss << "<td style='padding: 10px; color: #0277bd; font-weight:bold;'>" << kv.first << "</td>";
            oss << "<td style='padding: 10px; color: #0288d1;'>" << kv.second.ip << ":" << kv.second.http_port << "</td>";
            oss << "<td style='padding: 10px;'><span style='background: #29b6f6; color: #ffffff; padding: 3px 8px; border-radius: 12px; font-size: 0.8em;'>云端同步</span></td>";
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

void start_http_server(int port) {
    if (port <= 0) port = 8080;
    httplib::Server svr;

    std::string frontend_dir = get_frontend_dir();
    std::string upload_dir = get_upload_dir();
    std::string engines_dir = get_engines_dir();

    std::cout << "[HTTP] Frontend Dir: " << frontend_dir << std::endl;
    std::cout << "[HTTP] Upload Dir: " << upload_dir << std::endl;
    std::cout << "[HTTP] Engines Dir: " << engines_dir << std::endl;

    //! 静态资源挂载 静态资源挂载
    svr.set_mount_point("/", frontend_dir);
    svr.set_mount_point("/uploads", upload_dir);
    svr.set_mount_point("/engines", engines_dir);

    svr.set_payload_max_length(500 * 1024 * 1024); // 500MB

    // 主页路由，显示连接信息
    svr.Get("/", [frontend_dir](const httplib::Request &req, httplib::Response &res) {
        // 读取原始的 index.html
        std::ifstream ifs(frontend_dir + "/index.html");
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
    // ----- AI Inference Route (Proxy to Microservice) -----
    svr.Post("/api/infer", [](const httplib::Request &req, httplib::Response &res) {
        if (!req.form.has_file("image")) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"no file field 'image'"})", "application/json");
            return;
        }

        std::string model_type = "yolo";
        if (req.has_param("model")) {
            model_type = req.get_param_value("model", 0);
        }

        auto file = req.form.get_file("image", 0);
        
        // ZeroMQ IPC/Network call to AI Microservice (Port 50055)
        try {
            zmq::socket_t zmq_sock(g_zmq_ctx, zmq::socket_type::req);
            // Set timeout so it doesn't block forever
            int timeout_ms = 5000;
            zmq_sock.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
            zmq_sock.setsockopt(ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
            
            zmq_sock.connect("tcp://127.0.0.1:50055");
            
            // Frame 1: JSON metadata
            std::string meta_json = "{\"model\": \"" + model_type + "\"}";
            zmq::message_t meta_msg(meta_json.size());
            memcpy(meta_msg.data(), meta_json.data(), meta_json.size());
            zmq_sock.send(meta_msg, zmq::send_flags::sndmore);
            
            // Frame 2: Image binary content
            zmq::message_t img_msg(file.content.size());
            memcpy(img_msg.data(), file.content.data(), file.content.size());
            zmq_sock.send(img_msg, zmq::send_flags::none);
            
            // Wait for response
            zmq::message_t reply;
            auto res_size = zmq_sock.recv(reply, zmq::recv_flags::none);
            
            if (res_size.has_value()) {
                std::string reply_str(static_cast<char*>(reply.data()), reply.size());
                res.set_content(reply_str, "application/json");
            } else {
                res.status = 504;
                res.set_content(R"({"ok":false,"error":"AI Worker timeout"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            std::string err = R"({"ok":false,"error":"ZMQ Exception: )";
            err += e.what();
            err += R"("})";
            res.set_content(err, "application/json");
        }
    });

    // ----- Original Upload Route -----
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
        std::string upload_dir = get_upload_dir();
        auto list = list_uploaded_files(upload_dir);
        
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

    // GET /api/devices -> 获取已注册设备的设备ID列表 (分布式全网视图)
    svr.Get("/api/devices", [](const httplib::Request &req, httplib::Response &res) {
        auto local_devices = get_all_device_ids();
        std::unordered_set<std::string> local_device_set(local_devices.begin(), local_devices.end());

        // 从Redis获取全网设备
        std::vector<std::string> all_devices = local_devices;
        if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
            std::vector<std::string> keys = redis->Keys("device:online:*");
            for (const auto& k : keys) {
                std::string dev_id = k.substr(14); // len("device:online:")
                if (!local_device_set.count(dev_id)) {
                    all_devices.push_back(dev_id);
                }
            }
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"total\":" << all_devices.size() << ",";
        oss << "\"devices\":[";
        for (size_t i = 0; i < all_devices.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << all_devices[i] << "\"";
        }
        oss << "],";
        oss << "\"local_connections\":" << get_connection_count();
        oss << "}";
        res.set_content(oss.str(), "application/json");
    });

    // GET /api/connections -> 获取详细的连接信息 (分布式全网视图)
    svr.Get("/api/connections", [](const httplib::Request &req, httplib::Response &res) {
        auto connections = get_all_connections();
        auto devices = get_all_device_ids();

        // 获取本地设备ID集合用于去重
        std::unordered_set<std::string> local_device_ids;
        for (const auto& conn : connections) {
            size_t start = conn.second.find('(');
            size_t end = conn.second.find(')');
            if (start != std::string::npos && end != std::string::npos) {
                std::string id = conn.second.substr(start + 1, end - start - 1);
                if (id != "未注册") local_device_ids.insert(id);
            }
        }

        // 从Redis获取远程设备信息
        struct RemoteInfo { std::string ip; int port; int http_port; };
        std::map<std::string, RemoteInfo> remote_devices;
        if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
            std::vector<std::string> keys = redis->Keys("device:online:*");
            for (const auto& k : keys) {
                std::string dev_id = k.substr(14);
                if (local_device_ids.count(dev_id)) continue; // 跳过本地已有的

                std::string val = redis->Get(k);
                RemoteInfo info = {"unknown", 0, 0};
                size_t p1 = val.find("\"ip\":\"");
                if (p1 != std::string::npos) {
                    size_t p2 = val.find("\"", p1 + 6);
                    if (p2 != std::string::npos) info.ip = val.substr(p1 + 6, p2 - p1 - 6);
                }
                p1 = val.find("\"port\":");
                if (p1 != std::string::npos) {
                    info.port = std::stoi(val.substr(p1 + 7));
                }
                p1 = val.find("\"http_port\":");
                if (p1 != std::string::npos) {
                    info.http_port = std::stoi(val.substr(p1 + 12));
                }
                remote_devices[dev_id] = info;
            }
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"total_connections\":" << get_connection_count() << ",";
        oss << "\"local_registered_devices\":" << devices.size() << ",";
        oss << "\"remote_devices\":" << remote_devices.size() << ",";
        oss << "\"connections\":[";

        // 先输出本地连接
        for (size_t i = 0; i < connections.size(); ++i) {
            if (i) oss << ",";
            oss << "{";
            oss << "\"fd\":" << connections[i].first << ",";
            oss << "\"device_id\":\"" << connections[i].second << "\",";
            oss << "\"location\":\"local\",";
            oss << "\"status\":\""
                << (connections[i].second.find("未注册") == std::string::npos ? "registered" : "unregistered")
                << "\"";
            oss << "}";
        }

        // 再输出远程连接
        for (const auto& kv : remote_devices) {
            if (!connections.empty() || &kv != &*remote_devices.begin()) oss << ",";
            oss << "{";
            oss << "\"fd\":-1,";
            oss << "\"device_id\":\"" << kv.first << "\",";
            oss << "\"location\":\"remote\",";
            oss << "\"node_ip\":\"" << kv.second.ip << "\",";
            oss << "\"node_port\":" << kv.second.port << ",";
            oss << "\"status\":\"online\"";
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
            // Redis Lookup for Distributed Node
            if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
                std::string key = "device:online:" + device;
                std::string val = redis->Get(key);
                if (!val.empty()) {
                    // Start Proxy logic
                    // Parse JSON: {"ip":"...","port":...,"http_port":...}
                    std::string target_ip;
                    std::string target_port_str;
                    int target_port = 0;
                    
                    // Simple manual parsing
                    // Look for "http_port": 1234
                    size_t pos_port = val.find("\"http_port\":");
                    if (pos_port != std::string::npos) {
                        size_t start = pos_port + 12; // Length of "http_port":
                        // Find first digit
                        size_t digit_start = val.find_first_of("0123456789", start);
                        if (digit_start != std::string::npos) {
                            size_t digit_end = val.find_first_not_of("0123456789", digit_start);
                            if (digit_end == std::string::npos) digit_end = val.length();
                            target_port_str = val.substr(digit_start, digit_end - digit_start);
                            try { target_port = std::stoi(target_port_str); } catch(...) {} 
                        }
                    }
                    
                    // Look for "ip": "..."
                    // Correcting logic to handle possible spaces or different ordering
                    size_t pos_ip = val.find("\"ip\":\"");
                    if (pos_ip != std::string::npos) {
                        size_t start = pos_ip + 6;
                        size_t end = val.find("\"", start);
                        if (end != std::string::npos) {
                            target_ip = val.substr(start, end - start);
                        }
                    }

                    std::cout << "[HTTP] Distributed Check: TargetIP=" << target_ip
                              << " TargetPort=" << target_port
                              << " SelfIP=" << ServerApp::getInstance().GetLocalIp()
                              << " SelfPort=" << ServerApp::getInstance().GetHttpPort() << std::endl;

                    // Check if redirect needed: 判断目标是否为本机
                    // 需要同时满足：IP相同 且 端口相同 才认为是本机，否则需要转发
                    bool is_local = (target_ip == ServerApp::getInstance().GetLocalIp() ||
                                     target_ip == "127.0.0.1" ||
                                     target_ip == "localhost") &&
                                    (target_port == ServerApp::getInstance().GetHttpPort());

                    if (target_port > 0 && !target_ip.empty() && !is_local) {
                             std::cout << "[HTTP] Proxying request to " << target_ip << ":" << target_port << std::endl;
                             // Just send same body
                             httplib::Client cli(target_ip, target_port);
                             cli.set_connection_timeout(2, 0); // 2s connect
                             cli.set_read_timeout(5, 0); // 5s read
                             
                             auto res2 = cli.Post("/api/send_b341", req.body, "application/json");
                             if (res2) { // Propagate response
                                 res.status = res2->status;
                                 res.set_content(res2->body, res2->get_header_value("Content-Type"));
                                 return;
                             } else {
                                 res.status = 502;
                                 res.set_content("{\"ok\":false,\"error\":\"proxy failed to reach target node\"}", "application/json");
                                 return;
                             }
                    }
                }
            }

            res.set_content("{\"ok\":false,\"error\":\"device not connected or not registered locally\"}", "application/json");
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
            int modelType = 1;
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

            // 新增：检查modelType字段
            if (req.form.has_field("modelType")) {
                try {
                    std::string modelTypeStr = req.form.get_field("modelType", 0);
                    modelType = std::stoi(modelTypeStr);
                    std::cout << "[HTTP] 获取模型类型: " << modelType << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[HTTP] 解析模型类型失败: " << e.what() << std::endl;
                    // 保持默认值
                }
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
            std::string temp_model_path = get_engines_dir() + "/model_upgrade_" + device + "_" + 
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
                // 调用自动抓拍函数，使用前端传来的modelType作为通道号
                std::cout << "[HTTP] 开始模型升级后抓拍测试，通道号: " << modelType << std::endl;
                
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
                
                // 解决与Reactor线程抢占读事件的问题：暂停Reactor对该socket的接管
                ServerApp::getInstance().getEventLoop().RemoveSocket(conn_ctx->connfd);
                
                int ret = SendModelToDevice(temp_model_path.c_str(), modelType, conn_ctx->connfd);
                
                // 恢复Reactor的接管
                ServerApp::getInstance().getEventLoop().AddSocket(conn_ctx->connfd, EPOLLIN | EPOLLET | EPOLLRDHUP, [](int fd){
                    OnClientRead(fd);
                });
                
                if (ret == 0) {
                    res.set_content(R"({"ok":true,"message":"模型文件上传成功，模型升级完成"})", "application/json");
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
        std::string test_image_path = get_upload_dir() + "/test_image.jpg";
        
        // 检查文件是否存在
        struct stat buffer;
        if (stat(test_image_path.c_str(), &buffer) != 0) {
            test_image_path = get_upload_dir() + "/default.jpg";
            
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

    svr.listen("0.0.0.0", port);
}

// 新HTTP Server的初始化函数 (基于epoll高性能版本)
void init_new_http_server(gw::HttpServer* server) {
    if (!server) return;

    std::string frontend_dir = get_frontend_dir();
    std::string upload_dir = get_upload_dir();
    std::string engines_dir = get_engines_dir();

    // 设置静态文件目录
    server->AddMountPoint("/", frontend_dir);
    server->AddMountPoint("/uploads", upload_dir);
    server->AddMountPoint("/engines", engines_dir);

    // 主页路由，显示连接信息
    server->Get("/", [frontend_dir](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::ifstream ifs(frontend_dir + "/index.html");
        if (!ifs) {
            res.SetStatus(500, "Internal Server Error");
            res.SetText("找不到 index.html");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());

        size_t pos = content.find("<body>");
        if (pos != std::string::npos) {
            pos += 6;
            std::string connections_html = get_connections_html();
            content.insert(pos, connections_html);
        }

        res.SetHtml(content);
    });

    // POST /upload - 文件上传
    server->Post("/upload", [upload_dir](const gw::HttpRequest& req, gw::HttpResponse& res) {
        // 简单实现：检查Content-Type是否为multipart
        std::string content_type = req.GetHeader("Content-Type");

        if (content_type.find("multipart/form-data") != 0) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"Content-Type must be multipart/form-data\"}");
            return;
        }

        // 解析body中的multipart数据 (简化版)
        // 实际生产环境建议使用专门的multipart解析库
        std::string boundary;
        size_t pos = content_type.find("boundary=");
        if (pos != std::string::npos) {
            boundary = content_type.substr(pos + 9);
        }

        if (boundary.empty() || req.body.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"invalid request\"}");
            return;
        }

        // 解析filename和content
        std::string filename;
        std::string file_content;

        // 简单解析：查找filename="..." 和 Content-Length
        size_t fn_pos = req.body.find("filename=\"");
        if (fn_pos != std::string::npos) {
            size_t fn_start = fn_pos + 10;
            size_t fn_end = req.body.find("\"", fn_start);
            if (fn_end != std::string::npos) {
                filename = req.body.substr(fn_start, fn_end - fn_start);
            }
        }

        if (filename.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"empty filename\"}");
            return;
        }

        // 获取通道参数
        int channel = 1;
        std::string channel_str = req.GetQuery("channel");
        if (!channel_str.empty()) {
            try {
                channel = std::stoi(channel_str);
                if (channel < 1) channel = 1;
                if (channel > 6) channel = 6;
            } catch (...) {}
        }

        // 提取文件内容 (在两个boundary之间)
        size_t body_start = req.body.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body_start += 4;
            size_t body_end = req.body.rfind("\r\n--" + boundary);
            if (body_end == std::string::npos) {
                body_end = req.body.size();
            }
            file_content = req.body.substr(body_start, body_end - body_start - 2);
        }

        if (file_content.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"empty file content\"}");
            return;
        }

        // 保存文件
        std::string saved = save_upload_to_web(filename, file_content, channel);
        if (saved.empty()) {
            res.SetStatus(500, "Internal Server Error");
            res.SetJson("{\"ok\":false,\"error\":\"save failed\"}");
            return;
        }

        std::ostringstream j;
        j << "{\"ok\":true,\"filename\":\"" << saved << "\",\"url\":\"/uploads/"
          << saved << "\",\"channel\":" << channel << "}";
        res.SetJson(j.str());
    });

    // GET /api/images
    server->Get("/api/images", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string upload_dir = get_upload_dir();
        auto list = list_uploaded_files(upload_dir);

        // 通道过滤
        std::string channel_str = req.GetQuery("channel");
        if (!channel_str.empty()) {
            try {
                int channel = std::stoi(channel_str);
                if (channel > 0) {
                    list = filter_files_by_channel(list, channel);
                }
            } catch (...) {}
        }

        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << list[i] << "\"";
        }
        oss << "]";
        res.SetJson(oss.str());
    });

    // GET /api/devices
    server->Get("/api/devices", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        auto local_devices = get_all_device_ids();
        std::unordered_set<std::string> local_device_set(local_devices.begin(), local_devices.end());

        std::vector<std::string> all_devices = local_devices;
        if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
            std::vector<std::string> keys = redis->Keys("device:online:*");
            for (const auto& k : keys) {
                std::string dev_id = k.substr(14);
                if (!local_device_set.count(dev_id)) {
                    all_devices.push_back(dev_id);
                }
            }
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"total\":" << all_devices.size() << ",";
        oss << "\"devices\":[";
        for (size_t i = 0; i < all_devices.size(); ++i) {
            if (i) oss << ",";
            oss << "\"" << all_devices[i] << "\"";
        }
        oss << "],";
        oss << "\"local_connections\":" << get_connection_count();
        oss << "}";
        res.SetJson(oss.str());
    });

    // GET /api/connections
    server->Get("/api/connections", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        auto connections = get_all_connections();
        auto devices = get_all_device_ids();

        std::unordered_set<std::string> local_device_ids;
        for (const auto& conn : connections) {
            size_t start = conn.second.find('(');
            size_t end = conn.second.find(')');
            if (start != std::string::npos && end != std::string::npos) {
                std::string id = conn.second.substr(start + 1, end - start - 1);
                if (id != "未注册") local_device_ids.insert(id);
            }
        }

        struct RemoteInfo { std::string ip; int port; int http_port; };
        std::map<std::string, RemoteInfo> remote_devices;
        if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
            std::vector<std::string> keys = redis->Keys("device:online:*");
            for (const auto& k : keys) {
                std::string dev_id = k.substr(14);
                if (local_device_ids.count(dev_id)) continue;

                std::string val = redis->Get(k);
                RemoteInfo info = {"unknown", 0, 0};
                size_t p1 = val.find("\"ip\":\"");
                if (p1 != std::string::npos) {
                    size_t p2 = val.find("\"", p1 + 6);
                    if (p2 != std::string::npos) info.ip = val.substr(p1 + 6, p2 - p1 - 6);
                }
                p1 = val.find("\"port\":");
                if (p1 != std::string::npos) {
                    info.port = std::stoi(val.substr(p1 + 7));
                }
                p1 = val.find("\"http_port\":");
                if (p1 != std::string::npos) {
                    info.http_port = std::stoi(val.substr(p1 + 12));
                }
                remote_devices[dev_id] = info;
            }
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"total_connections\":" << get_connection_count() << ",";
        oss << "\"local_registered_devices\":" << devices.size() << ",";
        oss << "\"remote_devices\":" << remote_devices.size() << ",";
        oss << "\"connections\":[";

        for (size_t i = 0; i < connections.size(); ++i) {
            if (i) oss << ",";
            oss << "{";
            oss << "\"fd\":" << connections[i].first << ",";
            oss << "\"device_id\":\"" << connections[i].second << "\",";
            oss << "\"location\":\"local\",";
            oss << "\"status\":\""
                << (connections[i].second.find("未注册") == std::string::npos ? "registered" : "unregistered")
                << "\"";
            oss << "}";
        }

        for (const auto& kv : remote_devices) {
            if (!connections.empty() || &kv != &*remote_devices.begin()) oss << ",";
            oss << "{";
            oss << "\"fd\":-1,";
            oss << "\"device_id\":\"" << kv.first << "\",";
            oss << "\"location\":\"remote\",";
            oss << "\"node_ip\":\"" << kv.second.ip << "\",";
            oss << "\"node_port\":" << kv.second.port << ",";
            oss << "\"status\":\"online\"";
            oss << "}";
        }

        oss << "]}";
        res.SetJson(oss.str());
    });

    // GET /health
    server->Get("/health", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
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
        res.SetJson(oss.str());
    });

    // GET /api/test/add_connection
    server->Get("/api/test/add_connection", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string device_id = req.GetQuery("device_id");

        if (device_id.empty()) {
            // 自动生成一个测试设备ID
            std::time_t t = std::time(nullptr);
            std::ostringstream oss;
            oss << "TEST_" << t;
            device_id = oss.str();
        }

        // 使用test_add_simulated_connection函数
        static int test_fd_counter = 1000;
        char test_device_id[17];
        snprintf(test_device_id, sizeof(test_device_id), "TEST%08X", rand() % 0xFFFFFFFF);

        {
            std::lock_guard<std::mutex> lock(connection_manager_mutex);
            int fd = test_fd_counter++;
            connection_manager[fd] = std::make_unique<ConnectionContext>(fd);
            if (rand() % 2 == 0) {
                connection_manager[fd]->setDeviceId(test_device_id);
            }
        }

        res.SetJson("{\"ok\":true,\"device_id\":\"" + std::string(test_device_id) + "\"}");
    });

    // POST /api/request_snapshot
    server->Post("/api/request_snapshot", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string device = req.GetQuery("device");
        int channel = 1;

        std::string channel_str = req.GetQuery("channel");
        if (!channel_str.empty()) {
            try {
                channel = std::stoi(channel_str);
                if (channel < 1) channel = 1;
                if (channel > 6) channel = 6;
            } catch (...) {}
        }

        if (device.empty()) {
            // 尝试从body解析
            device = req.GetQuery("device");
        }

        if (device.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"missing device\"}");
            return;
        }

        // 查找设备连接
        auto* conn_ctx = find_connection_by_device_id(device);
        if (!conn_ctx) {
            res.SetStatus(404, "Not Found");
            res.SetJson("{\"ok\":false,\"error\":\"device not found\"}");
            return;
        }

        // 发送快照请求 (直接发送协议命令)
        std::ostringstream proto;
        proto << "CMD:SNAPSHOT;CH:" << channel << "\n";
        std::string proto_s = proto.str();
        ssize_t n = ::send(conn_ctx->connfd, proto_s.c_str(), proto_s.size(), 0);

        if (n <= 0) {
            res.SetStatus(500, "Internal Server Error");
            res.SetJson("{\"ok\":false,\"error\":\"send failed\"}");
            return;
        }

        res.SetJson("{\"ok\":true,\"device\":\"" + device + "\",\"channel\":" + std::to_string(channel) + "}");
    });

    // POST /api/send_b341
    server->Post("/api/send_b341", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string device = req.GetQuery("device");
        int channel = 1;

        std::string channel_str = req.GetQuery("channel");
        if (!channel_str.empty()) {
            try {
                channel = std::stoi(channel_str);
                if (channel < 1) channel = 1;
                if (channel > 6) channel = 6;
            } catch (...) {}
        }

        // 也尝试从body解析JSON
        if (device.empty() && !req.body.empty()) {
            size_t dpos = req.body.find("\"device\":");
            if (dpos != std::string::npos) {
                size_t q1 = req.body.find("\"", dpos + 8);
                if (q1 != std::string::npos) {
                    size_t q2 = req.body.find("\"", q1 + 1);
                    if (q2 != std::string::npos) {
                        device = req.body.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
            size_t cpos = req.body.find("\"channel\":");
            if (cpos != std::string::npos) {
                size_t digit_start = req.body.find_first_of("0123456789", cpos + 9);
                if (digit_start != std::string::npos) {
                    size_t digit_end = req.body.find_first_not_of("0123456789", digit_start);
                    if (digit_end == std::string::npos) digit_end = req.body.size();
                    try {
                        channel = std::stoi(req.body.substr(digit_start, digit_end - digit_start));
                    } catch (...) {}
                }
            }
        }

        if (device.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"missing device\"}");
            return;
        }

        // 查找本地连接
        auto* conn_ctx = find_connection_by_device_id(device);

        if (!conn_ctx) {
            // Redis查找分布式节点
            if (auto* redis = ServerApp::getInstance().GetRedisClient()) {
                std::string key = "device:online:" + device;
                std::string val = redis->Get(key);
                if (!val.empty()) {
                    std::string target_ip;
                    int target_port = 0;

                    size_t pos_port = val.find("\"http_port\":");
                    if (pos_port != std::string::npos) {
                        size_t digit_start = val.find_first_of("0123456789", pos_port + 12);
                        if (digit_start != std::string::npos) {
                            size_t digit_end = val.find_first_not_of("0123456789", digit_start);
                            if (digit_end == std::string::npos) digit_end = val.length();
                            try {
                                target_port = std::stoi(val.substr(digit_start, digit_end - digit_start));
                            } catch (...) {}
                        }
                    }

                    size_t pos_ip = val.find("\"ip\":\"");
                    if (pos_ip != std::string::npos) {
                        size_t start = pos_ip + 6;
                        size_t end = val.find("\"", start);
                        if (end != std::string::npos) {
                            target_ip = val.substr(start, end - start);
                        }
                    }

                    bool is_local = (target_ip == ServerApp::getInstance().GetLocalIp() ||
                                     target_ip == "127.0.0.1" || target_ip == "localhost") &&
                                    (target_port == ServerApp::getInstance().GetHttpPort());

                    if (target_port > 0 && !target_ip.empty() && !is_local) {
                        // 需要代理转发 - 这里简化处理，返回错误
                        // 实际应该使用HTTP Client转发
                        res.SetStatus(502, "Bad Gateway");
                        res.SetJson("{\"ok\":false,\"error\":\"device on another node, proxy not implemented yet\"}");
                        return;
                    }
                }
            }

            res.SetStatus(404, "Not Found");
            res.SetJson("{\"ok\":false,\"error\":\"device not connected or not registered locally\"}");
            return;
        }

        bool success = send_b341_to_fd(conn_ctx->connfd, channel);

        if (!success) {
            res.SetStatus(500, "Internal Server Error");
            res.SetJson("{\"ok\":false,\"error\":\"failed to send B341 command\"}");
            return;
        }

        res.SetJson("{\"ok\":true,\"message\":\"B341 command sent successfully\",\"device\":\"" +
                    device + "\",\"channel\":" + std::to_string(channel) + "}");
    });

    // POST /api/send_b341_by_fd
    server->Post("/api/send_b341_by_fd", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        int fd = -1;
        int channel = 1;

        // 解析fd
        if (!req.body.empty()) {
            size_t pos = req.body.find("\"fd\"");
            if (pos != std::string::npos) {
                size_t colon = req.body.find(':', pos);
                if (colon != std::string::npos) {
                    size_t q1 = req.body.find_first_of("0123456789", colon);
                    if (q1 != std::string::npos) {
                        size_t q2 = req.body.find_first_not_of("0123456789", q1);
                        std::string fd_str = req.body.substr(q1, q2 - q1);
                        fd = std::stoi(fd_str);
                    }
                }
            }

            size_t cpos = req.body.find("\"channel\":");
            if (cpos != std::string::npos) {
                size_t digit_start = req.body.find_first_of("0123456789", cpos + 9);
                if (digit_start != std::string::npos) {
                    size_t digit_end = req.body.find_first_not_of("0123456789", digit_start);
                    if (digit_end == std::string::npos) digit_end = req.body.size();
                    try {
                        channel = std::stoi(req.body.substr(digit_start, digit_end - digit_start));
                    } catch (...) {}
                }
            }
        }

        if (fd < 0) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"missing fd\"}");
            return;
        }

        bool success = send_b341_to_fd(fd, channel);

        if (success) {
            res.SetJson("{\"ok\":true,\"fd\":" + std::to_string(fd) + ",\"channel\":" + std::to_string(channel) + "}");
        } else {
            res.SetStatus(500, "Internal Server Error");
            res.SetJson("{\"ok\":false,\"error\":\"send failed\"}");
        }
    });

    // POST /api/upload_model
    server->Post("/api/upload_model", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string device = req.GetQuery("device");

        if (device.empty() && !req.body.empty()) {
            size_t dpos = req.body.find("\"device\":");
            if (dpos != std::string::npos) {
                size_t q1 = req.body.find("\"", dpos + 8);
                if (q1 != std::string::npos) {
                    size_t q2 = req.body.find("\"", q1 + 1);
                    if (q2 != std::string::npos) {
                        device = req.body.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }

        if (device.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"missing device\"}");
            return;
        }

        auto* conn_ctx = find_connection_by_device_id(device);
        if (!conn_ctx) {
            res.SetStatus(404, "Not Found");
            res.SetJson("{\"ok\":false,\"error\":\"device not found\"}");
            return;
        }

        // 简单实现：返回成功
        res.SetJson("{\"ok\":true,\"message\":\"model upload not fully implemented\"}");
    });

    // POST /api/test_capture_after_upgrade
    server->Post("/api/test_capture_after_upgrade", [](const gw::HttpRequest& req, gw::HttpResponse& res) {
        std::string device = req.GetQuery("device");

        if (device.empty() && !req.body.empty()) {
            size_t dpos = req.body.find("\"device\":");
            if (dpos != std::string::npos) {
                size_t q1 = req.body.find("\"", dpos + 8);
                if (q1 != std::string::npos) {
                    size_t q2 = req.body.find("\"", q1 + 1);
                    if (q2 != std::string::npos) {
                        device = req.body.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }

        if (device.empty()) {
            res.SetStatus(400, "Bad Request");
            res.SetJson("{\"ok\":false,\"error\":\"missing device\"}");
            return;
        }

        auto* conn_ctx = find_connection_by_device_id(device);
        if (!conn_ctx) {
            res.SetStatus(404, "Not Found");
            res.SetJson("{\"ok\":false,\"error\":\"device not found\"}");
            return;
        }

        // 发送测试捕获命令 (使用SendModelToDevice)
        std::string test_image_path = get_upload_dir() + "/test_image.jpg";
        struct stat buffer;
        if (stat(test_image_path.c_str(), &buffer) != 0) {
            test_image_path = get_upload_dir() + "/default.jpg";
            if (stat(test_image_path.c_str(), &buffer) != 0) {
                res.SetStatus(404, "Not Found");
                res.SetJson("{\"ok\":false,\"error\":\"test image not found\"}");
                return;
            }
        }

        int ret = SendModelToDevice(test_image_path.c_str(), 1, conn_ctx->connfd);

        if (ret == 0) {
            res.SetJson("{\"ok\":true,\"device\":\"" + device + "\"}");
        } else {
            res.SetStatus(500, "Internal Server Error");
            res.SetJson("{\"ok\":false,\"error\":\"send failed\"}");
        }
    });

    std::cout << "[HTTP] Routes registered for new HttpServer" << std::endl;
}
