/**
 * 设备智能助手浮窗SDK v2.0
 * 支持Markdown渲染、代码高亮、流式思考模拟
 */
class DeviceAgentWidget {
    constructor(options = {}) {
        // 自动检测端口: 如果是8080/80端口，默认尝试8000端口
        const isDev = window.location.port === '8080';
        this.options = {
            // Dev (8080): Direct to PyAgent(8000). Prod (80): Proxy via Nginx /agent/ -> 8000
            serverUrl: isDev ? 'http://' + window.location.hostname + ':8000' : '/agent',
            position: 'bottom-right', 
            primaryColor: '#06b3b0',
            ...options
        };
        
        this.isOpen = false;
        this.chatHistory = [];
        this.init();
    }
    
    init() {
        this.injectStyles();
        this.createButton();
        this.createChatWindow();
        this.bindEvents();
    }
    
    injectStyles() {
        const style = document.createElement('style');
        style.textContent = `
            @keyframes fadeIn { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
            @keyframes pulse { 0% { box-shadow: 0 0 0 0 rgba(6, 179, 176, 0.4); } 70% { box-shadow: 0 0 0 10px rgba(6, 179, 176, 0); } 100% { box-shadow: 0 0 0 0 rgba(6, 179, 176, 0); } }
            @keyframes dot-typing {
                0% { box-shadow: 9984px 0 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px 0 0 0 #06b3b0; }
                16.667% { box-shadow: 9984px -10px 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px 0 0 0 #06b3b0; }
                33.333% { box-shadow: 9984px 0 0 0 #06b3b0, 9999px -10px 0 0 #06b3b0, 10014px 0 0 0 #06b3b0; }
                50% { box-shadow: 9984px 0 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px -10px 0 0 #06b3b0; }
                66.667% { box-shadow: 9984px -10px 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px 0 0 0 #06b3b0; }
                83.333% { box-shadow: 9984px 0 0 0 #06b3b0, 9999px -10px 0 0 #06b3b0, 10014px 0 0 0 #06b3b0; }
                100% { box-shadow: 9984px 0 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px -10px 0 0 #06b3b0; }
            }

            #agent-chat-window {
                font-family: 'Inter', system-ui, sans-serif;
                background: linear-gradient(180deg, #1a2624 0%, #0d1a19 100%);
                border: 1px solid rgba(6,179,176,0.3);
                border-radius: 16px;
                box-shadow: 0 20px 60px rgba(0,0,0,0.6), 0 0 0 1px rgba(255,255,255,0.05);
                display: none;
                flex-direction: column;
                z-index: 10001;
                backdrop-filter: blur(20px);
                transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
                opacity: 0;
                transform: translateY(20px) scale(0.95);
            }
            #agent-chat-window.open {
                display: flex;
                opacity: 1;
                transform: translateY(0) scale(1);
            }

            .agent-header {
                padding: 18px 20px;
                background: rgba(6,179,176,0.08);
                border-bottom: 1px solid rgba(6,179,176,0.1);
                display: flex;
                justify-content: space-between;
                align-items: center;
                border-radius: 16px 16px 0 0;
            }
            .agent-title strong {
                color: #06b3b0;
                font-size: 16px;
                letter-spacing: 0.5px;
            }
            .agent-title small {
                display: block; opacity: 0.7; font-size: 11px; color: #bfeee9; margin-top: 4px;
            }
            .agent-close {
                background: none; border: none; color: #bfeee9; font-size: 24px; cursor: pointer; opacity: 0.6; transition: opacity 0.2s;
            }
            .agent-close:hover { opacity: 1; }

            #agent-resize-handle {
                width: 20px;
                height: 20px;
                /* Top-Left handle for bottom-right anchored window */
                background: linear-gradient(135deg, rgba(6,179,176,0.5) 50%, transparent 50%);
                position: absolute;
                top: 0;
                left: 0;
                cursor: nwse-resize; 
                border-top-left-radius: 16px;
                z-index: 10002;
                transition: background 0.2s;
            }
            #agent-resize-handle:hover {
                background: linear-gradient(135deg, rgba(6,179,176,1) 50%, transparent 50%);
            }

            .agent-messages {
                flex: 1;
                padding: 20px;
                /* Padding bottom to avoid overlap with resize handle if needed, though handle is small corner */
                overflow-y: auto;
                display: flex;
                flex-direction: column;
                gap: 16px;
                scroll-behavior: smooth;
            }
            .agent-messages::-webkit-scrollbar { width: 6px; }
            .agent-messages::-webkit-scrollbar-thumb { background: rgba(6,179,176,0.2); border-radius: 3px; }

            .message {
                max-width: 88%;
                padding: 12px 16px;
                border-radius: 12px;
                line-height: 1.6;
                font-size: 14px;
                position: relative;
                animation: fadeIn 0.3s ease-out;
                word-wrap: break-word;
            }
            .message.user {
                align-self: flex-end;
                background: linear-gradient(135deg, #06b3b0 0%, #048f8c 100%);
                color: #fff;
                border: none;
                border-bottom-right-radius: 4px;
                box-shadow: 0 4px 12px rgba(6,179,176,0.2);
            }
            .message.agent {
                align-self: flex-start;
                background: rgba(255,255,255,0.05);
                border: 1px solid rgba(255,255,255,0.08);
                color: #e0f2f1;
                border-bottom-left-radius: 4px;
            }
            
            /* Simple Markdown Styles */
            .message.agent code {
                background: rgba(0,0,0,0.3);
                padding: 2px 5px;
                border-radius: 4px;
                font-family: 'Consolas', monospace;
                color: #80cbc4;
                font-size: 0.9em;
            }
            .message.agent pre {
                background: #0a1010;
                padding: 12px;
                border-radius: 8px;
                overflow-x: auto;
                border: 1px solid rgba(6,179,176,0.2);
                margin: 10px 0;
            }
            .message.agent pre code {
                background: transparent;
                padding: 0;
                color: #a7ffeb;
                white-space: pre;
                display: block;
            }
            .message.agent ul { padding-left: 20px; margin: 8px 0; }
            .message.agent li { margin-bottom: 4px; }
            .message.agent p { margin: 8px 0; }
            .message.agent p:first-child { margin-top: 0; }
            .message.agent p:last-child { margin-bottom: 0; }
            .message.agent h1, .message.agent h2, .message.agent h3 {
                margin: 12px 0 8px 0;
                color: #06b3b0;
                font-size: 1.1em;
                font-weight: 600;
            }
            .message.agent blockquote {
                border-left: 3px solid #06b3b0;
                margin: 8px 0;
                padding-left: 10px;
                color: #80cbc4;
                background: rgba(6,179,176,0.05);
                padding: 8px 10px;
                border-radius: 0 4px 4px 0;
            }

            .agent-input-area {
                padding: 16px;
                border-top: 1px solid rgba(6,179,176,0.1);
                display: flex; gap: 10px;
                background: rgba(0,0,0,0.2);
                border-radius: 0 0 16px 16px;
            }
            .agent-input {
                flex: 1;
                padding: 12px 16px;
                border-radius: 20px;
                border: 1px solid rgba(255,255,255,0.1);
                background: rgba(255,255,255,0.05);
                color: #fff;
                outline: none;
                font-size: 14px;
                transition: all 0.2s;
            }
            .agent-input:focus {
                border-color: #06b3b0;
                background: rgba(255,255,255,0.08);
                box-shadow: 0 0 0 2px rgba(6,179,176,0.2);
            }
            
            .agent-send {
                width: 44px; height: 44px;
                border-radius: 50%;
                border: none;
                background: linear-gradient(135deg, #06b3b0 0%, #048f8c 100%);
                color: white;
                cursor: pointer;
                display: flex; align-items: center; justify-content: center;
                transition: transform 0.2s, box-shadow 0.2s;
                font-weight: bold;
            }
            .agent-send:hover { transform: scale(1.05); box-shadow: 0 4px 12px rgba(6,179,176,0.4); }

            .typing-dot {
                position: absolute; left: -9999px; width: 6px; height: 6px; border-radius: 5px;
                background-color: #06b3b0; color: #06b3b0;
                box-shadow: 9984px 0 0 0 #06b3b0, 9999px 0 0 0 #06b3b0, 10014px 0 0 0 #06b3b0;
                animation: dot-typing 1.5s infinite linear;
            }
            .typing-msg { padding: 16px 24px; min-width: 60px; display: flex; align-items: center; justify-content: center; }

            .agent-examples {
                padding: 10px 20px;
                display: flex; flex-wrap: wrap; gap: 8px;
                border-top: 1px solid rgba(255,255,255,0.05);
            }
            .chip {
                font-size: 11px; padding: 6px 12px;
                background: rgba(6,179,176,0.1); color: #80cbc4;
                border: 1px solid rgba(6,179,176,0.2);
                border-radius: 16px; cursor: pointer; transition: all 0.2s;
            }
            .chip:hover {
                background: rgba(6,179,176,0.2); color: #fff; transform: translateY(-1px);
            }
        `;
        document.head.appendChild(style);
    }
    
    createButton() {
        this.button = document.createElement('div');
        this.button.id = 'agent-float-btn';
        this.button.innerHTML = `
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" style="filter: drop-shadow(0 2px 4px rgba(0,0,0,0.2));">
                <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>
            </svg>
        `;
        this.button.style.cssText = `
            position: fixed; ${this.getPositionStyle()};
            width: 60px; height: 60px;
            background: linear-gradient(135deg, ${this.options.primaryColor}, #048f8c);
            border-radius: 50%;
            cursor: pointer;
            display: flex; align-items: center; justify-content: center;
            box-shadow: 0 4px 20px rgba(6, 179, 176, 0.4);
            z-index: 10000;
            transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            animation: pulse 3s infinite;
        `;
        this.button.onclick = () => this.toggleChat();
        document.body.appendChild(this.button);
    }
    
    createChatWindow() {
        this.chatWindow = document.createElement('div');
        this.chatWindow.id = 'agent-chat-window';
        this.chatWindow.innerHTML = `
            <div class="agent-header">
                <div class="agent-title">
                    <strong>⚡ 设备智能助手</strong>
                    <small>Grid Monitor · Code Agent</small>
                </div>
                <button class="agent-close" title="最小化">_</button>
            </div>
            <div class="agent-messages">
                <div class="message agent">
                    <p>你好！我是设备智能助手 (Agent v2.0)。</p>
                    <p>除了<strong>设备监控</strong>，我还支持<strong>代码修改</strong>与<strong>编译部署</strong>链路。</p>
                    <p>试试输入：</p>
                    <ul>
                        <li><code>modify agent js</code> 优化前端体验</li>
                        <li><code>compile frontend</code> 编译资源</li>
                        <li><code>run tests</code> 执行测试套件</li>
                    </ul>
                </div>
            </div>
            <div class="agent-examples">
                <span class="chip">设备状态</span>
                <span class="chip">modify js</span>
                <span class="chip">compile</span>
                <span class="chip">diagnostics</span>
            </div>
            <div class="agent-input-area">
                <input type="text" class="agent-input" placeholder="输入指令..." />
                <button class="agent-send">➤</button>
            </div>
            <div id="agent-resize-handle"></div>
        `;
        this.chatWindow.style.cssText = `
            position: fixed; ${this.getPositionStyle('window')};
            width: 380px; height: 560px;
            /* Allow resize */
            min-width: 300px; min-height: 400px;
            max-width: 90vw; max-height: 90vh;
        `;
        document.body.appendChild(this.chatWindow);
    }
    
    bindEvents() {
        this.chatWindow.querySelector('.agent-close').addEventListener('click', () => this.toggleChat());
        this.chatWindow.querySelector('.agent-send').addEventListener('click', () => this.sendMessage());
        
        const input = this.chatWindow.querySelector('.agent-input');
        input.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') this.sendMessage();
        });
        
        this.chatWindow.querySelectorAll('.chip').forEach(chip => {
            chip.addEventListener('click', (e) => {
                const txt = e.target.textContent;
                input.value = txt;
                if (!txt.includes('modify')) { // modify commands might need confirmation or parameters
                     this.sendMessage();
                } else {
                    input.focus();
                }
            });
        });

        this.initResize();
    }
    
    initResize() {
        const handle = this.chatWindow.querySelector('#agent-resize-handle');
        const win = this.chatWindow;
        let isResizing = false;
        let startX, startY, startW, startH;

        handle.addEventListener('mousedown', (e) => {
            isResizing = true;
            startX = e.clientX;
            startY = e.clientY;
            startW = parseInt(document.defaultView.getComputedStyle(win).width, 10);
            startH = parseInt(document.defaultView.getComputedStyle(win).height, 10);
            
            // Prevent text selection during resize
            document.documentElement.style.userSelect = 'none';
            // Disable transitions during resize
            win.style.transition = 'none';
        });

        document.documentElement.addEventListener('mousemove', (e) => {
            if (!isResizing) return;
            // Handle is at Top-Left. Moving mouse left/up increases size.
            // DeltaX = e.clientX - startX.
            // If DeltaX is negative (moved left), width should increase.
            // NewW = startW - DeltaX
            
            const newW = startW - (e.clientX - startX);
            const newH = startH - (e.clientY - startY);
            
            win.style.width = `${newW}px`;
            win.style.height = `${newH}px`;
        });
        
        document.documentElement.addEventListener('mouseup', () => {
            if(isResizing) {
                isResizing = false;
                document.documentElement.style.userSelect = '';
                // Restore transition
                win.style.transition = 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)';
            }
        });
    }
    
    toggleChat() {
        this.isOpen = !this.isOpen;
        const win = this.chatWindow;
        
        if (this.isOpen) {
            win.classList.add('open');
            this.button.style.transform = 'scale(0) rotate(180deg)';
            this.button.style.opacity = '0';
            this.button.style.pointerEvents = 'none';
            setTimeout(() => win.querySelector('.agent-input').focus(), 300);
        } else {
            win.classList.remove('open');
            this.button.style.transform = 'scale(1) rotate(0deg)';
            this.button.style.opacity = '1';
            this.button.style.pointerEvents = 'auto';
        }
    }
    
    // Minimal Markdown Parser
    parseText(text) {
        if (!text) return '';
        let html = text
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            // Code Blocks
            .replace(/```(\w+)?\n([\s\S]*?)\n```/g, '<pre><code>$2</code></pre>')
            // Inline Code
            .replace(/`([^`]+)`/g, '<code>$1</code>')
            // Bold
            .replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
            // Newlines
            .replace(/\n/g, '<br>');
        return html;
    }
    
    async sendMessage() {
        const input = this.chatWindow.querySelector('.agent-input');
        const message = input.value.trim();
        if (!message) return;
        
        this.addMessage(message, 'user');
        input.value = '';
        input.disabled = true;
        
        const loader = document.createElement('div');
        loader.className = 'message agent typing-msg';
        loader.innerHTML = '<div class="typing-indicator"><div class="typing-dot"></div></div>';
        this.chatWindow.querySelector('.agent-messages').appendChild(loader);
        this.scrollToBottom();

        try {
            // 支持开发环境端口回退
            let fetchUrl = `${this.options.serverUrl}/api/query`;
            
            // 发送请求
            const res = await fetch(fetchUrl, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ question: message })
            });

            // 增强的错误处理
            if (!res.ok) {
                const text = await res.text();
                throw new Error(`HTTP Error ${res.status}: ${text.slice(0, 50)}...`);
            }
            
            // 安全JSON解析
            let data;
            try {
                data = await res.json();
            } catch (jsonErr) {
                 const text = await res.text();
                 throw new Error(`Invalid JSON response: "${text.slice(0,50)}..."`);
            }
            
            loader.remove();
            
            if (data.answer) {
                this.addMessage(data.answer, 'agent');
            } else {
                 this.addMessage('**No Answer**: Received empty response from agent.', 'agent');
            }
            
        } catch (e) {
            loader.remove();
            this.addMessage(`**System Error**: Connection to agent failed.\n\`${e.message}\``, 'agent');
        } finally {
            input.disabled = false;
            input.focus();
        }
    }
    
    addMessage(text, type) {
        const div = document.createElement('div');
        div.className = `message ${type}`;
        if (type === 'user') {
            div.textContent = text;
        } else {
            div.innerHTML = this.parseText(text);
        }
        this.chatWindow.querySelector('.agent-messages').appendChild(div);
        this.scrollToBottom();
    }
    
    scrollToBottom() {
        const el = this.chatWindow.querySelector('.agent-messages');
        el.scrollTop = el.scrollHeight;
    }
    
    getPositionStyle(type) {
        const m = '24px';
        return `bottom: ${m}; right: ${m};`;
    }
}

document.addEventListener('DOMContentLoaded', () => {
    window.DeviceAgent = new DeviceAgentWidget();
});
