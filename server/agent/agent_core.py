import os
import requests
import dashscope
from typing import Dict, Any, List
from dotenv import load_dotenv

# 加载环境变量
load_dotenv()

# 设置DashScope API密钥
dashscope.api_key = os.getenv("DASHSCOPE_API_KEY")
if not dashscope.api_key:
    raise ValueError("请设置 DASHSCOPE_API_KEY 环境变量")

class DeviceTools:
    """设备查询工具类 - 直接调用你的C++后端API"""
    
    def __init__(self, backend_url="http://127.0.0.1:8080"):
        self.backend_url = backend_url
    
    def get_device_list(self) -> str:
        """获取设备列表"""
        try:
            resp = requests.get(f"{self.backend_url}/api/devices", timeout=5)
            if resp.status_code == 200:
                data = resp.json()
                devices = data.get("devices", [])
                if devices:
                    return f"当前在线设备({len(devices)}个): {', '.join(devices)}"
                return "当前没有在线的设备。"
            return "获取设备列表失败。"
        except Exception as e:
            return f"查询设备列表出错: {str(e)}"
    
    def get_connection_stats(self) -> str:
        """获取连接统计"""
        try:
            resp = requests.get(f"{self.backend_url}/api/connections", timeout=5)
            if resp.status_code == 200:
                data = resp.json()
                total = data.get("total_connections", 0)
                registered = data.get("registered_devices", 0)
                return f"连接统计: 总连接数={total}, 已注册设备={registered}"
            return "获取连接统计失败。"
        except Exception as e:
            return f"查询连接统计出错: {str(e)}"

class QwenAgent:
    """基于Qwen的简化版Agent"""
    
    def __init__(self, tools: Dict[str, Any]):
        self.tools = tools
        self.conversation_history = []
        
    def get_tools_description(self) -> str:
        """生成工具描述，用于提示词"""
        descriptions = []
        for name, tool in self.tools.items():
            descriptions.append(f"- {name}: {tool['description']}")
        return "\n".join(descriptions)
    
    def call_qwen_api(self, prompt: str) -> str:
        """直接调用Qwen API"""
        try:
            response = dashscope.Generation.call(
                model="qwen-turbo",
                prompt=prompt,
                max_tokens=1500,
                temperature=0.1,
                top_p=0.8,
                result_format='message',
            )
            
            if response.status_code == 200:
                return response.output.choices[0].message.content
            else:
                return f"API调用失败: {response.code} - {response.message}"
        except Exception as e:
            return f"调用Qwen API时出错: {str(e)}"
    
    def process_query(self, user_query: str) -> str:
        """处理用户查询的核心逻辑"""
        # 1. 构建系统提示词
        system_prompt = f"""你是一个设备管理助手，可以查询设备状态。
可用的工具和信息：
{self.get_tools_description()}

请根据用户问题选择合适工具获取信息，然后用中文清晰回答。
如果问题超出范围，请礼貌告知。

用户问题：{user_query}

请按以下步骤处理：
1. 先思考用户需要什么信息
2. 调用相关工具获取数据
3. 基于获取的数据回答

开始处理："""
        
        # 2. 先检查是否需要调用工具
        need_tool = False
        tool_name = None
        
        # 简单的关键词匹配（实际可以更智能）
        tool_keywords = {
            "get_device_list": ["设备", "列表", "在线", "哪些设备"],
            "get_connection_stats": ["连接", "统计", "数量", "多少个"]
        }
        
        for tool, keywords in tool_keywords.items():
            if any(keyword in user_query for keyword in keywords):
                need_tool = True
                tool_name = tool
                break
        
        # 3. 如果需要工具，先调用工具获取数据
        if need_tool and tool_name in self.tools:
            tool_result = self.tools[tool_name]["function"]()
            tool_prompt = f"{system_prompt}\n\n工具调用结果：{tool_result}\n\n基于以上信息，请回答用户："
            return self.call_qwen_api(tool_prompt)
        
        # 4. 否则直接回答
        return self.call_qwen_api(system_prompt)

def create_agent():
    """创建并返回Agent实例"""
    
    # 初始化工具
    device_tools = DeviceTools()
    
    # 定义工具集
    tools = {
        "get_device_list": {
            "function": device_tools.get_device_list,
            "description": "获取当前所有在线设备的列表。当用户问'有哪些设备'、'设备列表'时使用。"
        },
        "get_connection_stats": {
            "function": device_tools.get_connection_stats,
            "description": "获取系统的连接统计信息，包括总连接数和已注册设备数。"
        }
    }
    
    # 创建Agent实例
    agent = QwenAgent(tools)
    return agent

# 测试代码
if __name__ == "__main__":
    print("正在初始化Qwen Agent...")
    agent = create_agent()
    print("Agent初始化成功！")
    
    # 测试
    test_queries = [
        "当前有哪些设备？",
        "连接数是多少？",
        "系统状态怎么样？"
    ]
    
    for query in test_queries:
        print(f"\n用户: {query}")
        response = agent.process_query(query)
        print(f"助手: {response}")