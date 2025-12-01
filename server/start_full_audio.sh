#!/bin/bash

# Start Full Audio Server

# Activate virtual environment
if [ -d "venv" ]; then
    source venv/bin/activate
else
    echo "Virtual environment not found. Run setup first."
    exit 1
fi

echo "Starting Full Audio Server..."
python3 server_full_audio.py
