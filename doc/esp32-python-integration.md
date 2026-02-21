# ESP32-Python Integration Strategy

## Decision: Hybrid Approach

**Use C++ (Arduino) on ESP32 for the production remote** - your `wifi_joystick` code already works, has stable BLE, and provides real-time guarantees needed for safety-critical wheelchair control. MicroPython has memory constraints (~120KB) and unreliable BLE on ESP32.

**Use Python for development tooling** - leverage your sophisticated `core/supervisor.py` and `core/mapper.py` safety logic to generate configuration files, validate C++ implementations, and run comprehensive tests. Python tests ensure C++ behavior matches validated algorithms.

**Bridge them with shared protocols** - Python generates `device_config.h` from QR codes, creates test harnesses for C++ code, and can optionally generate C++ functions from validated Python algorithms. Both sides implement the same command validation rules for defense-in-depth safety.

**Why**: For wheelchair control, reliability trumps code reuse. Your Arduino implementation is production-ready while maintaining Python's development advantages where they matter most.
