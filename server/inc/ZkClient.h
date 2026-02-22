#ifndef ZKCLIENT_H
#define ZKCLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <zookeeper/zookeeper.h>

// ZK节点信息
struct ZkNodeInfo {
    std::string path;
    std::string data;
    int version = 0;
};

// Watcher回调类型
using WatcherCallback = std::function<void(int type, int state, const std::string& path)>;

class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    // 基础操作
    void Start(const std::string& host);
    void Create(const std::string& path, const std::string& data, int state = 0);
    std::string GetData(const std::string& path);
    void Close();

    // 扩展API
    // 获取子节点列表
    std::vector<std::string> GetChildren(const std::string& path);

    // 设置Watcher (基于路径)
    void SetWatcher(const std::string& path, WatcherCallback callback);

    // 删除节点
    bool Delete(const std::string& path);

    // 检查节点是否存在
    bool Exists(const std::string& path);

    // 同步创建 (带超时)
    bool CreateSync(const std::string& path, const std::string& data, int state = 0, int timeout_ms = 5000);

    // 获取连接状态
    bool IsConnected() const { return m_connected; }

    // 创建递归路径
    bool CreatePathRecursive(const std::string& path);

    // 公共成员 (供DistributedCoord使用)
    zhandle_t *m_zhandle;

private:
    std::string m_host;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_connected;

    // Watcher回调映射
    std::unordered_map<std::string, WatcherCallback> watchers_;
    std::mutex watchers_mutex_;

    // Static watcher for C API
    static void watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx);

    // 处理Watcher事件
    void handle_watcher_event(int type, int state, const std::string& path);
};

#endif
