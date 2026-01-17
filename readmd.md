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

## 客户端B341协议修正
```cpp
int waitForB341(int fd) 
{
    int len = read(fd, buffer, 1024);
    if(len < 0) {
        printf("B341 Socket Read出错\n");
        return -1;
    }
    
    int ret;
    if((ret = CheckFrameFull(buffer, len)) < 0) {
        printf("帧解析出错，不完整，错误码%d\n", ret);
        deBugFrame(buffer, len);
        return -1;
    }
    
    u_int8 frameType, packetType;
    getFramePacketType(buffer, &frameType, &packetType);
    
    if(frameType == 0x07 && packetType == 0xEE) {
        printf("接收到B341协议\n");
        deBugFrame(buffer, len);
        
        // 提取通道号 - 根据B341报文格式
        // 计算偏移量：
        // Sync(2) + Packet_Length(2) + CMD_ID(17) + Frame_Type(1) + Packet_Type(1) + Frame_No(1) = 24字节
        // Channel_No是第25个字节（从1开始计数），索引为24（从0开始）
        
        if(len >= 25) {  // 确保报文足够长
            u_int8 channelNo = buffer[24];  // 第25个字节是通道号
            printf("B341报文中的通道号: %d\n", channelNo);
            
            // 如果需要，这里可以保存通道号到全局变量或返回
            // global_channel = channelNo;  // 假设有全局变量
            
            // 返回通道号作为成功（正整数）或0
            return (int)channelNo;
        } else {
            printf("B341报文长度不足，无法提取通道号\n");
            return -3;
        }
    }
    
    printf("收到其他包，没有收到B341\n");
    return -2;
}
```