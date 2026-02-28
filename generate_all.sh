#!/bin/bash
# Generate all C++ headers from Python reference implementation
# Run this before building ESP32 firmware

echo "Generating C++ headers from Python..."
echo ""

python3 tools/generate_constants.py || {
    echo "ERROR: Failed to generate constants.h"
    exit 1
}
echo ""

python3 tools/generate_profiles.py || {
    echo "ERROR: Failed to generate profiles.h"
    exit 1
}
echo ""

python3 tools/generate_tests.py || {
    echo "ERROR: Failed to generate test files"
    exit 1
}
echo ""

echo ""
echo "========================================"
echo "All generators completed successfully!"
echo "========================================"
echo ""
echo "Generated files:"
echo "  - esp32/arduino/remote_control/constants.h"
echo "  - esp32/arduino/remote_control/profiles.h"
echo "  - esp32/test/test_mapper.cpp"
echo "  - esp32/test/test_supervisor.cpp"
echo ""
echo "You can now build the ESP32 firmware:"
echo "  cd esp32/arduino/remote_control"
echo "  arduino-cli compile --fqbn esp32:esp32:esp32s3"
echo ""
