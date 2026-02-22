#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>

namespace gw {

// HTTP方法枚举
enum class HttpMethod {
    kGet = 0,
    kPost = 1,
    kPut = 2,
    kDelete = 3,
    kPatch = 4,
    kOptions = 5,
    kHead = 6,
    kMaxMethod
};

inline const char* MethodToString(HttpMethod m) {
    switch (m) {
        case HttpMethod::kGet: return "GET";
        case HttpMethod::kPost: return "POST";
        case HttpMethod::kPut: return "PUT";
        case HttpMethod::kDelete: return "DELETE";
        case HttpMethod::kPatch: return "PATCH";
        case HttpMethod::kOptions: return "OPTIONS";
        case HttpMethod::kHead: return "HEAD";
        default: return "UNKNOWN";
    }
}

inline HttpMethod StringToMethod(const std::string& s) {
    if (s == "GET") return HttpMethod::kGet;
    if (s == "POST") return HttpMethod::kPost;
    if (s == "PUT") return HttpMethod::kPut;
    if (s == "DELETE") return HttpMethod::kDelete;
    if (s == "PATCH") return HttpMethod::kPatch;
    if (s == "OPTIONS") return HttpMethod::kOptions;
    if (s == "HEAD") return HttpMethod::kHead;
    return HttpMethod::kMaxMethod;
}

// HTTP请求结构
struct HttpRequest {
    HttpMethod method;
    std::string uri;
    std::string version;
    std::string body;

    // Header存储
    std::unordered_map<std::string, std::string> headers;

    // 查询参数 (解析后存储)
    std::unordered_map<std::string, std::string> query_params;

    // 获取单个header
    std::string GetHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) return it->second;
        return "";
    }

    // 获取Content-Length
    size_t content_length() const {
        auto it = headers.find("Content-Length");
        if (it != headers.end()) {
            try { return std::stoul(it->second); }
            catch (...) { return 0; }
        }
        return 0;
    }

    // 获取query参数
    std::string GetQuery(const std::string& key) const {
        auto it = query_params.find(key);
        if (it != query_params.end()) return it->second;
        return "";
    }
};

// HTTP响应结构
struct HttpResponse {
    int status_code = 200;
    std::string status_message;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    HttpResponse() : status_message("OK") {}

    void SetStatus(int code, const std::string& msg = "") {
        status_code = code;
        status_message = msg.empty() ? DefaultStatusMessage(code) : msg;
    }

    void SetHeader(const std::string& key, const std::string& value) {
        headers[key] = value;
    }

    void SetContent(const std::string& content, const std::string& content_type) {
        body = content;
        headers["Content-Type"] = content_type;
        headers["Content-Length"] = std::to_string(content.size());
    }

    void SetJson(const std::string& json) {
        SetContent(json, "application/json");
    }

    void SetText(const std::string& text) {
        SetContent(text, "text/plain");
    }

    void SetHtml(const std::string& html) {
        SetContent(html, "text/html");
    }

    void SetFile(const std::string& filepath, const std::string& content) {
        body = content;
        std::string ext = filepath.substr(filepath.rfind('.') + 1);
        std::string mime = GetMimeType(ext);
        headers["Content-Type"] = mime;
        headers["Content-Length"] = std::to_string(content.size());
    }

    static std::string GetMimeType(const std::string& ext) {
        static const std::unordered_map<std::string, std::string> mime_types = {
            {"html", "text/html"}, {"htm", "text/html"},
            {"css", "text/css"}, {"js", "application/javascript"},
            {"json", "application/json"}, {"xml", "application/xml"},
            {"txt", "text/plain"},
            {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
            {"gif", "image/gif"}, {"svg", "image/svg+xml"}, {"ico", "image/x-icon"},
            {"pdf", "application/pdf"}, {"zip", "application/zip"},
            {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"},
        };
        auto it = mime_types.find(ext);
        return it != mime_types.end() ? it->second : "application/octet-stream";
    }

    static std::string DefaultStatusMessage(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 304: return "Not Modified";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            default: return "Unknown";
        }
    }

    // 序列化为HTTP响应字符串
    std::string ToString() const {
        std::string result;
        result.reserve(512 + body.size());

        result += "HTTP/1.1 ";
        result += std::to_string(status_code);
        result += " ";
        result += status_message;
        result += "\r\n";

        // 默认headers
        if (headers.find("Connection") == headers.end()) {
            result += "Connection: close\r\n";
        }

        // 添加所有headers
        for (const auto& h : headers) {
            result += h.first;
            result += ": ";
            result += h.second;
            result += "\r\n";
        }

        result += "\r\n";
        result += body;
        return result;
    }
};

// HTTP解析器
class HttpParser {
public:
    enum class ParseState {
        kMethod,       // 解析方法
        kUri,          // 解析URI
        kVersion,      // 解析版本
        kHeaderKey,    // 解析Header Key
        kHeaderValue,  // 解析Header Value
        kHeadersDone,  // Header解析完成
        kBody,         // 解析Body
        kComplete,     // 解析完成
        kError         // 解析错误
    };

    HttpParser() : state_(ParseState::kMethod) {}

    // 解析数据，返回是否完成
    bool parse(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];

            if (state_ == ParseState::kError) {
                return false;
            }

            switch (state_) {
                case ParseState::kMethod:
                    if (c == ' ') {
                        if (!method_str_.empty()) {
                            request_.method = StringToMethod(method_str_);
                            state_ = ParseState::kUri;
                        } else {
                            state_ = ParseState::kError;
                        }
                    } else {
                        method_str_ += c;
                    }
                    break;

                case ParseState::kUri:
                    if (c == ' ') {
                        state_ = ParseState::kVersion;
                    } else {
                        uri_ += c;
                    }
                    break;

                case ParseState::kVersion:
                    if (c == '\r') {
                        // HTTP/1.1 or HTTP/1.0
                        state_ = ParseState::kHeaderKey;
                    } else {
                        version_ += c;
                    }
                    break;

                case ParseState::kHeaderKey:
                    if (c == '\r') {
                        // 空行，header结束
                        state_ = ParseState::kHeadersDone;
                    } else if (c == ':') {
                        state_ = ParseState::kHeaderValue;
                        // 跳过空格
                        if (i + 1 < len && data[i + 1] == ' ') i++;
                    } else if (c != ' ') {
                        header_key_ += c;
                    }
                    break;

                case ParseState::kHeaderValue:
                    if (c == '\r') {
                        // header完成
                        request_.headers[header_key_] = header_value_;
                        header_key_.clear();
                        header_value_.clear();
                        state_ = ParseState::kHeaderKey;
                    } else if (c != '\n') {
                        header_value_ += c;
                    }
                    break;

                case ParseState::kHeadersDone:
                    if (c == '\n') {
                        // 检查是否有body
                        size_t content_len = request_.content_length();
                        if (content_len > 0) {
                            body_remaining_ = content_len;
                            state_ = ParseState::kBody;
                        } else {
                            state_ = ParseState::kComplete;
                            return true;
                        }
                    }
                    break;

                case ParseState::kBody:
                    body_ += c;
                    body_remaining_--;
                    if (body_remaining_ == 0) {
                        request_.body = body_;
                        ParseQueryParams();
                        state_ = ParseState::kComplete;
                        return true;
                    }
                    break;

                default:
                    break;
            }
        }
        return false;
    }

    // 重置解析器
    void reset() {
        state_ = ParseState::kMethod;
        method_str_.clear();
        uri_.clear();
        version_.clear();
        header_key_.clear();
        header_value_.clear();
        body_.clear();
        body_remaining_ = 0;
        request_ = HttpRequest();
    }

    // 获取解析结果
    const HttpRequest& request() const { return request_; }
    HttpRequest& request() { return request_; }
    ParseState state() const { return state_; }
    bool is_complete() const { return state_ == ParseState::kComplete; }
    bool is_error() const { return state_ == ParseState::kError; }

    // 获取URI (原始)
    const std::string& uri() const { return uri_; }

private:
    ParseState state_;
    std::string method_str_;
    std::string uri_;
    std::string version_;
    std::string header_key_;
    std::string header_value_;
    std::string body_;
    size_t body_remaining_ = 0;
    HttpRequest request_;

    void ParseQueryParams() {
        size_t pos = uri_.find('?');
        if (pos != std::string::npos) {
            std::string query = uri_.substr(pos + 1);
            request_.uri = uri_.substr(0, pos);

            // 解析query参数
            size_t start = 0;
            while (start < query.size()) {
                size_t eq = query.find('=', start);
                size_t amp = query.find('&', start);

                if (eq == std::string::npos) break;

                std::string key = query.substr(start, eq - start);
                std::string value;

                if (amp == std::string::npos) {
                    value = query.substr(eq + 1);
                    start = query.size();
                } else {
                    value = query.substr(eq + 1, amp - eq - 1);
                    start = amp + 1;
                }

                // URL解码 (简单实现)
                request_.query_params[UrlDecode(key)] = UrlDecode(value);
            }
        } else {
            request_.uri = uri_;
        }
    }

    std::string UrlDecode(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                int val;
                std::istringstream iss(s.substr(i + 1, 2));
                if (iss >> std::hex >> val) {
                    result += static_cast<char>(val);
                    i += 2;
                } else {
                    result += s[i];
                }
            } else if (s[i] == '+') {
                result += ' ';
            } else {
                result += s[i];
            }
        }
        return result;
    }
};

} // namespace gw
