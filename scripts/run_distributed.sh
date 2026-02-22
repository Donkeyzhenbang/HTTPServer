#!/bin/bash

# Configuration
# Assuming local Redis and Zookeeper are running
REDIS_HOST="127.0.0.1:6379"
ZK_HOST="127.0.0.1:2181"
LOCAL_IP="127.0.0.1"

# Get script directory
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
ROOT_DIR=$(dirname "$SCRIPT_DIR")

BIN_PATH="$ROOT_DIR/server/bin/httpserver"

# Check if binary exists
if [ ! -f "$BIN_PATH" ]; then
    echo "Error: Server binary not found at $BIN_PATH"

    echo "Please build the project first."
    exit 1
fi

# Kill existing instances
pkill httpserver
sleep 1

# Start Node 1
echo "Starting Node 1 (TCP: 52487, HTTP: 8080)..."
$BIN_PATH -p 52487 -w 8080 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > $ROOT_DIR/logs/node1.log 2>&1 &
PID1=$!
echo "Node 1 PID: $PID1"

# Start Node 2
echo "Starting Node 2 (TCP: 52488, HTTP: 8081)..."
$BIN_PATH -p 52488 -w 8081 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > $ROOT_DIR/logs/node2.log 2>&1 &
PID2=$!
echo "Node 2 PID: $PID2"

# Start Node 3
echo "Starting Node 3 (TCP: 52489, HTTP: 8082)..."
$BIN_PATH -p 52489 -w 8082 -z $ZK_HOST -r $REDIS_HOST -i $LOCAL_IP > $ROOT_DIR/logs/node3.log 2>&1 &
PID3=$!
echo "Node 3 PID: $PID3"

echo "Distributed cluster running."
echo "Logs are in logs/node*.log"
echo "Press Ctrl+C to stop all nodes."

trap "kill $PID1 $PID2 $PID3; exit" INT TERM
wait
