#!/bin/bash
## PC HDR模型升级脚本 
echo "=== [PC x86_64] 开始执行PC端模型升级脚本 ==="
echo "源文件: $1"
echo "目标目录: $2"
mkdir -p "$2"
cp "$1" "$2/" # 用cp替代mv方便反复测试
cd "$2"
echo "当前目录: $(pwd)"
echo "=== [PC] 正在执行编译 (模拟) ==="
sleep 1
echo "=== [PC] 正在运行程序 ==="
sleep 1
echo "=== 分布式升级完成 ==="
