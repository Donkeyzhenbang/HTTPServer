#ifndef REDISCLIENT_H
#define REDISCLIENT_H

#include <string>
#include <mutex>
#include <hiredis/hiredis.h>

class RedisClient
{
public:
    RedisClient();
    ~RedisClient();

    bool Connect(const std::string& host, int port);
    void Disconnect();

    bool Set(const std::string& key, const std::string& value, int expirySeconds = 0);
    std::string Get(const std::string& key);
    bool Del(const std::string& key);

private:
    redisContext* m_context;
    std::string m_host;
    int m_port;
    std::mutex m_mutex;
};

#endif
