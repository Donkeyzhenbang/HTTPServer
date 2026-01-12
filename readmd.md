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
wc -l `find src inc -name "*.cpp";find src inc -name "*.h"`
```

## 等待心跳包协议
```cpp
int waitForHeartBeat(int fd) 
{
    int len = read(fd,buffer,1024);
    if(len < 0) {
        //出错
        printf("Heart Socket Read出错\n");
        exit(EXIT_FAILURE);
        // return -1;
    }
    int ret;
    if((ret = CheckFrameFull(buffer, len))<0) {
        printf("帧解析出错，不完整，错误码%d\n",ret);
        deBugFrame(buffer,len);
        exit(EXIT_FAILURE);
        // return -1;
    }
    u_int8 frameType,packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    if(frameType == 0x09 && packetType == 0xE6) {
        printf("接收到心跳协议\n");
        deBugFrame(buffer,len);
        return 0;
    }
    printf("收到其他包，没有收到心跳协议\n");
    return -2;
}
```

## recv_and_resolve
```cpp
/**
 * @brief 改进版：读socket，区分空闲状态和接收中状态
 * 
 * @param sockfd 
 * @param pPacket 
 * @param pQueue 连接的队列
 * @param pIsConnectionAlive 指向连接是否存活的标志
 * @return int 返回0代表正常，-1代表连接断开，-2代表接收超时
 */
int recv_and_resolve(int sockfd, Packet_t* pPacket, MyQueue* pQueue, std::atomic<bool>* pIsConnectionAlive)
{
    enum Rx_Status {Rx_Sync_1,Rx_Sync_2,Rx_Length_1,Rx_Length_2,Rx_Data,Rx_End};
    enum Rx_Status rx_status = Rx_Sync_1;
    unsigned short DataSize;//应接收的数据字节
    unsigned short RxDataNum = 0;//已接收的数据字节
    bool packetBufferStatus = false;//
    
    // 分离两个计数器
    int packet_timeout_cnt = 0;       // 接收包过程中的超时计数（严格）
    
    while(!packetBufferStatus){
        
        // 1. 检查TCP物理连接是否已由读线程标记为断开
        if(pIsConnectionAlive != NULL && pIsConnectionAlive->load() == false) {
            printf("检测到读线程已断开连接，退出解析\n");
            return -1;
        }

        int size = pQueue->size();
        
        // 2. 队列为空时的处理逻辑
        if(size == 0) {
            struct timespec req = {0, 100000000L}; // 100 毫秒
            nanosleep(&req, (struct timespec *)NULL);

            // --- 核心修改逻辑开始 ---
            if (rx_status == Rx_Sync_1) {
                // Case A: 处于【空闲状态】 (还在等包头第一个字节)
                // 允许无限等待，或者依赖 TCP Keepalive。
                packet_timeout_cnt = 0;  // 重置超时计数器
                continue;
            }
            else {
                // Case B: 处于【接收中状态】 (已经收到了 0xA5，包收到一半卡住了)
                packet_timeout_cnt++;
                
                // 100ms * 30 = 3秒。如果3秒内没把剩下的包传完，认为坏包或客户端挂掉
                if (packet_timeout_cnt >= 30) {
                    printf("接收报文超时中断 (状态机卡在状态: %d)\n", rx_status);
                    return -1; // 报文传输中断，断开连接
                }
            }
            // --- 核心修改逻辑结束 ---
            continue;
        }

        // 3. 队列有数据，开始处理
        // 只要读到了数据，就重置由于卡顿产生的超时计数
        packet_timeout_cnt = 0;

        // 处理队列中的数据
        for(int i=0;i<size;i++) {
            unsigned char rx_temp = pQueue->front();
            pQueue->pop();
            switch(rx_status){
                case Rx_Sync_1://接收包头
                    rx_status = rx_temp == 0xA5 ? Rx_Sync_2:Rx_Sync_1;
                    pPacket->packetBuffer[0] = rx_temp;
                    break;
                case Rx_Sync_2:
                    rx_status = rx_temp == 0x5A ? Rx_Length_1:Rx_Sync_1;
                    pPacket->packetBuffer[1] = rx_temp;
                    break;
                case Rx_Length_1://收到
                    DataSize = rx_temp;
                    rx_status = Rx_Length_2;
                    pPacket->packetBuffer[2] = rx_temp;
                    break;
                case Rx_Length_2:
                    DataSize += rx_temp*256 + 27;
                    pPacket->packetLength =  DataSize;
                    DataSize -= 5;
                    rx_status = Rx_Data;
                    pPacket->packetBuffer[3] = rx_temp;
                    RxDataNum = 0;
                    break;
                case Rx_Data:
                    ++ RxDataNum;
                    pPacket->packetBuffer[3+RxDataNum] = rx_temp;
                    rx_status =  RxDataNum == DataSize?Rx_End:Rx_Data;
                    break;
                case Rx_End:
                    pPacket->packetBuffer[3+RxDataNum+1] = rx_temp;
                    if( rx_temp != 0x96 ) {
                        printf("包尾校验错误: %02X\n", rx_temp);
                        rx_status = Rx_Sync_1;
                        // 校验失败，丢弃当前包，继续接收下一个包
                        i = size;
                        continue;
                    }else{
                        packetBufferStatus = true;
                    }
                    rx_status = Rx_Sync_1;
                    i = size;
                    break;
            }
        }
        
        if(packetBufferStatus) {
            int ret = CheckFrameFull(pPacket->packetBuffer,pPacket->packetLength);
            if(-1 == ret) {
                printf("整体帧校验错误\n");
                return -2;
            }
            return 0;
        }
    }
    return 0;
}
```