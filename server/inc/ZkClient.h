#ifndef ZKCLIENT_H
#define ZKCLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <zookeeper/zookeeper.h>

class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    void Start(const std::string& host);
    void Create(const std::string& path, const std::string& data, int state = 0);
    std::string GetData(const std::string& path);
    void Close();

private:
    zhandle_t *m_zhandle;
    std::string m_host;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_connected;
    
    // Static watcher for C API
    static void watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx);
};

#endif
