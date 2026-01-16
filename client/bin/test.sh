#!/bin/bash

echo "启动4个传图进程，通道1到4..."

./ImageSend 1 &
./ImageSend 2 &
./ImageSend 3 &
./ImageSend 4 &
echo "第一组进程已启动，等待10秒..."

# sleep 1

# echo "启动第二组4个传图进程，通道1到4..."
# ./ImageSend 1 &
# ./ImageSend 2 &
# ./ImageSend 3 &
# ./ImageSend 4 &

echo "所有进程已启动，按 Ctrl+C 停止所有进程"

# 等待用户中断
trap 'echo "正在停止所有进程..."; kill $(jobs -p); exit' INT
wait