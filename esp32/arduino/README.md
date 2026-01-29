# ESP32 Arduino Examples
C++ implementations for ESP32. Reference implementations, not primary focus.

## Projects

### wifi_joystick
Web-based WiFi controller with HTML5 joystick interface. Creates AP, serves web UI, connects to wheels via BLE.

### fake_m25_wheel
BLE peripheral simulator for testing without real wheels. Emulates M25 SPP service.

**Purpose:**
- Test BLE connection and discovery
- Validate protocol implementation
- Debug encryption/decryption
- Safe testing without moving real wheels

**Features:**
- BLE SPP service emulation
- Receives and logs encrypted packets
- Sends fake responses
- Simulates battery level
- Serial monitor logging

**Setup:**
1. Open `fake_m25_wheel/fake_m25_wheel.ino` in Arduino IDE
2. Install BLE libraries if needed (ESP32 built-in)
3. Upload to ESP32
4. Open Serial Monitor at 115200 baud
5. Run Python code to connect

**Configuration:**
```cpp
#define DEVICE_NAME "M25_FAKE_LEFT"  // Or M25_FAKE_RIGHT
```

For dual wheel testing, upload to two ESP32s with different names.

**Usage with m5squared:**
```python
# Python side - use fake wheel MAC address
from m25_bluetooth import BluetoothManager

bt = BluetoothManager()
devices = bt.scan()  # Should find M25_FAKE_LEFT
# Connect normally - fake wheel will respond
```

## Hardware
- ESP32-WROOM-32 or compatible

## Board Setup
1. File → Preferences → Additional Board Manager URLs:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Tools → Board → Boards Manager → Install "esp32"
3. Tools → Board → ESP32 Dev Module
