#!/bin/bash

# Whisper Talk LLaMA Startup Script
# Starts the HTTP server and opens the web interface

echo "🚀 Starting Whisper Talk LLaMA System..."

# Change to the whisper.cpp directory
cd "/Users/whisper/Documents/augment-projects/github versionierung/whisper.cpp"

# Check if HTTP server executable exists
HTTP_SERVER="./build/bin/whisper-http-server"
if [ ! -f "$HTTP_SERVER" ]; then
    echo "❌ HTTP server not found at: $HTTP_SERVER"
    echo "Please build the project first with: cmake --build build --config Release"
    exit 1
fi

echo "✅ Found HTTP server executable"

# Kill any existing HTTP server on port 8081
echo "🔄 Checking for existing HTTP server..."
if lsof -ti:8081 > /dev/null 2>&1; then
    echo "🛑 Stopping existing HTTP server on port 8081..."
    kill $(lsof -ti:8081) 2>/dev/null || true
    sleep 2
fi

# Start HTTP server in background
echo "🌐 Starting HTTP server on port 8081..."
"$HTTP_SERVER" --port 8081 &
HTTP_PID=$!

# Wait a moment for server to start
sleep 3

# Check if server started successfully
if ! kill -0 $HTTP_PID 2>/dev/null; then
    echo "❌ Failed to start HTTP server"
    exit 1
fi

echo "✅ HTTP server started successfully (PID: $HTTP_PID)"

# Open web interface in default browser
echo "🌐 Opening web interface..."
open "http://localhost:8081"

echo ""
echo "🎉 Whisper Talk LLaMA is now running!"
echo ""
echo "📋 Available services:"
echo "   • Web Interface: http://localhost:8081"
echo "   • HTTP Server PID: $HTTP_PID"
echo ""
echo "💡 Usage:"
echo "   • Configure SIP lines through the web interface"
echo "   • Start SIP client manually if needed:"
echo "     ./build/bin/whisper-sip-client --simulate"
echo ""
echo "🛑 To stop the HTTP server:"
echo "   kill $HTTP_PID"
echo ""
echo "Press Ctrl+C to stop this script (HTTP server will continue running)"

# Keep script running to show status
trap 'echo ""; echo "👋 Script stopped. HTTP server is still running."; exit 0' INT

# Monitor the HTTP server
while kill -0 $HTTP_PID 2>/dev/null; do
    sleep 5
done

echo "❌ HTTP server stopped unexpectedly"
