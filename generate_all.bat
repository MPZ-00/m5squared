@echo off
REM Generate all C++ headers from Python reference implementation
REM Run this before building ESP32 firmware

echo Generating C++ headers from Python...
echo.

python tools\generate_constants.py
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to generate constants.h
    exit /b 1
)
echo.

python tools\generate_profiles.py
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to generate profiles.h
    exit /b 1
)
echo.

python tools\generate_tests.py
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to generate test files
    exit /b 1
)
echo.

echo.
echo ========================================
echo All generators completed successfully!
echo ========================================
echo.
echo Generated files:
echo   - esp32/arduino/remote_control/constants.h
echo   - esp32/arduino/remote_control/profiles.h
echo   - esp32/test/test_mapper.cpp
echo   - esp32/test/test_supervisor.cpp
echo.
echo You can now build the ESP32 firmware:
echo   cd esp32/arduino/remote_control
echo   arduino-cli compile --fqbn esp32:esp32:esp32s3
echo.
