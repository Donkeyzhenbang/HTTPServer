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