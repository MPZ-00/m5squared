#!/bin/bash
# Quick launcher for m5squared GUI on Linux/Mac
# Make executable with: chmod +x start.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Starting m5squared GUI..."
echo ""

if [ -x ".venv/bin/python" ]; then
    .venv/bin/python launch.py
else
    python3 launch.py
fi
