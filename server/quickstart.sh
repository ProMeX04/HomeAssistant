#!/bin/bash

# Quick Start Guide: Test Gemini Integration
# This script helps you quickly test the Gemini audio processing without ESP32

echo "🚀 Quick Start: Testing Gemini Audio Processing"
echo "================================================"
echo ""

# Check if API key is set
if [ -z "$GEMINI_API_KEY" ]; then
    echo "❌ GEMINI_API_KEY not set!"
    echo ""
    echo "Please set your API key first:"
    echo "  1. Get API key from: https://aistudio.google.com/app/apikey"
    echo "  2. Run: export GEMINI_API_KEY='your-api-key'"
    echo "  3. Or use: ./setup_api_key.sh"
    echo ""
    exit 1
fi

echo "✅ GEMINI_API_KEY is set"
echo ""

# Check if venv exists
if [ ! -d "venv" ]; then
    echo "📦 Creating virtual environment..."
    python3.9 -m venv venv
    echo "✅ Virtual environment created"
fi

# Activate venv and install dependencies
echo "📥 Installing dependencies..."
source venv/bin/activate
pip install -q -r requirements.txt
echo "✅ Dependencies installed"
echo ""

# Create a test audio file if it doesn't exist
if [ ! -d "test_audio" ]; then
    mkdir -p test_audio
    echo "📁 Created test_audio directory"
fi

echo "🎙️  Test Audio Options:"
echo "  1. Use existing WAV file from recordings/"
echo "  2. Record audio using your Mac's microphone"
echo "  3. Start server and wait for ESP32"
echo ""
read -p "Choose option (1-3): " option

case $option in
    1)
        # List existing recordings
        if [ -d "recordings" ] && [ "$(ls -A recordings/*.wav 2>/dev/null)" ]; then
            echo ""
            echo "📂 Available recordings:"
            ls -lh recordings/*.wav | awk '{print "   " $9 " (" $5 ")"}'
            echo ""
            read -p "Enter filename to test (or press Enter to skip): " testfile
            if [ -n "$testfile" ]; then
                echo "🧪 Testing with: $testfile"
                echo "   You can manually test by running:"
                echo "   curl -X POST -H 'Content-Type: audio/wav' --data-binary @$testfile http://localhost:6666/upload_audio"
            fi
        else
            echo "❌ No recordings found in recordings/ directory"
        fi
        ;;
    2)
        echo "🎤 Recording with Mac microphone..."
        echo "   Press Ctrl+C when done (or wait 5 seconds)"
        rec test_audio/test_recording.wav rate 16k channels 1 trim 0 5 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "✅ Recording saved: test_audio/test_recording.wav"
            echo "🧪 Testing with Gemini (WebSocket)..."
            
            # Create a temporary python script to test streaming
            cat <<EOF > test_stream.py
import asyncio
import websockets
import sys

async def send_audio():
    uri = "ws://localhost:6666/audio"
    async with websockets.connect(uri) as websocket:
        print(f"Connected to {uri}")
        
        # Read audio file
        with open("test_audio/test_recording.wav", "rb") as f:
            # Skip header (44 bytes) if we want to simulate raw PCM, 
            # but for now let's send it all or skip it.
            # The server expects raw PCM, so let's skip header
            f.read(44) 
            audio_data = f.read()
            
        # Send audio in chunks
        chunk_size = 4096
        for i in range(0, len(audio_data), chunk_size):
            chunk = audio_data[i:i+chunk_size]
            await websocket.send(chunk)
            await asyncio.sleep(0.01) # Simulate real-time
            
        print("Audio sent, sending END signal...")
        await websocket.send("END")
        
        # Wait for response
        while True:
            try:
                message = await websocket.recv()
                if isinstance(message, str):
                    print(f"Received text: {message}")
                    if message == "AUDIO_END":
                        break
                else:
                    print(f"Received audio: {len(message)} bytes")
            except websockets.exceptions.ConnectionClosed:
                break
                
asyncio.run(send_audio())
EOF
            # Install websockets if needed
            pip install -q websockets
            
            # Run the test script in background while server runs
            # We need to run the server first!
            echo "⚠️  We need to start the server first. The test will run automatically after server starts."
            echo "   (This is a bit tricky in a single script, so we'll just prepare the file)"
            echo "   To run the test, open a NEW terminal and run:"
            echo "   source venv/bin/activate && python test_stream.py"
        else
            echo "⚠️  sox not installed or recording failed."
        fi
        ;;
    3)
        echo "🌐 Starting server..."
        echo "   Server will listen on port 6666"
        echo "   ESP32 can now send audio to this server"
        ;;
esac

echo ""
echo "🚀 Starting Flask Server..."
echo "   Press Ctrl+C to stop"
echo ""

# Start the server
# Start the WebSocket server
export PYTHONUNBUFFERED=1
venv/bin/python server_ws.py

