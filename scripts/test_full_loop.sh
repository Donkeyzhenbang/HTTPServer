#!/bin/bash

# Configuration
NODE1_PORT=8080
NODE2_PORT=8081
CLIENT_TARGET_PORT=52488 # Node 2 TCP Port
DEVICE_ID="10370000123456789"

echo "=== 1. Starting Distributed Server Cluster ==="
cd $(dirname "$0")
./run_distributed.sh > /dev/null 2>&1 &
RUN_PID=$!
sleep 3 # Wait for servers to start and register to ZK

echo "=== 2. Starting Client (Connecting to Node 2: $CLIENT_TARGET_PORT) ==="
# Run client in background, waiting for command (Mode 6)
# Client must run from its bin dir to find resources (../resource/photos)
cd ../client/bin
./ImageSend -c 6 -i 127.0.0.1 -p $CLIENT_TARGET_PORT > client.log 2>&1 &
CLIENT_PID=$!
echo "Client PID: $CLIENT_PID"
sleep 2 # Wait for client to connect and heartbeat

echo "=== 3. Sending Command to Node 1 (Port $NODE1_PORT) to Control Device on Node 2 ==="
# Send B341 command via Node 1
RESPONSE=$(curl -s -X POST http://127.0.0.1:$NODE1_PORT/api/send_b341 \
    -H "Content-Type: application/json" \
    -d "{\"device\": \"$DEVICE_ID\", \"channel\": 1}")

echo "Response from Node 1: $RESPONSE"

# Check if successful
if echo "$RESPONSE" | grep -q "true"; then
    echo "SUCCESS: Command sent and proxied successfully!"
else
    echo "FAILURE: Command failed."
    # Dump logs
    echo "--- Client Log ---"
    cat client.log
    echo "--- Node 1 Log ---"
    cat ../../logs/node1.log
    echo "--- Node 2 Log ---"
    cat ../../logs/node2.log
fi

echo "=== 4. Cleaning Up ==="
kill $CLIENT_PID
kill $RUN_PID
pkill httpserver
exit 0
