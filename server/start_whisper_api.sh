#!/bin/bash
# Activate virtual environment
source venv/bin/activate

# Set default model (optional, default is medium)
export WHISPER_MODEL="medium"
export PORT=7777

# Run the API server
echo "Starting Whisper API Server on port $PORT..."
python3 whisper_api.py
