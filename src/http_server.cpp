// src/http_server.cpp
#include "http_server.h"
#include "httplib.h"   // 你上传的单文件 httplib.h（以此版本为准）
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <dirent.h>
#include <sys/socket.h> // send()
#include <unistd.h>     // close()
#include <ctime>
#include <sstream>

// 你的 main.cpp 应该定义这两个全局符号（见下文集成部分）：
extern std::unordered_map<std::string, int> g_device_map;
extern std::mutex g_device_mtx;

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
        // fallback to "."
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
    // simple approach: try mkdir with mode 0755; if exists, ok
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return true;
        return false;
    }
    // create parent recursively (naive approach)
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' || i + 1 == dir.size()) {
            if (cur.empty()) continue;
            // remove trailing slash
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
    // final check
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// 全局（静态）upload dir computed once
static std::string g_upload_dir;

// 修改 save_upload_to_web：使用绝对 path
static std::string save_upload_to_web(const std::string &filename,
                                      const std::string &content) {
    if (g_upload_dir.empty()) {
        std::string exe_dir = get_exe_dir(); // e.g. /home/jym/.../bin
        g_upload_dir = exe_dir + "/web/uploads";
    }

    if (!ensure_dir_exists(g_upload_dir)) {
        std::cerr << "[HTTP] ensure_dir_exists failed: " << g_upload_dir << std::endl;
        return "";
    }

    std::ostringstream oss;
    oss << std::time(nullptr) << "_" << filename;
    std::string saved = oss.str();
    std::string path = g_upload_dir + "/" + saved;

    // debug print
    std::cerr << "[HTTP] saving upload to: " << path << " (size=" << content.size() << ")\n";

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[HTTP] 无法打开写入文件: " << path << " errno=" << errno << "\n";
        return "";
    }
    ofs.write(content.data(), (std::streamsize)content.size());
    ofs.close();

    return saved; // note: caller will prepend /uploads/ when returning URL
}

void start_http_server() {
    httplib::Server svr;

    // 静态资源挂载：把 web/ 目录作为静态根目录
    // set_mount_point(mount_point, dir)
    svr.set_mount_point("/", "/home/jym/code/cpp/personal-project/gw-server/bin/web");

    // 增大允许的 payload（可按需调整）
    svr.set_payload_max_length(50 * 1024 * 1024); // 50MB

    // POST /upload - multipart form upload: field name = "image"
    svr.Post("/upload", [](const httplib::Request &req, httplib::Response &res) {
        // 使用你给的 httplib 版本中的 MultipartFormData API
        // 检查 multipart 文件是否存在
        if (!req.form.has_file("image")) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"no file field 'image'"})", "application/json");
            return;
        }
        // 取第一个文件
        auto file = req.form.get_file("image", 0); // FormData
        if (file.filename.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"empty filename"})", "application/json");
            return;
        }
        // 保存到 web/uploads
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

    // GET /api/images -> 返回 JSON 列表
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

    // GET /api/devices -> 返回已注册设备 id 列表（从主程序维护的 map 中读取）
    svr.Get("/api/devices", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lg(g_device_mtx);
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (const auto &p : g_device_map) {
            if (!first) oss << ",";
            oss << "\"" << p.first << "\"";
            first = false;
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    // POST /api/request_snapshot
    // 支持两种调用方式：
    //  1) POST body JSON-ish: {"device":"dev001","channel":1}
    //  2) URL 查询参数: /api/request_snapshot?device=dev001&channel=1
    svr.Post("/api/request_snapshot", [](const httplib::Request &req, httplib::Response &res) {
        // 优先尝试从查询参数获取
        std::string device;
        std::string channel;
        if (req.has_param("device")) device = req.get_param_value("device", 0);
        if (req.has_param("channel")) channel = req.get_param_value("channel", 0);

        // 若没有，尝试从 body 里做简单解析（适用于前端直接发 JSON 字符串）
        if (device.empty()) {
            // 简单查找 "device":"xxx" 或 "device": "xxx"
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
                    } else {
                        // 可能是数字/无引号（不常见）
                    }
                }
            }
        }
        if (channel.empty()) {
            auto pos = req.body.find("\"channel\"");
            if (pos != std::string::npos) {
                auto colon = req.body.find(':', pos);
                if (colon != std::string::npos) {
                    // 找数字字符
                    size_t i = colon+1;
                    while (i < req.body.size() && isspace((unsigned char)req.body[i])) ++i;
                    size_t j = i;
                    while (j < req.body.size() && (isdigit((unsigned char)req.body[j]) || req.body[j]=='-')) ++j;
                    if (j > i) channel = req.body.substr(i, j-i);
                }
            }
        }

        if (device.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"missing device\"}", "application/json");
            return;
        }
        if (channel.empty()) channel = "1";

        // 生成协议（按你项目实际协议替换）
        std::ostringstream proto;
        proto << "CMD:SNAPSHOT;CH:" << channel << "\n";
        std::string proto_s = proto.str();

        // 找设备 fd 并发送
        int fd = -1;
        {
            std::lock_guard<std::mutex> lg(g_device_mtx);
            auto it = g_device_map.find(device);
            if (it != g_device_map.end()) fd = it->second;
        }

        if (fd < 0) {
            res.set_content("{\"ok\":false,\"error\":\"device not connected\"}", "application/json");
            return;
        }

        ssize_t n = ::send(fd, proto_s.c_str(), proto_s.size(), 0);
        if (n <= 0) {
            res.set_content("{\"ok\":false,\"error\":\"send failed\"}", "application/json");
            return;
        }

        res.set_content("{\"ok\":true}", "application/json");
    });

    std::cout << "[HTTP] start listen 0.0.0.0:8080\n";
    svr.listen("0.0.0.0", 8080);
}
