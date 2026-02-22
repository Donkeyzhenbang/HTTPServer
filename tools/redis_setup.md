# Redis 远程访问配置步骤

您的第二台服务器 (Node B, `172.19.186.12`) 无法连接第一台服务器 (Node A, `172.19.186.11`) 上的 Redis。
错误信息：`[Redis] Connection error: Connection refused`

这是因为 Redis 默认处于**保护模式**且只监听**本地回环地址 (127.0.0.1)**。您需要修改第一台服务器上的 Redis 配置。

### 步骤 1: 修改 Redis 配置文件 (在第一台服务器上操作)

1.  **找到配置文件**：通常位于 `/etc/redis/redis.conf`。
    ```bash
    sudo vim /etc/redis/redis.conf
    ```

2.  **修改绑定地址 (bind)**：
    找到 `bind 127.0.0.1 ::1` 这一行。
    *   **修改为**：`bind 0.0.0.0` (允许所有 IP 连接)
    *   或者：`bind 127.0.0.1 172.19.186.11` (只允许本地和内网 IP)

3.  **关闭保护模式 (protected-mode)**：
    找到 `protected-mode yes`。
    *   **修改为**：`protected-mode no`
    *   *(注意：关闭保护模式后，Redis 将暴露在内网中。如果您的阿里云安全组已限制公网访问 6379 端口，这通常是安全的。或者您可以设置 `requirepass` 密码，但那样需要修改我们的 C++ 代码来支持密码认证。目前建议先关闭保护模式跑通功能。)*

### 步骤 2: 重启 Redis 服务 (在第一台服务器上操作)

```bash
sudo systemctl restart redis-server
```

### 步骤 3: 验证配置

在第一台服务器上查看 Redis 是否监听了 `0.0.0.0:6379`：

```bash
netstat -tulnp | grep 6379
# 输出应该包含 0.0.0.0:6379
```

### 步骤 4: 重试第二台服务器

再次在第二台服务器上启动 `httpserver`，应该就能连接成功了。

---

### 关于 ZooKeeper 错误的说明
日志中显示：`[ZK] Create failed for /gw-server/nodes/node_172.19.186.12_52487, code: -101`

*   **原因**：`-101` 表示节点已存在 (`ZNODEEXISTS`)。这说明您的第二台服务器之前已经成功在 ZooKeeper 上注册过一次，且连接未断开（或者是上次非正常退出，临时节点还没过期）。
*   **处理**：这是**正常现象**，不影响使用。程序逻辑中，如果节点已存在，会忽略创建错误并继续运行，此时实际上它已经“注册”成功了（或者说复用了之前的注册）。
