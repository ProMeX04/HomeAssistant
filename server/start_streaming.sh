#!/bin/bash
# Start TRUE Streaming Whisper Server

cd "$(dirname "$0")"

# Use Python 3.12 venv for better compatibility
if [ -d "venv_py312" ]; then
    source venv_py312/bin/activate
elif [ -d "venv" ]; then
    source venv/bin/activate
else
    echo "❌ Virtual environment not found!"
    exit 1
fi

echo "🚀 Starting TRUE Streaming Whisper Server..."
python server_streaming.py
