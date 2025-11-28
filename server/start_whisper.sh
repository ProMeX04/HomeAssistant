#!/bin/bash
# Start server with Python 3.12 venv for Whisper support

cd "$(dirname "$0")"
source venv_py312/bin/activate
python server_whisper.py
