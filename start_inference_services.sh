#!/bin/bash
# 推理服务一键启动脚本

echo "=== 推理服务启动脚本 ==="

# 1. 检查 GPU Worker 是否已运行
if pgrep -f "hdr_gpu_worker" > /dev/null; then
    echo "[1] GPU Worker 已运行 (PID: $(pgrep -f hdr_gpu_worker))"
else
    echo "[1] 启动 GPU Worker..."
    cd /home/jym/cpp/gw-server/ai-services
    source ~/venv/hdr-venv/bin/activate
    nohup python hdr_gpu_worker.py > /tmp/hdr_worker.log 2>&1 &
    sleep 6
    if pgrep -f "hdr_gpu_worker" > /dev/null; then
        echo "    GPU Worker 启动成功 (PID: $(pgrep -f hdr_gpu_worker))"
        tail -5 /tmp/hdr_worker.log
    else
        echo "    GPU Worker 启动失败!"
        tail -10 /tmp/hdr_worker.log
        exit 1
    fi
fi

# 2. 检查 Gateway 是否已运行
if pgrep -f "httpserver" > /dev/null; then
    echo "[2] Gateway 已运行 (PID: $(pgrep -f httpserver))"
else
    echo "[2] 启动 Gateway..."
    cd /home/jym/cpp/gw-server/server
    nohup ./bin/httpserver -p 52487 -w 8080 > /tmp/httpserver.log 2>&1 &
    sleep 3
    if pgrep -f "httpserver" > /dev/null; then
        echo "    Gateway 启动成功 (PID: $(pgrep -f httpserver))"
        tail -3 /tmp/httpserver.log
    else
        echo "    Gateway 启动失败!"
        tail -10 /tmp/httpserver.log
        exit 1
    fi
fi

echo ""
echo "=== 服务状态 ==="
echo "GPU Worker: $(pgrep -f hdr_gpu_worker | xargs -I{} ps -p {} -o comm= 2>/dev/null || echo '未运行')"
echo "Gateway:    $(pgrep -f httpserver | xargs -I{} ps -p {} -o comm= 2>/dev/null || echo '未运行')"
echo ""
echo "访问地址: http://localhost:8080"
echo ""
echo "按 Ctrl+C 停止服务"
echo ""

# 等待用户中断
trap "echo '正在停止服务...'; pkill -f hdr_gpu_worker 2>/dev/null; pkill -f httpserver 2>/dev/null; exit" INT TERM
wait
