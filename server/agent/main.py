from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional
import uvicorn
from datetime import datetime

from agent_core import create_agent

app = FastAPI(title="设备智能助手API", version="1.0.0")

# 配置CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 创建Agent实例
agent = create_agent()

# 请求/响应模型
class QueryRequest(BaseModel):
    question: str
    user_id: Optional[str] = None

class QueryResponse(BaseModel):
    answer: str
    success: bool = True
    timestamp: str

@app.get("/")
async def root():
    return {
        "service": "Qwen Device Agent",
        "status": "running",
        "model": "qwen-turbo",
        "endpoints": {
            "POST /api/query": "处理自然语言查询",
            "GET /health": "健康检查"
        }
    }

@app.post("/api/query", response_model=QueryResponse)
async def handle_query(request: QueryRequest):
    """处理自然语言查询"""
    try:
        # 使用Agent处理查询
        answer = agent.process_query(request.question)
        
        return QueryResponse(
            answer=answer,
            timestamp=datetime.now().isoformat()
        )
    except Exception as e:
        print(f"处理查询时出错: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"处理失败: {str(e)}"
        )

@app.get("/health")
async def health_check():
    """健康检查"""
    return {
        "status": "healthy",
        "service": "qwen-agent",
        "timestamp": datetime.now().isoformat()
    }

@app.get("/api/tools")
async def list_tools():
    """列出可用工具"""
    return {
        "tools": ["查询设备列表", "查看连接统计"],
        "model": "qwen-turbo"
    }

if __name__ == "__main__":
    uvicorn.run(
        app,
        host="0.0.0.0",
        port=8000,
        log_level="info"
    )