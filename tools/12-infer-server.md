# 大规模多模态推理网关与微服务架构设计 (工业级/国网场景)

基于您的反馈（使用 Epoll 模拟 Proactor、解耦独立推理微服务、维持 httplib），本方案针对类似国网这类需要处理海量图像、多种算法并发（融合、YOLO、分割）、多显卡算力调度的场景，重新规划了系统的全景架构。

## 1. 整体系统架构蓝图
在大型工业场景（如国网巡检、安防监控），标准做法是将 **网络网关（Gateway）** 与 **侧端算法服务（AI Workers）** 彻底剥离。
网关只做连接管线、协议解析和流量分发；算法服务作为独立进程甚至独立容器运行，独自把控 GPU 显存和算力。

架构分为三层：
**[ 客户端/前端 ]** -> HTTP(图片上传) / TCP(协议流)
       ↓
**[ I/O 网关层 (本项目 gw-server) ]** -> Epoll Proactor, 内存池, 路由中心
       ↓ （跨进程通信：gRPC 或 ZeroMQ）
**[ 推理微服务集 (AI Workers) ]** -> (YOLO微服务、多曝光融合微服务、语义分割微服务)

---

## 2. 核心模块技术设计

### 2.1 网关底层：Epoll 模拟 Proactor 改造
改动代价中等，无需大动干戈切换 `io_uring`，但在现有的 Reactor（读写就绪后被动处理）基础上进行升级：
* **结构划分**：
  将当前的单 `EventLoop` 拆分为 **1 个 Main Acceptor Loop + N 个 Sub IO Loop**。
* **业务面无感知读写 (Proactor 语义)**：
  IO 线程在 `epoll_wait` 触发 `EPOLLIN` 时，**主动**从网络底层把数据读出到“预分配的内存池”中，拼装成完整的 HTTP Body 或 TCP Packet，然后把带有完整数据指针的任务丢进 Worker Queue。
* **收益**：业务 Worker 线程（包括 HTTP 请求处理线程）绝对不会卡在 `read()/recv()` 上，彻底告别现在的粘包处理与阻塞等待争抢。

### 2.2 大型算力调度：分离式推理微服务架构
既然您目前只有算法（未封装成服务），我们建议**不需在网关库内手写复杂的 CUDA 调度**，而是为这些算法提供一套 **“标准化微服务外壳”**。

工业级场景的多卡多任务分发策略设计如下：
1. **通信框架（网关与微服务之间）**：
   * **推荐 ZeroMQ (ZMQ) 或 gRPC**。同在一台物理机时，ZMQ 配合 IPC (甚至共享内存 Shared Memory) 传输图片极快，基本零拷贝。
2. **算法服务封装 (AI Worker Template)**：
   * 我们提供一个微服务模板（Python或C++均可）。您只需要把您的 YOLO、分割、多曝光融合算法填入这个模板的 `infer()` 函数中。
   * 每个算法编译成一个独立的进程（可执行文件或 Python 脚本）。
3. **GPU 卡显式绑定与多进程架构**：
   * 通过设置环境变量 `CUDA_VISIBLE_DEVICES` 或 `cudaSetDevice()` 来启动不同的微服务进程。
   * **例如分配策略**：
     - 进程 A（运行 YOLO 识别），绑定 GPU 0。
     - 进程 B（运行 语义分割），绑定 GPU 1。
     - 进程 C（运行 多曝光），绑定 GPU 0, 1（按需）。
4. **动态批处理 (Dynamic Batching) 下沉**：
   * 让网关尽可能轻。网关收到请求后，直接通过 ZMQ 打包发送给对应的算法微服务。
   * **在算法微服务内部实现批处理**：微服务的 ZMQ 接收端建立一个小队列（如 5ms 窗口），收到网关发来的 4 张零散图片后，在微服务内将它们拼接成 Batch=4 的 Tensor `(4, 3, H, W)` 统一送进推理引擎。

### 2.3 HTTP 路由与轻量级接入
* 保持当前的 `httplib.h` 作为 HTTP 前端。它基于阻塞线程池，但在前述架构下，它的 Handler 不再负责复杂的阻塞解析或推理。
* 它的工作变成了：接收 HTTP Post (文件内容) -> 写入内存池 -> 打包分发到对应算法的 ZMQ Router -> 挂起等待 ZMQ Reply -> 将结果返回给前端 HTTP 200 OK。这样单个 HTTP 线程占用的时间大幅缩短。

---

## 3. 我们可以分几个阶段来开工 (实施路径)：

* **Phase 1: 网络基建与模型服务通信解耦 (网关重构)**
  - 改写 `EventLoop` 支持 SubLoops，封装 Proactor 接口。
  - 在网关 (gw-server) 内引入轻量级 IPC/跨进程通信客户端（如集入 cppzmq 或基于原生 TCP 域套接字通信类），以便向后端微服务发送数据。

* **Phase 2: 搭建“算法微服务”空壳**
  - 为您建立一个脱离于 `gw-server` 的独立工程/目录（例如 `ai-services`），里面提供一个接收 ZMQ/TCP 数据的微服务框架。
  - 收到数据后，将数据转换为 OpenCV Mat 或 Tensor 内存，留出 `process_image()` 接口给您填入 YOLO / 分割算法。

* **Phase 3: 上下行链路与动态批处理打通**
  - 完善微服务内的 Dynamic Batching 队列，将多个前端请求拼批。
  - 打通 HTTP ->网关->IPC(ZMQ)->微服务->GPU多卡调度->回应网关->HTTP 的全链路闭环，并进行不同模型挂载到不同显存的并发测试。


## 4. 真实 GPU 推理服务的演进与接入 (当前状态与未来对接)

当前在 Phase 2 的试桩阶段（`mock_worker.py`），为了快速跑通全链路，暂时使用了非常轻量级的基于 HTTP 的本地端口（50051）转发，并且计算逻辑为纯 Mock。
**目前尚未启用 ZeroMQ (ZMQ) 或共享内存。**
接下来当您的**真实算法**就绪、且要应对**大批量多路并发、超高吞吐**的业务时，我们需要进行真正的微服务底座演进。具体实施方案如下：

### 4.1 ZMQ + 共享内存 (Shared Memory) 的工业级重构方案

如果在 C++ WebServer 节点与 GPU Python 算法节点之间，通过原生 TCP (或者 HTTP Socket) 大规模传递 `1080P/4K` 高清图像的二进制 Blob 数组，序列化和内核空间拷贝的开销极大。为了压榨性能，我们必须改为**零拷贝机制**：

1. **共享内存分配（Shared Memory Block）**
   - **网关层 (C++) 操作**：`httpserver` 在接收完前端的表单图像后，使用 `shmget` / `mmap` 或者 POSIX `shm_open` 创建一块共享内存，将图像数据直接 `memcpy` 进去。
   - 这块内存可支持多块轮转（建立一个小的 Ring Buffer 池）。

2. **ZMQ 消息信令总线 (IPC 协议)**
   - **摒弃 HTTP 转发**：引入 ZeroMQ (`cppzmq` 绑定)。将网关变成一个 ZMQ Router，将 AI Workers 变成 ZMQ Dealer。
   - **传输信令内容**：网关再也不发送庞大的图像内容（避开 base64/二进制序列化负担）。网关仅仅通过 ZMQ 跨进程发送一条极小的 JSON 或二进制指令给 Python Worker，例如：
     ```json
     {
         "task_id": 9527,
         "model": "yolo",
         "shm_key": "shm_image_block_1",
         "offset": 0,
         "size": 4194304
     }
     ```
   - Python 端的 Worker (如使用 `vipc` 或 `multiprocessing.shared_memory`) 收到这条微小指令后，用相同的 `shm_key` 映射出那块共享内存，直接使用 `cv2.imdecode()` 甚至 `cupy`/`torch` 张量原址解析数据，送给显卡。

### 4.2 真实 GPU 模型后端 (AI Worker) 的实现蓝图

为了替换掉现在的 `mock_worker`，您的独立 AI 服务开发应该遵循以下模式：

1. **框架骨架**
   采用高速 Python 引擎（如有类似 Triton 的并行框架，或基于 FastAPI+Uvicorn / ZMQ Router 裸写）。
2. **多卡进程隔离 (Multi-Process)**
   针对您提到的多卡运行多个任务，在项目内创建不同的启动脚本如 `worker_yolo.py`, `worker_seg.py`。
   在 Supervisor 脚本拉起时控制显存：
   ```bash
   CUDA_VISIBLE_DEVICES=0 python worker_yolo.py --zmq_port 50051 &
   CUDA_VISIBLE_DEVICES=1 python worker_seg.py --zmq_port 50052 &
   ```
3. **真实 GPU Batch 队列 (Dynamic Batching)**
   在 Python Worker 收数据的循环内，不要来一张就推一次 TensorRT（如果 QPS 非常高的话），而需要有一个缓冲器：
   ```python
   # 伪代码：在真实卡节点下增加请求组包
   batch_reqs = []
   while True:
       # 从 ZMQ 读取消息，最长阻塞 10毫秒
       msg = zmq_socket.recv(flags=zmq.NOBLOCK, timeout=10)
       if msg: 
           batch_reqs.append(msg)
       # 触发推流的条件：超时没等到满，或者已经攒够了 16 张图 (Batch=16)
       if len(batch_reqs) == 16 or timeout_reached:
           tensors = [load_from_shm(req.shm_key) for req_in batch_reqs]
           # 拼贴 Tensor: GPU [N, C, H, W] 并推入真实模型网络
           batch_tensor = torch.stack(tensors).to('cuda')
           results = yolo_model(batch_tensor)  
           # 组包回 ZMQ 并回发信号给 C++ 网关
           zmq_socket.send_multipart(results_mapped_to_reqs)
   ```
4. **模型预热与常驻显存**
   AI Worker 必须在启动的头两秒，载入所有的 `.engine` 或 `.pt` 权重文件，并将网络结构推送到 Device 端（甚至过一张空 Batch 预热）。这样客户端任何时候下发图像指令时，GPU 皆属于热机秒响应状态。

---

## 5. 当前后端推理服务实现状态（截至 2026-03-28）

### 5.1 已完成

1. **HTTP 推理入口已接通**
   - 网关新增 `POST /api/infer` 接口，接收 `multipart/form-data`（字段：`image1`、`image2`、`model`）。
2. **网关到微服务通道已从 HTTP 转为 ZMQ**
   - C++ 网关使用 `cppzmq` 发送 Multipart：
     - 帧1：JSON 元信息（模型类型、是否有第二张图等）
     - 帧2：图片1二进制字节
     - 帧3：图片2二进制字节（可选，用于HDR融合）
   - Python 微服务使用 `zmq.REP` 接收并回包。
3. **OpenCV 基础处理打通**
   - 微服务可完成：图片解码、模拟画框/分割效果、编码回 JPEG。
4. **前后端闭环验证通过**
   - 已实测链路：前端发起 -> 网关 -> ZMQ 微服务 -> 返回 JSON（含 `image_b64`）-> 前端展示结果图。
5. **基础构建依赖已就绪**
   - `server/CMakeLists.txt` 已接入 `libzmq` 链接。

### 5.2 本轮更新完成内容（2026-03-28）

#### 5.2.1 Zookeeper 编译修复
- **问题**：Zookeeper 3.9.0 将同步 API (`zoo_create`、`zoo_get` 等) 放在 `#ifdef THREADED` 条件内
- **修复**：在 `CMakeLists.txt` 中添加 `target_compile_definitions(server_core PRIVATE THREADED)`

#### 5.2.2 前端多图输入支持
- **修改文件**：`server/frontend/index.html`、`server/frontend/js/app.js`
- **功能**：
  - 添加第二图片上传控件（仅 HDR 融合时显示）
  - 支持模型选择动态显示/隐藏第二图片上传
  - 支持三种模型：`yolo`（1张图）、`segmentation`（1张图）、`hdr_fusion`（2张图）

#### 5.2.3 后端多图接收支持
- **修改文件**：`server/src/http_server.cpp`
- **功能**：
  - 支持 `image1` 和 `image2` 两个图片字段
  - 从 `req.form.get_field("model")` 获取模型类型
  - ZMQ 发送三帧: metadata + image1 + image2
  - 超时时间延长至 30s（适应 GPU 推理）

#### 5.2.4 GPU 推理 Worker
- **新增文件**：`ai-services/hdr_gpu_worker.py`
- **功能**：
  - 使用 CUDA GPU 加速（LFM_V1 + CRM_V1 模型）
  - GPU 预热机制
  - 支持 HDR 多曝光融合（2张图输入）
  - ZMQ REP 模式监听 50055 端口

#### 5.2.5 全链路测试验证
```
HDR Fusion: Status: success | Model: hdr_fusion_gpu | Time: ~500ms (GPU)
YOLO:       Status: success | Model: yolo | Time: ~4ms
Segmentation: Status: success | Model: segmentation | Time: ~8ms
```

### 5.3 待办（下一阶段）

1. **YOLO 真实模型接入**
   - 当前 YOLO 使用 OpenCV Mock 处理，需接入真实检测模型。
2. **语义分割真实模型接入**
   - 当前使用 OpenCV Mock 处理，需接入真实分割模型。
3. **多卡调度落地**
   - 尚未落地 `CUDA_VISIBLE_DEVICES` 级别的多进程 worker 编排。
4. **动态批处理**
   - 当前是单请求即时处理，未实现批窗口聚合。
5. **共享内存零拷贝**
   - 当前网关到微服务仍发送图片字节，未改造为 `shm_open/mmap + ZMQ 信令`。
6. **生产级可观测性**
   - 缺少任务级 trace、超时分层统计、失败重试与熔断策略。

---

## 6. 当前版本数据流与工作流梳理

### 6.1 数据流（当前实现）

1. **上传图片（资源管理）**
   - 路径：前端 -> `POST /upload` -> 网关磁盘落盘。
   - 存储位置：`server/resource/uploads/`。
   - 用途：归档与主页面图片浏览。

2. **推理请求（推理弹窗）**
   - 路径：前端选择本地图片 -> `POST /api/infer`（multipart，通常不先落盘）。
   - **多图支持**：
     - YOLO/语义分割：上传1张图片（`image1`）
     - HDR融合：上传2张图片（`image1` + `image2`）
   - 网关收到后：
     - 从请求体取图片二进制和模型参数；
     - 通过 **ZMQ Multipart** 发给 Python 微服务（不是 HTTP 转发）。

3. **推理结果返回**
   - 微服务处理后返回 JSON：`status/model_used/inference_time_ms/detections/image_b64`。
   - 网关将该 JSON 原样回给前端。
   - 前端把 `image_b64` 渲染到”推理后结果”框。

### 6.2 文件落盘与传输方式说明

1. **哪些文件会落盘**
   - 通过 `POST /upload` 的图片：会落盘到 `server/resource/uploads/`。
2. **哪些数据不落盘**
   - `POST /api/infer` 的弹窗推理图片：当前实现走内存字节流，不强制落盘。
3. **传输协议现状**
   - 浏览器 -> 网关：**HTTP multipart**（字段：`image1`、`image2`、`model`）。
   - 网关 -> 推理微服务：**ZMQ Multipart（二进制）**。
   - 微服务 -> 网关 -> 浏览器：**HTTP JSON**（含 Base64 结果图）。

### 6.3 工作流程（当前实现）

1. 用户在前端推理弹窗选择模型类型（YOLO/语义分割/HDR融合）
2. 根据模型类型上传1张或2张图片
3. 浏览器调用 `POST /api/infer`。
4. C++ 网关解析请求后，通过 ZMQ 将元信息+图片字节发给 Python worker。
5. Python worker 用 GPU 完成 HDR 融合处理（或 OpenCV Mock 处理 YOLO/分割），编码并回传 JSON。
6. 网关透传 JSON 给前端，前端显示推理后图片与日志。

### 6.4 支持的模型类型

| 模型 | 输入图片数 | 后端实现 | 备注 |
|------|-----------|---------|------|
| `yolo` | 1 | OpenCV Mock | 待接入真实检测模型 |
| `segmentation` | 1 | OpenCV Mock | 待接入真实分割模型 |
| `hdr_fusion` | 2 | GPU (LFM_V1 + CRM_V1) | 已完成真实模型推理 |

> 结论：当前推理链路是 **HTTP -> 网关 -> ZMQ -> 微服务(GPU) -> 网关 -> HTTP**；
> `upload` 路径负责持久化图片，`infer` 路径负责在线推理回显。

---

## 7. 系统架构与启动指南

### 7.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              客户端浏览器                                     │
│                         http://localhost:8080                               │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ HTTP POST /api/infer
                                    │ (multipart: image1, image2, model)
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        C++ Gateway (httpserver)                             │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │  http_server.cpp                                                      │  │
│  │   - HTTP 服务器 (端口 8080)                                           │  │
│  │   - ZMQ 客户端 (连接到 tcp://127.0.0.1:50055)                         │  │
│  │   - 接收前端请求 → 转发给 Python Worker → 返回结果给前端               │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  启动命令: ./bin/httpserver -p 52487 -w 8080                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ ZMQ Multipart
                                    │ Frame 1: JSON metadata
                                    │ Frame 2: image1 bytes
                                    │ Frame 3: image2 bytes (optional)
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Python Worker (HDR GPU Worker)                          │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │  hdr_gpu_worker.py                                                    │  │
│  │   - ZMQ REP 服务 (端口 50055)                                         │  │
│  │   - CUDA GPU 推理 (LFM_V1 + CRM_V1)                                   │  │
│  │   - 接收图片 → GPU 推理 → 返回 JSON (含 image_b64)                    │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  启动命令: python hdr_gpu_worker.py                                        │
│  文件位置: ai-services/hdr_gpu_worker.py                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ CUDA GPU
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           NVIDIA GPU (RTX 4060 Ti)                          │
│  - LFM_V1 模型 (亮度融合)                                                   │
│  - CRM_V1 模型 (色彩恢复)                                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 启动顺序与流程

**必须先启动的两个服务（按顺序）：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           步骤 1: 启动 Python GPU Worker                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  # 终端 1                                                                    │
│  cd /home/jym/cpp/gw-server/ai-services                                    │
│  source ~/venv/hdr-venv/bin/activate                                       │
│  python hdr_gpu_worker.py                                                  │
│                                                                             │
│  预期输出:                                                                  │
│  GPU available: NVIDIA GeForce RTX 4060 Ti                                  │
│  HDR models loaded successfully!                                             │
│  HDR GPU Worker listening on port 50055...                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           步骤 2: 启动 C++ Gateway                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  # 终端 2                                                                    │
│  cd /home/jym/cpp/gw-server/server                                          │
│  ./bin/httpserver -p 52487 -w 8080                                          │
│                                                                             │
│  预期输出:                                                                  │
│  Starting Gateway Server...                                                  │
│    TCP Port:   52487                                                        │
│    HTTP Port:  8080                                                         │
│  服务器已启动，监听端口 52487                                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           步骤 3: 打开浏览器使用                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 浏览器访问: http://localhost:8080                                       │
│  2. 点击 "云端模型推理" 按钮                                                │
│  3. 选择模型类型 (YOLO / 语义分割 / 多曝光融合 HDR)                         │
│  4. 上传图片                                                                │
│  5. 点击 "开始推理"                                                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.3 一键启动脚本

项目根目录已提供启动脚本：

```bash
/home/jym/cpp/gw-server/start_inference_services.sh
```

### 7.4 Worker 文件说明

| 文件 | 路径 | 说明 |
|------|------|------|
| GPU Worker | `ai-services/hdr_gpu_worker.py` | 使用 NVIDIA GPU 推理（当前默认） |
| CPU Worker | `ai-services/hdr_worker.py` | 使用 CPU 推理（速度较慢） |
| Mock Worker | `ai-services/zmq_worker.py` | OpenCV Mock 处理（用于开发调试） |
| Gateway | `server/src/http_server.cpp` | ZMQ 地址硬编码在第 355 行 |

### 7.5 切换 CPU/GPU Worker

**启动 GPU Worker（默认）：**
```bash
cd /home/jym/cpp/gw-server/ai-services
source ~/venv/hdr-venv/bin/activate
python hdr_gpu_worker.py
```

**启动 CPU Worker：**
```bash
cd /home/jym/cpp/gw-server/ai-services
source ~/venv/hdr-venv/bin/activate
python hdr_worker.py
```

### 7.6 端口配置

| 服务 | 端口 | 配置文件 |
|------|------|----------|
| C++ Gateway HTTP | 8080 | 启动参数 `-w 8080` |
| C++ Gateway TCP | 52487 | 启动参数 `-p 52487` |
| Python Worker ZMQ | 50055 | `http_server.cpp` 第 355 行硬编码 |

### 7.7 数据流时序图

```
用户浏览器                    C++ Gateway                Python GPU Worker               NVIDIA GPU
    │                            │                            │                            │
    │  1. 选择图片并推理          │                            │                            │
    │───────────────────────────>│                            │                            │
    │                            │  2. 解析 multipart         │                            │
    │                            │  3. ZMQ Frame 1 (JSON)    │                            │
    │                            │───────────────────────────>│                            │
    │                            │  4. ZMQ Frame 2 (image1)  │                            │
    │                            │───────────────────────────>│                            │
    │                            │  5. ZMQ Frame 3 (image2)  │                            │
    │                            │───────────────────────────>│                            │
    │                            │                            │  6. 解码图片              │
    │                            │                            │  7. GPU 推理调用           │
    │                            │                            │───────────────────────────>│
    │                            │                            │  8. LFM_V1 + CRM_V1       │
    │                            │                            │<───────────────────────────│
    │                            │  9. JSON (image_b64)      │                            │
    │<───────────────────────────│<───────────────────────────│                            │
    │  10. 显示结果图片          │                            │                            │
```

### 7.8 快速测试命令

```bash
# 测试 HDR 融合
curl -X POST http://127.0.0.1:8080/api/infer \
  -F "image1=@test_low.jpg" \
  -F "image2=@test_high.jpg" \
  -F "model=hdr_fusion"

# 测试 YOLO
curl -X POST http://127.0.0.1:8080/api/infer \
  -F "image1=@test.jpg" \
  -F "model=yolo"

# 测试语义分割
curl -X POST http://127.0.0.1:8080/api/infer \
  -F "image1=@test.jpg" \
  -F "model=segmentation"
```

