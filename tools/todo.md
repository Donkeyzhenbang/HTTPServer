# gw-server 改造 TODO（网络架构 / 性能 / 复用）

## 0. 现状速览（基于代码阅读）

- 服务器 TCP：主线程 `accept` 后每连接一个线程，线程内先阻塞等心跳，再启动“读线程”把 socket 字节推进队列，然后主线程从队列解析帧并顺序处理：[main.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/main.cpp#L69-L103)、[HandleClient](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L593-L644)、[StartReadThread](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L169-L223)、[recv_and_resolve](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L236-L345)
- 协议处理：用 `Handlers[]` 做 (frameType, packetType) 查表回调：[Handlers](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L79-L94)、[sFrameResolver](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L353-L370)
- 图片接收：`RecvFileHandler` 内部再“自旋式”反复 `recv_and_resolve()` 直到收齐 F0/F1，这会把其它协议帧读走但不分发（心跳/设备注册等会被吞掉）：[RecvFileHandler](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L436-L524)
- 前端加载图片：每次设置 `img.src` 都拼接 `?_=` 或 `?t=` + `Date.now()`，导致浏览器缓存永远失效： [index.html](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/bin/web/index.html#L1137-L1173)、[renderMainImage](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/bin/web/index.html#L1257-L1276)

---

## 1. 后续网络架构怎么设计（保证“随时接收不同协议”，不丢心跳）

### 1.1 当前代码的核心问题（需要先修）

- **顺序流吞包**：图片/模型等“大事务”在某个 handler 内部“独占读取”，把其它帧消费掉但不交给 `sFrameResolver()`，导致心跳/控制指令被舍弃（表现为“只有顺序流能跑”）。
- **阻塞式握手**：连接建立后先 `waitForHeartBeat(connfd)` 直接 `read()`，此时读线程/解析器尚未统一接管输入流，后续扩展协议会越来越难维护：[HandleClient](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L600-L608)、[waitForHeartBeat](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/heartbeat.cpp#L82-L186)
- **CPU/时延浪费**：读线程按字节 push 到 `std::queue`，解析线程反复 `size()+pop()`，并用 `nanosleep` 忙等；大量小包时 CPU 会被锁/自旋吃掉：[MyQueue](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/connection.h#L14-L48)、[StartReadThread](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L169-L215)、[recv_and_resolve](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L257-L279)
- **可靠性 bug**：读线程读到 `len==0` 会 `exit(EXIT_FAILURE)`，会把整个 server 进程直接杀掉（任何客户端断开都可能触发）：[StartReadThread](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L199-L205)

### 1.2 目标形态（建议）

每个连接都遵循一条不可破坏的“输入处理主线”：

1. **持续读取 socket（只在一个地方读）**
2. **字节流解析成“完整帧”**
3. **按 (frameType, packetType) 分发给 handler**
4. handler **只更新状态 / 产出响应 / 投递任务**，绝不在 handler 内部再“独占读取”

这样无论设备什么时候发心跳、告警、小控制包，都能被解析并投递；图片/模型等大事务通过“会话状态机”逐帧推进，而不是靠顺序 while-loop 抢占输入。

### 1.3 两阶段改造路线（从易到难）

#### 阶段 A：保持“每连接一个线程”，先把协议从“顺序流”改为“事件流”

- 在 `ConnectionContext` 里维护 **会话状态**，典型是：
  - `registered_device_id`（设备注册状态）
  - `ImageRxSession`（按 channel 维护：总包数、已收包 bitmap、缓冲区、当前写入位置、超时）
  - `ModelUpgradeSession`（同理）
- 把 `RecvFileHandler` 改成“启动/更新会话”，不要在里面再循环 `recv_and_resolve()` 等包：
  - `0x05 0xEF`（B351）=> 创建 `ImageRxSession`
  - `0x05 0xF0`（分包）=> 写入 session（并校验 subpacketNo）
  - `0x05 0xF1`（结束）=> 完成校验、落盘、回复 B38
  - 其它帧（心跳、控制）照常走分发
- 把“等心跳”改成“心跳 handler”：连接建立后直接进入统一读/解析/分发循环；收到心跳帧时再注册 device_id（不要额外 `read()` 破坏流的一致性）。

#### 阶段 B：性能可扩展（大量小包 + 多连接）

选一个方向即可：

- **Reactor（推荐 Linux 生产）**：`epoll` + 非阻塞 socket
  - 1 个 IO 线程负责 `read -> parse -> dispatch(lightweight)`
  - N 个 worker 线程处理重任务（图片落盘、模型落盘、MD5、压缩/缩略图等）
- **asio（开发体验更好）**：统一异步 IO，天然适合“多协议交织”

这两种都能把线程数量从“连接数 * 2”降下来，也避免自旋队列。

---

## 2. 服务器端优化（阿里云 2核2G，图片加载卡）

### 2.1 代码里最直接的瓶颈：前端禁用了缓存

当前前端每次加载图片都附带 `Date.now()`，等价于告诉浏览器“每次都重新下载整张图”，即使 Nginx 配了强缓存也完全无效：

- 缩略图： [index.html](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/bin/web/index.html#L1154-L1173)
- 主图： [renderMainImage](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/bin/web/index.html#L1257-L1271)

这会在阿里云公网环境下把“加载图片”变成纯带宽/延迟问题，表现就是卡。

### 2.2 后端侧的性能隐患（会拖慢 HTTP/静态资源）

- TCP 收包的 per-byte 队列 + busy-wait 会把 2 核 CPU 吃满，进而让 HTTP 响应排队变慢：[MyQueue](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/inc/connection.h#L14-L48)、[recv_and_resolve](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L257-L279)
- 读线程 `exit(EXIT_FAILURE)` 会导致进程频繁退出重启，用户侧看起来像“服务不稳定/加载很慢/偶发 404”：[StartReadThread](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/recvfile.cpp#L199-L205)

### 2.3 可落地优化手段（按收益排序）

1. **前端缓存策略（最高收益）**
   - 历史图片：去掉 `?_=` / `?t=`，让浏览器按文件名缓存（配合 Nginx `expires` 最有效）
   - 只对“刚拍完、同名覆盖”的场景加版本号；更推荐后端返回 `mtime`，前端用 `?v=<mtime>`，而不是 `Date.now()`
2. **静态资源由 Nginx 直出**
   - 确认 `/uploads/` 的 `alias` 路径与实际落盘目录一致（不一致会回落到应用层转发，性能会差）：[nginx 配置](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/config/nginx/transmission-platform#L37-L43)
3. **TCP 接收链路降 CPU**
   - 用 SPSC ring buffer 或 “批量 append + 条件变量”替换按字节 push/pop（避免自旋锁 + size()）
   - 去掉 busy-wait（100ms sleep + 轮询），改为条件变量唤醒或 `poll/epoll`
4. **缩略图策略**
   - 列表显示缩略图（比如 320px 宽），点击再加载原图；公网带宽占用会立刻下降
5. **进程拆分（稳定性/隔离）**
   - 把 TCP 收包服务与 HTTP 服务拆成两个进程：收包进程只负责落盘；HTTP 进程/Nginx 只负责对外提供静态文件

---

## 3. 客户端与服务器重复代码复用（建议抽库）

结论：建议抽一个“协议与基础设施”库，否则后续每加一个协议要双端复制/修改，风险和维护成本会指数增长。

### 3.1 明显可复用点（已有重复）

- CRC16、帧完整性校验、frameType/packetType 提取：[server/utils.*](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/utils.cpp#L83-L156)、[client/utils.h](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/client/inc/utils.h#L1-L22)
- B351/B37/B342 等协议结构体与初始化/发送逻辑在多处重复：[client/sendfile.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/client/src/sendfile.cpp#L45-L236)、[server/modelupgrade.cpp](file:///c:/Users/%E5%AD%A3%E7%87%95%E9%93%AD/OneDrive/Desktop/gw-server/server/src/modelupgrade.cpp#L19-L257)
- 解析/等待某协议的“阻塞 read”模式在 client/server 都存在（后续改为统一 parser + dispatcher 时也应一起抽象）

### 3.2 抽库建议形态

- 形式：`gw_common`（静态库优先），对外暴露“无状态工具 + 协议编解码 + 帧解析器”
- 目录：顶层新增 `common/`（含 `inc/`、`src/`、`CMakeLists.txt`）
- 内容建议：
  - `FrameParser`：字节流 -> 完整帧（支持多帧粘包/半包）
  - `ProtocolCodec`：Bxxx 的 pack/unpack/校验
  - `Net`：socket 选项（keepalive、nodelay、缓冲区），跨平台适配
  - `Time`：时间戳/统计（避免全局变量）

---

## 4. 改造 TODO 列表（建议按顺序做）

- [ ] 修复读线程 `len==0` 触发进程退出的问题
- [ ] 把图片接收从“顺序 while-loop”改为“会话状态机”
- [ ] 用统一的“读/解析/分发”替换 `waitForHeartBeat` 阻塞握手
- [ ] 替换 per-byte 自旋队列为 ring buffer/条件变量队列
- [ ] 修复 connection_manager 的并发访问一致性（统一加锁策略）
- [ ] 移除前端对图片的 `Date.now()` cache-bust（保留必要刷新场景）
- [ ] 校验 Nginx `/uploads/` alias 与实际落盘目录一致
- [ ] 设计并落地 `gw_common` 复用库（client/server 共同链接）

