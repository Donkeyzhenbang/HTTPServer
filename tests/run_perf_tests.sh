#!/bin/bash
set -e

# Configuration
SERVER_BIN="../server/bin/httpserver"
TCP_CLIENT_BIN="./perf_tcp_client"
HTTP_CLIENT_SCRIPT="../tests/perf_http_client.py"
LOG_FILE="server_perf.log"
PID_FILE="server.pid"

# Paths
cd "$(dirname "$0")"
TEST_DIR=$(pwd)
ROOT_DIR=$(dirname "$TEST_DIR")

echo "========================================="
echo "Building TCP Perf Client..."
echo "========================================="
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
make perf_tcp_client

echo "========================================="
echo "Building Server..."
echo "========================================="
cd "$ROOT_DIR/server"
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
make

echo "========================================="
echo "Starting Server in Background..."
echo "========================================="
cd "$ROOT_DIR/tests"
if [ -f "$PID_FILE" ]; then
    kill -9 $(cat "$PID_FILE") 2>/dev/null || true
    rm "$PID_FILE"
fi

# Kill any existing server on port 52487
fuser -k -n tcp 52487 || true
sleep 1

# Run server (redirect output)
nohup "$ROOT_DIR/server/bin/httpserver" > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"
echo "Server started with PID $SERVER_PID. Logs in $LOG_FILE"

# Wait for server startup
sleep 2

echo "========================================="
echo "Running TCP QPS Test (50 threads, 10s)"
echo "========================================="
"$TEST_DIR/build/perf_tcp_client" 127.0.0.1 52487 50 10

echo "========================================="
echo "Running HTTP QPS Test (50 threads, 10s)"
echo "========================================="
python3 "$HTTP_CLIENT_SCRIPT"

echo "========================================="
echo "Cleaning Up..."
echo "========================================="
kill -9 $SERVER_PID
rm "$PID_FILE"
echo "Done."
