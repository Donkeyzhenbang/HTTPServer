# Agent 重构与 Bug 修复记录 (2026-02-22)

## 1. 初始状态分析
- 当用户尝试使用 Agent 聊天时，前端报 `Failed to execute 'json' on 'Response': Unexpected end of JSON input`。
- 这是由于前端 JS 默认向当前 Web 服务器 (C++ HttpServer, 端口 8080) 发送 `/api/query` 请求。
- C++ 服务器没有 `/api/query` 的路由处理，因此返回了空或非 JSON 的错误响应。
- 真实的 Python Agent 服务运行在 8000 端口，前端未能正确连接。

## 2. 架构调整
- **前端 (Agent Widget)**: 
  - 路径: `server/frontend/js/agent_widget.js`
  - 职责: 负责 UI 渲染、Markdown 解析、与后端通信。
- **C++ 后端 (HTTPServer)**: 
  - 端口: 8080
  - 职责: 依然作为静态文件服务器即主业务 API 服务。
- **Python 后端 (Agent Service)**: 
  - 路径: `server/agent/main.py`
  - 端口: 8000
  - 职责: 处理自然语言查询、模拟代码修改、编译、测试等指令。

## 3. Bug 修复过程

### 3.1 端口与路由修复
- **问题**: `agent_widget.js` 默认 `serverUrl: ''`，导致请求发往 8080。
- **修复**: 修改 `server/frontend/js/agent_widget.js`。
  - 增加端口检测逻辑: `const isDev = window.location.port === '8080';`
  - 如果检测到是 8080 环境，自动将 `serverUrl` 设置为 `http://hostname:8000`。
  - 这样既保留了生产环境(通过 Nginx 统一反代)的兼容性，也修复了开发环境直连的问题。

### 3.2 错误处理增强
- **问题**: `await res.json()` 直接抛出异常，缺乏调试信息，且直接吞掉了具体的 HTTP 错误码。
- **修复**: 
  - 增加 `res.ok` 检查，如果不通过则抛出带状态码和响应体前50字符的 Error。
  - 增加 JSON 解析的 `try-catch` 包裹，如果解析失败（如收到 HTML 错误页），会抛出 `Invalid JSON response` 并展示响应片段。
  - 允许在 UI 上直接看到具体的错误原因（如 404 Not Found 或 500 Internal Server Error）。

## 4. 功能重构与增强

### 4.1 前端 UI 升级 (Widget v2.0)
- **视觉**: 
  - 采用深色主题 (Dark Mode) 适配 Agent 的科技感。
  - 增加毛玻璃 (Backdrop Filter) 效果。
  - 优化了聊天气泡样式，区分 User/Agent/System 消息。
- **动效**: 
  - 增加“正在思考” (Typing Indicator) 的打字机动画。
  - 聊天窗口开关增加缩放位移过渡。
  - 底部增加快速指令 Chips (如 `modify agent js`, `compile frontend`)。

### 4.2 Markdown 渲染引擎
- **内建解析**: 在 `agent_widget.js` 中实现了一个轻量级的 Markdown 解析器 `parseText`。
- **支持语法**:
  - 代码块: ```javascript ... ``` -> `<pre><code>...</code></pre>`
  - 行内代码: ` `code` ` -> `<code>code</code>`
  - 标题: `### Title` -> `<h3>Title</h3>`
  - 列表: `- item` -> `<li>item</li>`
  - 加粗: `**bold**` -> `<strong>bold</strong>`

### 4.3 后端模拟能力 (Python Agent)
- **文件**: `server/agent/agent_core.py`
- **新增工具**:
  - `_handle_code_modification`: 模拟代码修改过程，返回 Diff 格式的 Markdown。
  - `_handle_compilation`: 模拟构建过程，输出构建日志。
  - `_handle_testing`: 模拟测试过程，输出测试通过率表格。
- **交互逻辑**: 
  - 识别 `modify`, `compile`, `test`, `build` 等关键词，触发对应的模拟流程。
  - 增强了 `process_command` 的自然语言处理能力（虽然目前主要基于关键词匹配）。

## 5. 验证结果
- **启动**: `python server/agent/main.py` 正常监听 8000。
- **连接**: 前端从 8080 访问网页，点击 Widget，发送 `help`。
- **响应**: 
  - 请求成功发往 `http://localhost:8000/api/query`。
  - 返回 JSON: `{"answer": "...", "success": true}`。
  - 前端正确解析并渲染 Markdown 内容。
- **异常测试**: 
  - 如果 python 服务未启动，前端提示 `System Error: Connection to agent failed`。
  - 不再出现 `Unexpected end of JSON input` 的无头错误。

## 6. 使用指南
1. 进入虚拟环境: `source agent-venv/bin/activate`
2. 启动 Agent: `python server/agent/main.py`
3. 启动 Web 服务: `./bin/httpserver` (或通过 VS Code Live Server)
4. 访问页面: `http://localhost:8080`
5. 点击右下角 Agent 图标进行交互。

## 7. Bug修复 (2025-02-23)

### 7.1 UnicodeEncodeError 修复
- **现象**: 在某些环境下（如 Docker 或特定 Locale），Agent 返回包含非 ASCII 字符（如中文或特殊符号）的 JSON 字符串时，UVicorn/Starlette 抛出 `UnicodeEncodeError: 'utf-8' codec can't encode character '\udcca' ...: surrogates not allowed`。
- **原因**: Python 在处理某些系统命令输出或文件读取时，可能会引入 Surrogate 字符（如 `\udcca`），这些字符在 JSON 序列化后保留在字符串中，但在 HTTP 响应编码为 UTF-8 字节流时是非法的。
- **修复**: 
  - 在 `server/agent/agent_core.py` 中增加 `_safe_json_dumps` 辅助函数。
  - 使用 `.encode('utf-8', 'replace').decode('utf-8')` 对 JSON 字符串进行清洗，将非法的 Surrogate 字符替换为 `?`，防止服务崩溃。
  - 应用于所有涉及 `json.dumps` 的返回点 (`status`, `logs`, `diag`, `check_server`)。

### 7.2 Nginx 生产环境路由修复
- **现象**: 在 80 端口（生产环境）访问时，Agent 聊天窗口无法连接 (`404 Not Found`)。这是因为前端默认请求 `/api/query`，而 Nginx 只有 `/agent/` 的反向代理配置，`/api/` 路径请求落入 C++ 后端（8080）导致 404。
- **修复**: 
  - 修改 `server/frontend/js/agent_widget.js`。
  - 调整 `serverUrl` 逻辑：
    - **开发环境 (Port 8080)**: 保持 `http://hostname:8000` 直连。
    - **生产环境 (Port 80/其他)**: 设置 `serverUrl` 为 `/agent`。
  - 最终请求路径变更为 `/agent/api/query`，Nginx 成功代理至 `http://127.0.0.1:8000/api/query`。

# 问题修复与验证报告
## 一、修复内容
### 1. 修复 Python 后端 UnicodeEncodeError 崩溃问题
- **故障原因**：Agent 返回的 JSON 数据包含特殊字符（如受损 Unicode、系统 Surrogate 字符 `\udcca`），FastAPI/Uvicorn 序列化响应体时，UTF-8 编码失败触发崩溃。
- **修复方案**：修改 `agent_core.py`，新增 `_safe_json_dumps` 辅助函数，在 JSON 序列化后主动采用 `replace` 策略处理非法编码字符，确保返回前端的数据为合法 UTF-8 字符串。
- **覆盖范围**：设备状态、日志查询、诊断结果、服务器健康检查等所有返回 JSON 格式的接口。

### 2. 修复 Nginx 80 端口访问前端 404 问题
- **故障原因**：Nginx 配置仅代理 `/agent/` 路径，但前端非开发模式下默认请求 `/api/query`（无 `/agent` 前缀），导致请求转发至 C++ 后端 8080 端口并返回 404。
- **修复方案**：修改 `agent_widget.js`，优化请求路径逻辑：
  - 开发环境（8080 端口）：保持直连 `http://localhost:8000`，不修改路径；
  - 生产环境（80 端口）：自动将 `serverUrl` 设置为 `/agent`，使最终请求路径变为 `/agent/api/query`，匹配 Nginx 代理规则。

### 3. 文档更新
- 所有修复细节、故障现象分析及验证方法已完整记录在 `agent.md` 中；
- 新增「7. Bug修复 (2025-02-23)」章节，统一归档本次修复内容。

## 二、验证方法
### 前置步骤
重启相关服务，确保配置和代码修改生效。

### 1. 验证 8080 端口（开发模式）
1. 访问地址：`http://localhost:8080`；
2. 打开 Agent 组件，输入指令：`status Device-A001`；
3. **预期结果**：正常返回 JSON 格式的设备状态数据，无 500 服务器错误。

### 2. 验证 80 端口（生产模式）
1. 访问地址：`http://localhost`（或服务器实际 IP）；
2. 打开 Agent 组件，输入指令：`help`；
3. **预期结果**：请求 `http://localhost/agent/api/query` 返回 200 OK，聊天窗口正常显示指令回复，无 404 错误。