## 服务器
- nginx端口80端口 负责转发所有请求
- 其中HTTP与前端界面端口在8080端口 HTML状态渲染数据也是依靠HTTPServer的/api/device也是依靠请求来获取信息
- agent服务在8000端口
- 后端设备连接服务在52487端口

## 启动
- source ~/agent-venv/bin/activate
- python main.py
- 启动nginx服务
- 启动服务器即可
- 如果缺少环境依赖 pip install -r requirements.txt 安装相应的依赖


## nginx
- 之前部署的nginx还是可以用的，nginx配置文件在 /etc/nginx/sites-available/transmission-platform
- 因为之前只部署了192.168.126.128 没有部署监听localhost 127.0.0.1 不上即可
- agent部署也是在8000 端口即可
- 精准查找SSH端口转发的进程（排除grep本身） ps aux | grep "ssh -L\|ssh -R" | grep -v grep 可以找到具体哪个后台进程再跑
- 测试Nginx配置是否正确 sudo nginx -t
- 重新加载Nginx配置（不中断服务） sudo nginx -s reload
- 或者重启Nginx服务 sudo systemctl restart nginx

## 端口！！！## 阿里云已开启端口
- 80端口是Nginx对外统一服务端口
- 8080 端口是C++后端+前端
- 8000 端口是agent端口
- 1316 之前使用开发端口
- 29016 29017 29018 之前做分布式网络
- 52487 后端端口
- 8888 可用端口

## 主机访问
- netstat -ano | findstr :8888 检查本地8888端口是否被监听
- SSH正向代理 ssh -L8888:localhost:80 jym@192.168.126.128 将本地8888端口转发到服务器的80端口也即是Nginx服务入口
- 修改nginx配置后，需要sudo nginx -t 查看语法对错，然后使用sudo systemctl restart nginx即可 
- ssh -L8888:localhost:80 jym@192.168.126.128        

## tmux
- tmux ls 
- tmux attach -t 0 #连接某个窗口

## agent
- python3 -m venv agent-venv创建虚拟环境
- pip install -r requirements.txt -i https://mirrors.aliyun.com/pypi/simple/
- .venv注意需要指定DASHSCOPE
- nginx需要配置好重启 sudo systemctl restart nginx


## 编译 scp 到服务器
```sh
#!/bin/sh
make clean && make -j8
cd /home/jym/code/cpp/personal-project/gw-server/bin
./SocketServer
scp ./bin/SocketServer jym@47.121.121.86:/home/jym/
# scp ./bin/SocketServer mv@47.122.114.144:/home/mv/
```
## 代码行数检测
```sh
wc -l `find . -name "*.cpp";find . -name "*.h";find . -name "*.py";find . -name "*.html";find . -name "*.js"`

find .. \
  \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.html" -o -name "*.js" \) \
  -not -name "httplib.h" \
  -not -path "../client/src/jsondist/*" \
  -exec wc -l {} +
```

## 关于测试
- 测试单张图片能发送
- 测试多张图片要发送
- 测试客户端可用静默链接
- 测试传图中断可用处理
- 任何错误处理都不能影响主程序继续接受其他链接
- 测试主动要图
- 后续需要添加GTEST测试


## 预期流程
预期流程：
- 用户在前端选择模型文件和目标设备
- 点击"开始升级"后，前端通过FormData发送到 /api/upload_model
- 后端接收文件，验证设备连接
- 保存模型文件到本地（备份）
- 调用设备升级协议发送模型文件
- 返回结果给前端
- 前端显示升级进度和结果

前端点击升级
    ↓
POST /api/model_upgrade
    ↓
后端验证并保存模型文件
    ↓
异步启动 ModelUpgradeHandler
    ↓
    ↓ (异步执行)
发送B361 → 等待B362
发送B363 → 等待B364
发送模型数据 → 
发送B365 → 等待B366
    ↓
升级完成，清理临时文件


## 客户端未来改进方向
- protocol单列，依赖于配置文件而不是依赖于C++文件，客户端协议文件也方便用于保护和修改
- 增加GTEST， 将多个服务器测试改为GTEST
- makefile修改为cmake


## 
- 服务器架构设计 这里先是AddSocket注册回调函数，acceptLoop接入服务器之后会
```cpp
    eventLoop.AddSocket(serverSocket, EPOLLIN | EPOLLET, [this](int fd){
        this->acceptLoop();
    });

```
- acceptLoop对于接入服务器之后会调用另一个回调函数handleNewConnection
```cpp
void ServerApp::acceptLoop() {
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    char ip_str[INET_ADDRSTRLEN] = {0};

    while (true) {
        int connfd = accept(serverSocket, (struct sockaddr*)&client_addr, &addrlen);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; 
            }
            perror("accept error");
            break;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "客户端连接: IP地址: " << ip_str 
                  << "; 端口号: " << ntohs(client_addr.sin_port) 
                  << "; fd=" << connfd << std::endl;

        handleNewConnection(connfd);
    }
}
```
- handleNewConnection这里AddSoekct执行的就是业务逻辑OnClientRead 创建连接上下文 读入到client
```cpp
void ServerApp::handleNewConnection(int connfd) {
    // Set Non-Blocking
    int flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);

    // Initialise Context
    create_connection_context(connfd);

    // Add to Reactor
    eventLoop.AddSocket(connfd, EPOLLIN | EPOLLET | EPOLLRDHUP, [](int fd){
        OnClientRead(fd);
    });
    
    // Send immediate heartbeat response if needed or wait for client?
    // Old code waited for HeartbeatFrame.
    // We just let `OnClientRead` handle the incoming packet.
}
```

- 读入到对应上下文之后，会推送到ThreadPool中进行处理
```cpp
void OnClientRead(int fd) {
    // Use shared_ptr to ensure safety
    auto ctxPtr = get_connection_shared_ptr(fd);
    if (!ctxPtr) return;
    
    ConnectionContext* ctx = ctxPtr.get();

    {
        std::lock_guard<std::mutex> lock(ctx->bufferMutex);
        // Read directly into vector
        char buf[4096];
        while(true) {
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno != EINTR) {
                    // unexpected error
                    perror("Read Error");
                    ctx->is_connection_alive = false;
                    CloseConnection(fd);
                    return;
                }
                break;
            }
            if (n == 0) {
                // EOF
                ctx->is_connection_alive = false;
                CloseConnection(fd);
                return;
            }
            ctx->inputBuffer.insert(ctx->inputBuffer.end(), buf, buf + n);
        }
    }
    
    // Trigger Processing Task if not already running
    bool expected = false;
    if (ctx->is_processing.compare_exchange_strong(expected, true)) {
        // Enqueue task
        ServerApp::getInstance().getThreadPool().enqueue([fd](){
            ProcessConnection(fd);
        });
    }
}

```

- ProcessConnection中会处理包长，然会根据交由ServerFrameResolver进行预先注册好的回调函数进行dispatch
```cpp
int ServerFrameResolver(unsigned char* pBuffer, int Length ,int sockfd)
{
    u_int8 frameType,packetType;
    getFramePacketType(pBuffer, &frameType, &packetType);
    //根据帧类型和包类型，调用不同的处理函数
    //查表，然后处理吗？
    for(int i=0;i<sizeof(Handlers)/sizeof(HandlerFun);i++) {
        if(Handlers[i].frameType == frameType && Handlers[i].packetType == packetType) {   
            if(Handlers[i].func!=NULL) {
                Handlers[i].func(pBuffer,Length,sockfd);
            }
            return 0;
        }
    }
    //没找到，处理
    printf("未处理协议 frameType = 0x%x, packetType = 0x%x\n",frameType,packetType);
    return -1;
}

```