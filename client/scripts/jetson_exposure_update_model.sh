#!/bin/bash
## Jetson Orin Nano HDR模型升级脚本 
echo "=== [Jetson Orin Nano aarch64] 开始执行Jetson端模型升级脚本 ==="
echo "源文件: $1"
echo "目标目录: $2"
mkdir -p "$2"
cp "$1" "$2/" # 用cp替代mv方便反复测试
cd "$2"
echo "当前目录: $(pwd)"
echo "=== [Jetson] 加载TensorRT环境 (模拟) ==="
sleep 1
echo "=== [Jetson] 重新编译CUDA/TRT代码 (模拟) ==="
# make clean && make -j$(nproc)
sleep 1
echo "=== [Jetson] 模型已加载到NVDLA/GPU ==="
echo "=== 边缘计算节点分布式升级成功 ==="
