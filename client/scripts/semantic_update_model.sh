#!/bin/bash
## 异物检测+语义分割模型升级脚本 
echo "=== 开始执行模型升级脚本 正在执行mv操作 ==="
mv "$1" "$2/"
cd "$2"
# 进入上一级目录
cd ..
echo "当前目录: $(pwd)"
echo "=== 正在执行编译 ==="
# make clean
# make -j$(nproc)
echo "=== 正在异物检测+语义分割程序 ==="
# cp output_HDR.jpg ../
# bash excute_model.sh