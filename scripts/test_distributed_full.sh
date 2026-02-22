#!/bin/bash

# Configuration
REDIS_HOST="127.0.0.1:6379"
ZK_HOST="127.0.0.1:2181"
LOCAL_IP="127.0.0.1"

ROOT_DIR=$(pwd)
SERVER_BIN="$ROOT_DIR/server/bin/httpserver"
CLIENT_BIN="$ROOT_DIR/client/bin/ImageSend"
LOG_DIR="$ROOT_DIR/logs"

# Check binaries
if [ ! -f "$SERVER_BIN" ]; then
    echo "Error: Server binary not found at $SERVER_BIN"
    exit 1
fi
if [ ! -f "$CLIENT_BIN" ]; then
    echo "Error: Client binary not found at $CLIENT_BIN"
    exit 1
fi

# Check Redis availability
if ! command -v redis-cli &> /dev/null; then
    echo "Warning: redis-cli not found. Cannot verify Redis keys."
else
    if ! redis-cli ping &> /dev/null; then
        echo "Error: Redis server is not running or not reachable."
        echo "Please start Redis: redis-server &"
        # Optional: Attempt to start if binary exists but not running
        if command -v redis-server &> /dev/null; then
             echo "Attempting to start Redis..."
             redis-server --daemonize yes
             sleep 1
        fi
    fi
fi

# Check ZooKeeper availability (simple port check)
if ! nc -z 127.0.0.1 2181 &> /dev/null; then
     echo "Warning: ZooKeeper appears to be down (port 2181 closed)."
     echo "Please start ZooKeeper for full distributed function."
fi

mkdir -p "$LOG_DIR"

# Cleanup
echo "Cleaning up old processes..."
pkill httpserver
pkill ImageSend
sleep 1

# Start Node 1
echo "Starting Node 1 (TCP: 52487, HTTP: 8080)..."
$SERVER_BIN -p 52487 -w 8080 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > "$LOG_DIR/node1.log" 2>&1 &
PID1=$!

# Start Node 2
echo "Starting Node 2 (TCP: 52488, HTTP: 8081)..."
$SERVER_BIN -p 52488 -w 8081 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > "$LOG_DIR/node2.log" 2>&1 &
PID2=$!

# Start Node 3
echo "Starting Node 3 (TCP: 52489, HTTP: 8082)..."
$SERVER_BIN -p 52489 -w 8082 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > "$LOG_DIR/node3.log" 2>&1 &
PID3=$!

echo "Waiting 3 seconds for servers to initialize..."
sleep 3

# Start Client connected to Node 2 (Port 52488)
# Run in background to keep connection open (simulating device)
# Note: ImageSend typically sends and exits, but if we loop it or use a specific mode?
# Using default mode, it connects, sends heartbeat, sends file, waits for ack. 
# Hopefully it stays connected long enough for us to test proxy.
# If not, we might need a dummy client loop.

echo "Starting Client (connecting to Node 2: 52488)..."
# Just run it. If it exits, we'll see.
$CLIENT_BIN -p 52488 -i 127.0.0.1 -c 6 > "$LOG_DIR/client.log" 2>&1 &
CLIENT_PID=$!

echo "Waiting 2 seconds for client to register..."
sleep 2

# Verify Redis State
echo "Verifying Redis registration..."
DEVICE_ID="10370000123456789"
REDIS_KEY="device:online:$DEVICE_ID"
REDIS_VAL=$(redis-cli GET "$REDIS_KEY")
echo "Redis Key [$REDIS_KEY] = $REDIS_VAL"

if [[ -z "$REDIS_VAL" ]]; then
    echo "FAIL: Device not found in Redis!"
else 
    echo "SUCCESS: Device registered in Redis."
fi

# Test Proxy: Send Command to Node 1 (8080), expect forwarding to Node 2 (8080 -> 8081?)
# Wait, Node 2 HTTP is 8081.
echo "Testing HTTP Proxy (Node 1 -> Node 2)..."
RESPONSE=$(curl -s -X POST http://127.0.0.1:8080/api/send_b341 \
    -H "Content-Type: application/json" \
    -d "{\"device\":\"$DEVICE_ID\", \"channel\":1}")

echo "Response: $RESPONSE"

if [[ "$RESPONSE" == *"ok\":true"* ]]; then
    echo "SUCCESS: Proxy command execution successful."
else
    echo "FAIL: Proxy command failed."
fi

# Send Image to Node 3 (Testing generic upload, not strictly distributed but good valid check)
# RESPONSE=$(curl -s -X POST http://127.0.0.1:8082/upload \
#    -F "image=@$ROOT_DIR/reademe.md")
# echo "Upload Response: $RESPONSE"

echo "Test Complete."
echo "Killing processes..."
kill $PID1 $PID2 $PID3 $CLIENT_PID 2>/dev/null
