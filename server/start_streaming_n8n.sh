#!/bin/bash
cd "$(dirname "$0")"

# Activate virtual environment
if [ -d "env" ]; then
    source env/bin/activate
elif [ -d "venv" ]; then
    source venv/bin/activate
fi

# Run the n8n streaming server
python server_streaming_n8n.py
