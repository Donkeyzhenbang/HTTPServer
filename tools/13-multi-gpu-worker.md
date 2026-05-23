# 多卡调度与动态批处理方案设计

## 1. 背景与目标

### 1.1 当前状态

当前已实现：
- ✅ C++ Gateway 到 Python Worker 的 ZMQ 通信
- ✅ GPU 模型推理（LFM_V1 + CRM_V1）
- ✅ HDR 多曝光融合（2张图输入）
- ✅ 单请求即时处理模式（batch_size = 1）

当前限制：
- ❌ 无动态批处理 - 每帧独立推理，GPU 利用率低
- ❌ 无多卡调度 - 单 GPU 瓶颈，吞吐受限
- ❌ 无流式处理 - 请求串行处理，有 gap 空档
- ❌ 显存碎片 - 长期运行显存泄漏/碎片

### 1.2 目标（按优先级）

1. **动态批处理** - 聚合多请求到同一 batch，提升 GPU 利用率
2. **多卡动态负载均衡** - 多 GPU Worker，ZMQ Router 自动路由
3. **流式处理 overlap** - 请求流水线化，减小开销
4. **显存健康维护** - 定时清理显存碎片

---

## 2. 技术方案详解

### 2.1 HDR 引擎说明

**重要**：HDR 的 LFM_V1 和 CRM_V1 必须**串行执行**，不可并行。

原因：
- LFM_V1 输出是 CRM_V1 输入的一部分
- 存在数据依赖，无法解耦

```python
# HDR 必须串行（不可 CUDA Stream 并行）
Output_Yf = mDEM(mDEM_Input)      # Step 1: 必须先完成
Output_CbCr = mCEM(mCEM_Input)   # Step 2: 依赖 Step1 输出
Output_HDR = combine(Output_Yf, Output_CbCr)
```

**CUDA Stream 的正确用途**：用于**批量图片的流式处理**，通过 overlap 减小批次间开销。

---

### 2.2 动态批处理 (Dynamic Batching)

#### 2.2.1 原理

设置一个时间窗口（如 10ms），收集窗口内的请求，合并为一个 batch：

```
时间窗口 10ms:
Request A ──────────→ [img1]
Request B ───────→ [img2]
Request C ─→ [img3]
              ↓ 窗口结束
         [Batch of 3] ──→ GPU ──→ 3个结果
```

#### 2.2.2 核心实现

```python
# === dynamic_batcher.py ===

import queue
import threading
import time
import uuid
import torch
import zmq

class BatchRequest:
    """批处理请求单元"""
    def __init__(self, request_id, img1_bytes, img2_bytes, model_type, event):
        self.request_id = request_id
        self.img1_bytes = img1_bytes
        self.img2_bytes = img2_bytes
        self.model_type = model_type
        self.event = event
        self.result = None
        self.exception = None

class DynamicBatcher:
    """动态批处理器

    工作流程:
    1. 接收请求，加入等待队列
    2. 达到 max_batch_size 或超时时，触发 batch
    3. 批量执行推理
    4. 分发结果给各请求
    """

    def __init__(self, engine, max_batch_size=8, max_wait_ms=10):
        self.engine = engine
        self.max_batch_size = max_batch_size
        self.max_wait_ms = max_wait_ms  # ms

        self.pending_queue = queue.Queue()  # 等待处理的请求
        self.active_batches = {}  # request_id -> BatchRequest
        self.lock = threading.Lock()

        # 启动批处理循环线程
        self.running = True
        self.batch_thread = threading.Thread(target=self._batch_loop, daemon=True)
        self.batch_thread.start()

    def add_request(self, img1_bytes, img2_bytes, model_type):
        """添加推理请求，返回 request_id 和结果事件"""
        request_id = str(uuid.uuid4())
        event = threading.Event()

        request = BatchRequest(request_id, img1_bytes, img2_bytes, model_type, event)

        with self.lock:
            self.active_batches[request_id] = request

        self.pending_queue.put(request)

        return request_id, event

    def _batch_loop(self):
        """批处理主循环"""
        while self.running:
            batch = self._collect_batch()

            if not batch:
                continue

            # 执行批处理
            self._execute_batch(batch)

    def _collect_batch(self):
        """收集一批请求直到达到 max_batch_size 或超时"""
        batch = []
        start_time = time.time()

        while len(batch) < self.max_batch_size:
            elapsed_ms = (time.time() - start_time) * 1000

            # 超时且已有请求，触发处理
            if elapsed_ms >= self.max_wait_ms and batch:
                break

            # 非阻塞获取请求
            try:
                request = self.pending_queue.get(timeout=0.001)
                batch.append(request)
            except queue.Empty:
                if batch:
                    break
                continue

        return batch

    def _execute_batch(self, batch):
        """执行批量推理"""
        try:
            # 准备 batch 数据
            img1_tensors = []
            img2_tensors = []

            for req in batch:
                img1_t = self.engine.preprocess(req.img1_bytes)
                img2_t = self.engine.preprocess(req.img2_bytes)
                img1_tensors.append(img1_t)
                img2_tensors.append(img2_t)

            # Stack 成 [N, C, H, W]
            batch_img1 = torch.cat(img1_tensors, dim=0)  # 会出问题，需要 unsqueeze
            batch_img2 = torch.cat(img2_tensors, dim=0)

            # 正确方式：
            batch_img1 = torch.stack(img1_tensors, dim=0)  # [N, C, H, W]
            batch_img2 = torch.stack(img2_tensors, dim=0)  # [N, C, H, W]

            # GPU 推理（串行执行 LFM + CRM）
            with torch.no_grad():
                outputs = self.engine.infer(batch_img1, batch_img2)  # [N, C, H, W]

            # 分发结果
            for i, req in enumerate(batch):
                req.result = outputs[i]
                req.event.set()

        except Exception as e:
            # 异常处理
            for req in batch:
                req.exception = e
                req.event.set()

        finally:
            # 清理 active_batches
            with self.lock:
                for req in batch:
                    self.active_batches.pop(req.request_id, None)

    def get_result(self, request_id, timeout=30):
        """获取推理结果"""
        with self.lock:
            req = self.active_batches.get(request_id)

        if req is None:
            return None

        req.event.wait(timeout=timeout)

        if req.exception:
            raise req.exception

        return req.result

    def shutdown(self):
        """关闭批处理器"""
        self.running = False
        self.batch_thread.join(timeout=5)
```

#### 2.2.3 HDR 引擎（串行，不可并行）

```python
# === hdr_engine.py ===

import torch
import cv2
import numpy as np
from torchvision import transforms

class HDREngine:
    """HDR 推理引擎

    注意：LFM_V1 和 CRM_V1 必须串行执行，不可 CUDA Stream 并行
    CUDA Stream 用于流式处理 overlap，不是用于并行两个模型
    """

    def __init__(self, checkpoint_dir, gpu_id=0):
        self.device = torch.device(f'cuda:{gpu_id}')
        torch.cuda.set_device(gpu_id)

        # 加载模型
        self.mDEM = self._load_model('LFM_V1', checkpoint_dir)
        self.mCRM = self._load_model('CRM_V1', checkpoint_dir)

        self.mDEM.to(self.device)
        self.mCRM.to(self.device)
        self.mDEM.eval()
        self.mCRM.eval()

        # 预热
        self._warmup()

    def _load_model(self, model_name, checkpoint_dir):
        from kernel.models import LFM_V1, CRM_V1
        model_class = LFM_V1 if model_name == 'LFM_V1' else CRM_V1
        model = model_class()

        ckpt_path = f"{checkpoint_dir}/{model_name}_Supervised_smy.pt"
        ckpt = torch.load(ckpt_path, map_location='cpu')
        model.load_state_dict(ckpt['state_dict'])
        return model

    def _warmup(self):
        """GPU 预热"""
        dummy1 = torch.randn(1, 3, 512, 512).to(self.device)
        dummy2 = torch.randn(1, 3, 512, 512).to(self.device)
        with torch.no_grad():
            _ = self.infer(dummy1, dummy2)
        torch.cuda.synchronize()

    def preprocess(self, img_bytes):
        """图片预处理"""
        img = cv2.imdecode(np.frombuffer(img_bytes, np.uint8), cv2.IMREAD_COLOR)
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        tensor = transforms.ToTensor()(img).unsqueeze(0)  # [1, C, H, W]
        return tensor.to(self.device)

    def infer(self, img1_batch, img2_batch):
        """HDR 推理（串行执行 LFM -> CRM）

        Args:
            img1_batch: [N, 3, H, W]
            img2_batch: [N, 3, H, W]

        Returns:
            output: [N, 3, H, W] HDR 融合结果
        """
        with torch.no_grad():
            # Step 1: LFM_V1 亮度融合
            img1_y = self._extract_y(img1_batch)
            img2_y = self._extract_y(img2_batch)
            lfm_input = torch.cat([img1_y, img2_y], dim=1)  # [N, 2, H, W]
            output_y = self.mDEM(lfm_input)  # [N, 1, H, W]

            # 归一化
            output_y = (output_y - output_y.min()) / (output_y.max() - output_y.min() + 1e-8)

            # Step 2: CRM_V1 色彩恢复（依赖 Step1 输出，必须串行）
            img1_ycbcr = self._rgb_to_ycbcr(img1_batch)
            img2_ycbcr = self._rgb_to_ycbcr(img2_batch)
            crm_input = torch.cat([img1_ycbcr, img2_ycbcr, output_y], dim=1)  # [N, 7, H, W]
            output_cbcr = self.mCRM(crm_input)  # [N, 2, H, W]

            # Step 3: 合并 Y + CbCr -> RGB
            output = self._ycbcr_to_rgb(torch.cat([output_y, output_cbcr], dim=1))

        return output

    def _extract_y(self, img_rgb):
        """提取 Y 通道"""
        # Y = 0.299R + 0.587G + 0.114B
        return (0.299 * img_rgb[:, 0:1, :, :] +
                0.587 * img_rgb[:, 1:2, :, :] +
                0.114 * img_rgb[:, 2:3, :, :])

    def _rgb_to_ycbcr(self, img_rgb):
        """RGB -> YCbCr"""
        Y = self._extract_y(img_rgb)
        Cb = 0.5 - 0.168736 * img_rgb[:, 0, :, :] - 0.331264 * img_rgb[:, 1, :, :] + 0.5 * img_rgb[:, 2, :, :]
        Cr = 0.5 + 0.5 * img_rgb[:, 0, :, :] - 0.418688 * img_rgb[:, 1, :, :] - 0.081312 * img_rgb[:, 2, :, :]
        return torch.stack([Y.squeeze(1), Cb, Cr], dim=1)

    def _ycbcr_to_rgb(self, img_ycbcr):
        """YCbCr -> RGB"""
        Y = img_ycbcr[:, 0, :, :]
        Cb = img_ycbcr[:, 1, :, :]
        Cr = img_ycbcr[:, 2, :, :]

        R = Y + 1.402 * (Cr - 0.5)
        G = Y - 0.344136 * (Cb - 0.5) - 0.714136 * (Cr - 0.5)
        B = Y + 1.772 * (Cb - 0.5)

        return torch.stack([R, G, B], dim=1)
```

---

### 2.3 多卡动态负载均衡

#### 2.3.1 架构设计

```
┌────────────────────────────────────────────────────────────────────────┐
│                     C++ Gateway (ZMQ Router)                            │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  请求路由：根据负载选择空闲 Worker                                 │  │
│  │  - 跟踪每个 Worker 的当前负载（pending 请求数）                  │  │
│  │  - 选择负载最低的 Worker 处理新请求                               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
                    │                    │                    │
        ┌───────────┴───────────┐       │       ┌───────────┴───────────┐
        ▼                       ▼       ▼       ▼                       ▼
┌───────────────┐       ┌───────────────┐ ┌───────────────┐       ┌───────────────┐
│  Worker 0     │       │  Worker 1     │ │  Worker 2     │       │  Worker 3     │
│  GPU 0        │       │  GPU 1        │ │  GPU 0        │       │  GPU 1        │
│  Port: 50055  │       │  Port: 50056  │ │  Port: 50057  │       │  Port: 50058  │
│  Load: 3      │       │  Load: 7      │ │  Load: 2      │       │  Load: 5      │
└───────────────┘       └───────────────┘ └───────────────┘       └───────────────┘
        │                       │               │                       │
        └───────────────────────┴───────────────┴───────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
            ┌───────────────┐               ┌───────────────┐
            │   NVIDIA      │               │   NVIDIA      │
            │   GPU 0       │               │   GPU 1       │
            │  (Worker 0,2) │               │  (Worker 1,3) │
            └───────────────┘               └───────────────┘
```

#### 2.3.2 Gateway 动态负载均衡实现

```cpp
// === http_server.cpp ===

#include <map>
#include <queue>
#include <mutex>
#include <atomic>

// Worker 信息
struct WorkerInfo {
    std::string endpoint;        // "tcp://127.0.0.1:50055"
    std::atomic<int> pending;    // 当前 pending 请求数
    int gpu_id;                 // GPU ID
    std::string model_type;     // 支持的模型类型
    bool available;              // 是否可用
};

class LoadBalancer {
    std::map<std::string, WorkerInfo> workers_;
    std::mutex mutex_;

public:
    void register_worker(const std::string& name, const std::string& endpoint,
                        int gpu_id, const std::string& model_type) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_[name] = {endpoint, 0, gpu_id, model_type, true};
    }

    void unregister_worker(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.erase(name);
    }

    // 选择负载最低的 Worker
    std::string select_worker(const std::string& model_type) {
        std::lock_guard<std::mutex> lock(mutex_);

        WorkerInfo* best = nullptr;
        int min_load = INT_MAX;

        for (auto& [name, info] : workers_) {
            if (!info.available) continue;
            if (!model_type.empty() && info.model_type != model_type &&
                info.model_type != "all") continue;

            if (info.pending < min_load) {
                min_load = info.pending;
                best = &info;
            }
        }

        return best ? best->endpoint : "";
    }

    void increment_load(const std::string& endpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, info] : workers_) {
            if (info.endpoint == endpoint) {
                info.pending++;
                break;
            }
        }
    }

    void decrement_load(const std::string& endpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, info] : workers_) {
            if (info.endpoint == endpoint) {
                info.pending--;
                if (info.pending < 0) info.pending = 0;
                break;
            }
        }
    }
};

// 使用方式
class ZMQInferenceClient {
    LoadBalancer balancer_;
    std::map<std::string, zmq::socket_t> sockets_;
    zmq::context_t ctx_{1};

public:
    void init() {
        // 注册 Workers
        balancer_.register_worker("gpu0_hdr", "tcp://127.0.0.1:50055", 0, "hdr_fusion");
        balancer_.register_worker("gpu1_hdr", "tcp://127.0.0.1:50056", 1, "hdr_fusion");
        balancer_.register_worker("gpu0_yolo", "tcp://127.0.0.1:50057", 0, "yolo");
        balancer_.register_worker("gpu1_seg", "tcp://127.0.0.1:50058", 1, "segmentation");
    }

    std::string infer(const std::string& model_type,
                       const std::vector<char>& img1,
                       const std::vector<char>& img2) {
        // 选择负载最低的 Worker
        std::string endpoint = balancer_.select_worker(model_type);
        if (endpoint.empty()) {
            throw std::runtime_error("No available worker");
        }

        // 获取或创建 socket
        zmq::socket_t& sock = get_socket(endpoint);
        balancer_.increment_load(endpoint);

        try {
            // 发送请求
            send_multipart(sock, model_type, img1, img2);

            // 接收结果
            std::string result = recv_string(sock);

            balancer_.decrement_load(endpoint);
            return result;
        } catch (...) {
            balancer_.decrement_load(endpoint);
            throw;
        }
    }
};
```

#### 2.3.3 Python Worker（支持批处理）

```python
# === multi_gpu_worker.py ===

import zmq
import json
import base64
import cv2
import numpy as np
import torch
import threading
import time
import logging
from typing import Dict, List, Optional
from dynamic_batcher import DynamicBatcher
from hdr_engine import HDREngine

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class MultiGPUWorker:
    """多模型 Worker，支持动态批处理"""

    def __init__(self, port: int, gpu_id: int, model_type: str = "all",
                 checkpoint_dir: str = "/home/jym/python/hdr-net-start/checkpoints"):
        self.port = port
        self.gpu_id = gpu_id
        self.model_type = model_type  # "hdr_fusion", "yolo", "segmentation", "all"
        self.checkpoint_dir = checkpoint_dir

        # 初始化 HDR 引擎
        self.hdr_engine = None
        if model_type in ["hdr_fusion", "all"]:
            logger.info(f"Loading HDR engine on GPU {gpu_id}...")
            self.hdr_engine = HDREngine(checkpoint_dir, gpu_id=gpu_id)
            self.batcher = DynamicBatcher(self.hdr_engine, max_batch_size=8, max_wait_ms=10)
            logger.info(f"HDR engine ready on GPU {gpu_id}")

        # ZMQ
        self.ctx = zmq.Context()
        self.socket = None

        # 运行状态
        self.running = False

        # 注册到 Nanny 服务（可选，用于动态发现）
        self.nanny_endpoint = "tcp://127.0.0.1:5555"

    def start(self):
        """启动 Worker"""
        self.socket = self.ctx.socket(zmq.REP)
        self.socket.setsockopt(zmq.RCVTIMEO, 30000)  # 30s 超时
        self.socket.setsockopt(zmq.SNDTIMEO, 30000)
        self.socket.bind(f"tcp://*:{self.port}")

        self.running = True
        logger.info(f"Worker started on port {self.port}, GPU {self.gpu_id}")

        self._register_to_nanny()

        while self.running:
            try:
                self._process_request()
            except zmq.Again:
                continue
            except Exception as e:
                logger.error(f"Error: {e}")
                self.socket.send_json({"error": str(e)})

    def stop(self):
        """停止 Worker"""
        self.running = False
        if self.socket:
            self.socket.close()
        self.ctx.term()

    def _process_request(self):
        """处理单个请求"""
        message = self.socket.recv_multipart()
        if len(message) < 2:
            self.socket.send_json({"error": "Invalid message format"})
            return

        meta = json.loads(message[0].decode('utf-8'))
        img1_bytes = message[1]
        img2_bytes = message[2] if len(message) > 2 and len(message[2]) > 0 else img1_bytes

        model_type = meta.get("model", self.model_type)
        request_id, event = self.batcher.add_request(img1_bytes, img2_bytes, model_type)

        # 等待结果
        result = self.batcher.get_result(request_id, timeout=30)

        if result is None:
            self.socket.send_json({"error": "Inference timeout"})
            return

        # 编码返回
        output_bgr = self._tensor_to_image(result)
        _, buffer = cv2.imencode('.jpg', output_bgr, [cv2.IMWRITE_JPEG_QUALITY, 95])
        encoded = base64.b64encode(buffer).decode('utf-8')

        response = {
            "status": "success",
            "model_used": model_type,
            "inference_time_ms": 0,  # TODO: 从 batcher 获取
            "image_b64": f"data:image/jpeg;base64,{encoded}"
        }

        self.socket.send_json(response)

    def _register_to_nanny(self):
        """注册到 Nanny 服务用于动态发现"""
        # TODO: 实现服务注册
        pass

    @staticmethod
    def _tensor_to_image(tensor):
        """Tensor -> OpenCV Image"""
        img = tensor.squeeze(0).permute(1, 2, 0).cpu().numpy()
        img = (img * 255).clip(0, 255).astype(np.uint8)
        return cv2.cvtColor(img, cv2.COLOR_RGB2BGR)


# === 流式处理 overlap ===
# 动态批处理天然支持流式 overlap：
# - 线程1: 收集请求 -> 组 batch
# - 线程2: 执行推理 (与线程1可 overlap)
# - 线程3: 分发结果
#
# Timeline:
# Time:    0ms    10ms    20ms    30ms    40ms    50ms
# Thread1: [Collect] [Collect] [Collect]
# Thread2:          [ Infer ] [ Infer ] [ Infer ]
# Thread3:                   [Dispatch] [Dispatch]
#
# 通过 overlap，CPU 收集和 GPU 推理可并行，进一步提升吞吐
```

---

### 2.4 进程管理器 (Supervisord)

使用 supervisord 管理多 Worker 进程：

```ini
# === /etc/supervisord.d/hdr-workers.ini ===

[supervisorctl]
serverurl=http://127.0.0.1:9001

[program:gpu0_hdr]
command=python /home/jym/cpp/gw-server/ai-services/multi_gpu_worker.py --port 50055 --gpu-id 0 --model hdr_fusion
directory=/home/jym/cpp/gw-server/ai-services
user=jym
autostart=true
autorestart=true
stderr_logfile=/var/log/hdr-worker-gpu0.err.log
stdout_logfile=/var/log/hdr-worker-gpu0.out.log
environment=CUDA_VISIBLE_DEVICES="0"

[program:gpu1_hdr]
command=python /home/jym/cpp/gw-server/ai-services/multi_gpu_worker.py --port 50056 --gpu-id 1 --model hdr_fusion
directory=/home/jym/cpp/gw-server/ai-services
user=jym
autostart=true
autorestart=true
stderr_logfile=/var/log/hdr-worker-gpu1.err.log
stdout_logfile=/var/log/hdr-worker-gpu1.out.log
environment=CUDA_VISIBLE_DEVICES="1"

[program:gpu0_yolo]
command=python /home/jym/cpp/gw-server/ai-services/multi_gpu_worker.py --port 50057 --gpu-id 0 --model yolo
directory=/home/jym/cpp/gw-server/ai-services
user=jym
autostart=true
autorestart=true
stderr_logfile=/var/log/hdr-worker-yolo0.err.log
stdout_logfile=/var/log/hdr-worker-yolo0.out.log
environment=CUDA_VISIBLE_DEVICES="0"

[program:gpu1_seg]
command=python /home/jym/cpp/gw-server/ai-services/multi_gpu_worker.py --port 50058 --gpu-id 1 --model segmentation
directory=/home/jym/cpp/gw-server/ai-services
user=jym
autostart=true
autorestart=true
stderr_logfile=/var/log/hdr-worker-seg1.err.log
stdout_logfile=/var/log/hdr-worker-seg1.out.log
environment=CUDA_VISIBLE_DEVICES="1"
```

启动命令：
```bash
# 启动所有 Worker
supervisorctl start all

# 查看状态
supervisorctl status

# 查看日志
supervisorctl tail -f gpu0_hdr
```

---

### 2.5 GPU 显存健康管理

#### 2.5.1 问题分析

长期运行时可能产生显存碎片：
- PyTorch 内存分配器可能产生碎片
- 模型权重占用大块显存，中间张量碎片化
- 批处理大小变化导致碎片

#### 2.5.2 解决方案

**方案 1: 定时清理（推荐）**

```python
class HDREngine:
    def __init__(self, ...):
        # ...
        self.cleanup_interval = 100  # 每 100 次推理清理一次
        self.inference_count = 0
        self.last_cleanup_time = time.time()

    def infer(self, ...):
        # 推理
        result = self._inference_impl(img1, img2)

        self.inference_count += 1

        # 定时清理
        if self.inference_count >= self.cleanup_interval:
            self._cleanup_memory()

        return result

    def _cleanup_memory(self):
        """清理显存碎片"""
        import gc

        # 1. Python GC
        gc.collect()

        # 2. PyTorch 显存缓存清理
        if torch.cuda.is_available():
            # 清除 PyTorch 的 CUDA 缓存
            torch.cuda.empty_cache()

            # 可选：重置 CUDA 分配器（更彻底但有性能开销）
            # torch.cuda.reset_accumulated_memory_stats()
            # torch.cuda.reset_peak_memory_stats()

        # 3. 记录日志
        allocated = torch.cuda.memory_allocated() / 1024**2
        reserved = torch.cuda.memory_reserved() / 1024**2
        logger.info(f"[GPU {self.gpu_id}] Memory cleanup: "
                    f"allocated={allocated:.1f}MB, reserved={reserved:.1f}MB")

        self.inference_count = 0
```

**方案 2: 显存监控 + 预警**

```python
class GPUMemoryMonitor:
    """GPU 显存监控"""

    def __init__(self, threshold=0.85):  # 85% threshold
        self.threshold = threshold
        self.gpu_id = 0

    def get_memory_usage(self):
        """获取当前显存使用率"""
        if not torch.cuda.is_available():
            return 0.0

        allocated = torch.cuda.memory_allocated(self.gpu_id)
        total = torch.cuda.get_device_properties(self.gpu_id).total_memory
        return allocated / total

    def should_cleanup(self):
        """是否需要清理"""
        return self.get_memory_usage() > self.threshold

    def monitor_loop(self):
        """监控循环"""
        while True:
            if self.should_cleanup():
                logger.warning(f"[GPU {self.gpu_id}] Memory usage high, triggering cleanup")
                torch.cuda.empty_cache()
            time.sleep(60)  # 每分钟检查
```

**方案 3: 预分配显存池（最稳定）**

```python
class HDREngine:
    """使用预分配显存池，减少碎片"""

    def __init__(self, ...):
        # 预热时分配最大显存块
        self.warmup_batch_size = 8

        # 预分配输入张量（重复利用）
        self._input_pool = []
        self._output_pool = []

        for _ in range(self.warmup_batch_size):
            # 预分配 1MB 张量作为池
            t = torch.empty(1, 3, 1024, 1024, device=self.device)
            self._input_pool.append(t)
            self._output_pool.append(torch.empty(1, 3, 1024, 1024, device=self.device))

        self.pool_idx = 0

    def infer(self, img1_batch, img2_batch):
        # 复用预分配的张量（避免动态分配）
        # ...
```

**推荐组合策略**:

```python
class MemoryManager:
    """综合显存管理"""

    def __init__(self, engine):
        self.engine = engine
        self.cleanup_interval = 100  # 每100次推理清理
        self.inference_count = 0
        self.monitor_thread = None

    def start_monitoring(self):
        """启动监控线程"""
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()

    def _monitor_loop(self):
        """每分钟检查一次"""
        while True:
            time.sleep(60)
            usage = self._get_memory_usage()
            if usage > 0.85:
                logger.warning(f"High memory usage: {usage*100:.1f}%")
                self._cleanup()

    def _cleanup(self):
        """清理显存"""
        gc.collect()
        torch.cuda.empty_cache()

    def on_inference_complete(self):
        """每次推理完成调用"""
        self.inference_count += 1
        if self.inference_count >= self.cleanup_interval:
            self._cleanup()
            self.inference_count = 0
```

---

## 3. 文件结构

```
ai-services/
├── hdr_engine.py              # HDR 推理引擎（串行执行）
├── dynamic_batcher.py         # 动态批处理器
├── multi_gpu_worker.py        # 多 GPU Worker（主程序）
├── memory_manager.py          # 显存健康管理
├── model_registry.py          # 模型注册表（可选）
└── test_batcher.py            # 测试脚本

supervisord/
└── hdr-workers.ini           # Supervisor 配置

server/
└── src/
    └── http_server.cpp        # Gateway（动态负载均衡路由）
```

---

## 4. 实施计划

### Phase 1: 动态批处理
1. 实现 `hdr_engine.py` - HDR 引擎（串行，保持原样）
2. 实现 `dynamic_batcher.py` - 批处理器
3. 修改 `multi_gpu_worker.py` - 集成批处理
4. 压测验证吞吐提升

### Phase 2: 多卡调度
1. 修改 Gateway 动态路由
2. 配置 supervisord 多 Worker
3. 多卡负载均衡测试

### Phase 3: 显存健康
1. 实现 `memory_manager.py`
2. 集成定时清理
3. 长时间稳定性测试

---

## 5. 配置参数

```python
# === 动态批处理 ===
BATCH_MAX_SIZE = 8        # 最大 batch size
BATCH_TIMEOUT_MS = 10    # 等待超时 ms

# === 显存管理 ===
MEMORY_CLEANUP_INTERVAL = 100  # 每 N 次推理清理一次
MEMORY_WARNING_THRESHOLD = 0.85  # 85% 阈值告警

# === 多卡调度 ===
WORKER_PORTS = {
    'hdr_fusion': [50055, 50056],  # GPU0, GPU1
    'yolo': [50057, 50058],
    'segmentation': [50059, 50060],
}
LOAD_BALANCE_STRATEGY = 'least_load'  # 最低负载优先
```

---

## 6. 开放问题

**已确认**:
1. ✅ HDR 两个引擎串行执行
2. ✅ CUDA Stream 用于流式 overlap（不是并行两个模型）
3. ✅ 动态批处理 + 多卡负载均衡
4. ✅ 使用 supervisord 管理进程
5. ✅ 显存健康管理（定时清理 + 监控）

**无需确认，可直接实施**。

---

## 7. 参考资料

- [PyTorch CUDA Memory Management](https://pytorch.org/docs/stable/notes/cuda.html#memory-management)
- [Supervisord Documentation](http://supervisord.org/)
- [ZeroMQ Load Balancing](https://zguide.zeromq.org/docs/chapter3/#Load-Balancing-Pattern)
