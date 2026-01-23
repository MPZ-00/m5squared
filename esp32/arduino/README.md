# ESP32 Arduino Examples
Arduino/C++ implementations for ESP32 hardware.

## Projects
### fake_m25_wheel
Simulates an M25 wheel as a BLE peripheral for testing the Python control code without real wheels.

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

## Requirements
### Hardware
- ESP32-WROOM-32 or compatible
- USB cable
- (Optional) Second ESP32 for dual-wheel testing

### Software
- Arduino IDE 2.x
- ESP32 board support
- Built-in BLE libraries

### Board Setup (Arduino IDE)
1. File → Preferences
2. Additional Board Manager URLs:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Tools → Board → Boards Manager
4. Search "esp32" and install "esp32 by Espressif"
5. Select: Tools → Board → ESP32 Dev Module

## Serial Monitor Output Example
```
=================================
Fake M25 Wheel Simulator
=================================

Device name: M25_FAKE_LEFT

BLE device ready!
Waiting for connection...

Client connected!
=== CONNECTED ===
Ready to receive commands

Received packet (16 bytes): A3 F2 1B 8C 44 9F 23 11 CC DD EE FF 00 11 22 33
Valid packet size (encrypted)
Sent response

Received packet (16 bytes): 88 99 AA BB CC DD EE FF 11 22 33 44 55 66 77 88
Valid packet size (encrypted)
Sent response
```

## Future Arduino Projects
Planned additions:
- **m25_controller** - Full joystick controller implementation
- **m25_bridge** - WiFi-to-BLE bridge
- **m25_logger** - Data logger with SD card
- **ble_scanner** - Simple BLE device scanner

## See Also
- [ESP32 Setup Guide](../../doc/esp32-setup.md)
- [M25 Protocol](../../doc/m25-protocol.md)
- [BLE Cross-Platform](../../doc/ble-cross-platform.md)
