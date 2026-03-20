# 模型升级功能 (Model Upgrade) 机制与优化方案

## 1. 业务目标
通过 Web 前端上传目标设备需要更新的AI引擎或模型文件（.engine），服务器接收后通过私有协议 (B351/B352, B350, B37, B38) 稳定传输到指定的边缘端（PC 或 Jetson Orin Nano），并根据不同的边缘计算架构执行对应的模型加载和重启脚本。

## 2. 核心挑战与解决逻辑
在原有的服务器框架中，`http_server` 的接口回调（处理上传请求的线程）调用底层的 `SendModelToDevice` 时，会直接阻塞等待设备的ACK回复（`waitForB352`, `waitForB38`），底层使用的是 `select` + `read`。
**但是，服务端的 `EventLoop` (Reactor线程) 同时也在非阻塞地监控同一个Socket `EPOLLIN`！**
当设备回复数据时，极易发生争抢（Reactor先抢走数据清空了buffer，导致 `waitForB352` 超时；或者 `waitForB352` 在不恰当的时候读取了不完整数据），造成“缺失包”甚至连接断开的问题。

### 2.1 Reactor与阻塞IO协调机制
我们在 `http_server.cpp` 层引入了**动态解绑机制**：
- 当服务器主动发起大规模模型传输前，先调用 `ServerApp::getInstance().getEventLoop().RemoveSocket(connfd)` 将该连接暂时剥离 Reactor 管管。
- 此阶段由 HTTP API 线程完全接管 Socket，进行稳定的同步阻塞读写 (Blocking IO)。
- 传输完毕（包括结束握手确认后），再通过 `AddSocket(...)` 把该 Socket 重新绑定回 Reactor 以恢复常规心跳或命令监听。

### 2.2 协议封包长度 Bug 修复
在分析缺失包时发现，之前在客户端解析收到的 `0xF0` 包的时候，长度计算有误：
`dataLen = pPacket->packetLength - 40;`
头尾协议开销共计 40 字节（而非之前的 33 字节，因为 `int prefix_sample[2];` 是 8 字节以及其他各种首尾标识的总和）。修正后保证了准确截取 1024 字节数据区，彻底消除了越界或漏包。

## 3. 架构适配：Jetson Orin Nano 与 PC 脚本下发差异
由于不同架构芯片部署升级的要求不同，客户端在升级完成后引入了跨平台兼容设计：
1. **自动架构检测**：在 `ClientApp.cpp` 使用 `sys/utsname.h` 的 `uname` 函数，判断系统是 `aarch64` (Jetson) 还是 `x86_64` (PC)。
2. **区别前缀脚本**：通过给要拉起的升级脚本追加前缀：
   - 比如 `aarch64` 触发 `jetson_exposure_update_model.sh`（内部逻辑为转移模型 -> 环境变量配置 -> 清理缓存 -> 重新 make TensorRT）。
   - `x86_64` 触发 `pc_exposure_update_model.sh` （目前仅做 mv 以及打印模拟日志）。
3. 所有指令都是通过 `system("bash ...")` 下发拉起。

## 4. 链路闭环验证
通过上传大体积的 `ImageSend` ELF执行文件（模拟近百KB或MB级.engine文件）：
* Server 将文件保存到 `resource/engines/`，通知 Client (`B351`)；
* Client 在另起的独立读取线程中，持续解包并存储；
* 传输结束后，校验 `recvStatus` 位图，稳定达到 `缺失包数为 0`，双向 `B38` 回调成功。
* HTTP 以 200 OK 返回，客户端随即拉起对应平台的 `.sh` 脚本执行后处理。
