#!/bin/bash
# Build script for ESP32-LyraT-Mini project

set -e

# Set up ESP-ADF/ESP-IDF environment
export ADF_PATH=$HOME/esp/esp-adf
export IDF_PATH=$HOME/esp/esp-adf/esp-idf

# Activate Python virtual environment
PYTHON_VENV="$HOME/.espressif/python_env/idf5.4_py3.14_env"
if [ -d "$PYTHON_VENV" ]; then
    source "$PYTHON_VENV/bin/activate"
    export PATH="$PYTHON_VENV/bin:$PATH"
fi

# Add IDF tools to PATH
export PATH="$IDF_PATH/tools:$PATH"
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin:$PATH"
export PATH="$HOME/.espressif/tools/esp32ulp-elf/2.38_20240113/esp32ulp-elf/bin:$PATH"
export PATH="$HOME/.espressif/tools/cmake/3.24.0/CMake.app/Contents/bin:$PATH"

# Navigate to project directory  
cd "$(dirname "$0")"

# Check if idf.py exists
if [ -f "$IDF_PATH/tools/idf.py" ]; then
    echo "Using idf.py from: $IDF_PATH/tools/idf.py"
    python3 "$IDF_PATH/tools/idf.py" build
else
    echo "ERROR: idf.py not found at $IDF_PATH/tools/idf.py"
    exit 1
fi

echo ""
echo "Build complete!"
