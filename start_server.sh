#!/bin/bash

# Helper script to start the server, killing any existing instance first

PORT=8080
SERVER="./server"

# Kill any existing server process on port 8080
echo "Checking for existing server on port $PORT..."
PID=$(lsof -ti :$PORT 2>/dev/null)

if [ ! -z "$PID" ]; then
    echo "Found existing server (PID: $PID). Killing it..."
    kill $PID 2>/dev/null
    sleep 1
    
    # Force kill if still running
    if kill -0 $PID 2>/dev/null; then
        echo "Force killing..."
        kill -9 $PID 2>/dev/null
        sleep 1
    fi
    echo "Old server terminated."
fi

# Check if server binary exists
if [ ! -f "$SERVER" ]; then
    echo "Error: $SERVER not found. Run 'make' first."
    exit 1
fi

# Start the server
echo "Starting server on port $PORT..."
$SERVER

