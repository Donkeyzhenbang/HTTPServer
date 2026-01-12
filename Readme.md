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
- # 精准查找SSH端口转发的进程（排除grep本身） ps aux | grep "ssh -L\|ssh -R" | grep -v grep 可以找到具体哪个后台进程再跑

## 端口！！！
- 80端口是Nginx对外统一服务端口
- 8080 端口是C++后端+前端
- 8000 端口是agent端口

## 主机访问
- netstat -ano | findstr :8888 检查本地8888端口是否被监听
- SSH正向代理 ssh -L8888:localhost:80 jym@192.168.126.128 将本地8888端口转发到服务器的80端口也即是Nginx服务入口
- 修改nginx配置后，需要sudo nginx -t 查看语法对错，然后使用sudo systemctl restart nginx即可         

## tmux
- tmux ls 
- tmux attach -t 0 #连接某个窗口
