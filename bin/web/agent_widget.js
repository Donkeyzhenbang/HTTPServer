/**
 * 设备智能助手浮窗SDK
 */
class DeviceAgentWidget {
    constructor(options = {}) {
        this.options = {
            serverUrl: 'http://' + window.location.host + '/agent',
            position: 'bottom-right', // 'bottom-right', 'bottom-left', 'top-right', 'top-left'
            primaryColor: '#06b3b0',
            ...options
        };
        
        this.isOpen = false;
        this.chatHistory = [];
        this.init();
    }
    
    init() {
        // 创建浮窗按钮
        this.createButton();
        // 创建聊天窗口
        this.createChatWindow();
        // 绑定事件
        this.bindEvents();
    }
    
    createButton() {
        this.button = document.createElement('div');
        this.button.id = 'agent-float-btn';
        this.button.innerHTML = `
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2">
                <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>
            </svg>
        `;
        this.button.style.cssText = `
            position: fixed;
            ${this.getPositionStyle()};
            width: 56px;
            height: 56px;
            background: ${this.options.primaryColor};
            border-radius: 50%;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            box-shadow: 0 4px 12px rgba(6, 179, 176, 0.3);
            z-index: 10000;
            transition: transform 0.2s, box-shadow 0.2s;
        `;
        document.body.appendChild(this.button);
    }
    
    createChatWindow() {
        this.chatWindow = document.createElement('div');
        this.chatWindow.id = 'agent-chat-window';
        this.chatWindow.innerHTML = `
            <div class="agent-header">
                <div class="agent-title">
                    <strong>设备智能助手</strong>
                    <small>询问设备状态、连接信息</small>
                </div>
                <button class="agent-close">&times;</button>
            </div>
            <div class="agent-messages"></div>
            <div class="agent-input-area">
                <input type="text" class="agent-input" placeholder="输入问题，如：当前有哪些设备？" />
                <button class="agent-send">发送</button>
            </div>
            <div class="agent-examples">
                试试问：
                <span class="example">设备列表</span>
                <span class="example">连接数统计</span>
                <span class="example">系统状态</span>
            </div>
        `;
        this.chatWindow.style.cssText = `
            position: fixed;
            ${this.getPositionStyle('window')};
            width: 320px;
            height: 460px;
            background: linear-gradient(180deg, rgba(3,28,29,0.95), rgba(2,18,20,0.98));
            border: 1px solid rgba(6,179,176,0.2);
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.6);
            z-index: 10001;
            display: none;
            flex-direction: column;
            backdrop-filter: blur(10px);
        `;
        
        // 添加内部样式
        const style = document.createElement('style');
        style.textContent = this.getChatWindowStyles();
        document.head.appendChild(style);
        
        document.body.appendChild(this.chatWindow);
    }
    
    getChatWindowStyles() {
        return `
            #agent-chat-window .agent-header {
                padding: 16px;
                border-bottom: 1px solid rgba(6,179,176,0.1);
                display: flex;
                justify-content: space-between;
                align-items: center;
            }
            #agent-chat-window .agent-title {
                color: #06b3b0;
            }
            #agent-chat-window .agent-title small {
                display: block;
                font-size: 12px;
                color: #7ab7b5;
                margin-top: 4px;
            }
            #agent-chat-window .agent-close {
                background: transparent;
                border: none;
                color: #bfeee9;
                font-size: 24px;
                cursor: pointer;
                line-height: 1;
                padding: 0 8px;
            }
            #agent-chat-window .agent-messages {
                flex: 1;
                padding: 16px;
                overflow-y: auto;
                display: flex;
                flex-direction: column;
                gap: 12px;
            }
            #agent-chat-window .message {
                max-width: 85%;
                padding: 10px 14px;
                border-radius: 12px;
                line-height: 1.4;
                word-wrap: break-word;
            }
            #agent-chat-window .message.user {
                align-self: flex-end;
                background: linear-gradient(90deg, rgba(6,179,176,0.2), rgba(6,179,176,0.1));
                border: 1px solid rgba(6,179,176,0.2);
                color: #a8eae6;
            }
            #agent-chat-window .message.agent {
                align-self: flex-start;
                background: rgba(255,255,255,0.05);
                border: 1px solid rgba(255,255,255,0.05);
                color: #bfeee9;
            }
            #agent-chat-window .agent-input-area {
                padding: 12px 16px;
                border-top: 1px solid rgba(6,179,176,0.1);
                display: flex;
                gap: 8px;
            }
            #agent-chat-window .agent-input {
                flex: 1;
                padding: 10px 14px;
                border-radius: 8px;
                border: 1px solid rgba(255,255,255,0.1);
                background: rgba(0,0,0,0.3);
                color: #bfeee9;
                outline: none;
            }
            #agent-chat-window .agent-input:focus {
                border-color: #06b3b0;
            }
            #agent-chat-window .agent-send {
                padding: 10px 16px;
                border-radius: 8px;
                border: none;
                background: linear-gradient(90deg, #06b3b0, #07d4d0);
                color: #021212;
                font-weight: bold;
                cursor: pointer;
            }
            #agent-chat-window .agent-examples {
                padding: 8px 16px 16px;
                font-size: 12px;
                color: #7ab7b5;
                display: flex;
                flex-wrap: wrap;
                gap: 8px;
                align-items: center;
            }
            #agent-chat-window .example {
                cursor: pointer;
                padding: 4px 8px;
                background: rgba(6,179,176,0.1);
                border-radius: 6px;
                transition: background 0.2s;
            }
            #agent-chat-window .example:hover {
                background: rgba(6,179,176,0.2);
            }
        `;
    }
    
    bindEvents() {
        // 按钮点击
        this.button.addEventListener('click', () => this.toggleChat());
        // 关闭按钮
        this.chatWindow.querySelector('.agent-close').addEventListener('click', () => this.closeChat());
        // 发送按钮
        this.chatWindow.querySelector('.agent-send').addEventListener('click', () => this.sendMessage());
        // 输入框回车
        this.chatWindow.querySelector('.agent-input').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') this.sendMessage();
        });
        // 示例点击
        this.chatWindow.querySelectorAll('.example').forEach(example => {
            example.addEventListener('click', (e) => {
                this.chatWindow.querySelector('.agent-input').value = e.target.textContent;
                this.sendMessage();
            });
        });
    }
    
    toggleChat() {
        this.isOpen = !this.isOpen;
        this.chatWindow.style.display = this.isOpen ? 'flex' : 'none';
        this.button.style.transform = this.isOpen ? 'scale(0.9)' : 'scale(1)';
        
        if (this.isOpen) {
            // 聚焦输入框
            setTimeout(() => {
                this.chatWindow.querySelector('.agent-input').focus();
            }, 100);
        }
    }
    
    closeChat() {
        this.isOpen = false;
        this.chatWindow.style.display = 'none';
        this.button.style.transform = 'scale(1)';
    }
    
    async sendMessage() {
        const input = this.chatWindow.querySelector('.agent-input');
        const message = input.value.trim();
        
        if (!message) return;
        
        // 添加用户消息
        this.addMessage(message, 'user');
        input.value = '';
        
        // 禁用输入
        input.disabled = true;
        const sendBtn = this.chatWindow.querySelector('.agent-send');
        sendBtn.disabled = true;
        sendBtn.textContent = '思考中...';
        
        try {
            // 发送到Agent服务
            const response = await fetch(`${this.options.serverUrl}/api/query`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ question: message })
            });
            
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.addMessage(data.answer, 'agent');
            
        } catch (error) {
            this.addMessage(`抱歉，助手暂时无法响应。错误: ${error.message}`, 'agent');
        } finally {
            // 恢复输入
            input.disabled = false;
            sendBtn.disabled = false;
            sendBtn.textContent = '发送';
            input.focus();
        }
    }
    
    addMessage(text, sender) {
        const messagesDiv = this.chatWindow.querySelector('.agent-messages');
        const messageDiv = document.createElement('div');
        messageDiv.className = `message ${sender}`;
        messageDiv.textContent = text;
        messagesDiv.appendChild(messageDiv);
        messagesDiv.scrollTop = messagesDiv.scrollHeight;
        
        // 保存到历史
        this.chatHistory.push({ sender, text, time: new Date() });
    }
    
    getPositionStyle(type = 'button') {
        const margin = type === 'window' ? '20px' : '20px';
        
        switch(this.options.position) {
            case 'bottom-right':
                return `bottom: ${margin}; right: ${margin};`;
            case 'bottom-left':
                return `bottom: ${margin}; left: ${margin};`;
            case 'top-right':
                return `top: ${margin}; right: ${margin};`;
            case 'top-left':
                return `top: ${margin}; left: ${margin};`;
            default:
                return `bottom: ${margin}; right: ${margin};`;
        }
    }
}

// 自动初始化（当DOM加载完成后）
document.addEventListener('DOMContentLoaded', function() {
    window.DeviceAgent = new DeviceAgentWidget();
});