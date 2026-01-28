# WiFi Virtual Joystick Controller
Web-based touchscreen joystick for controlling M25 wheels via ESP32.

## Overview
This solution uses WiFi for phone connectivity and BLE to connect to M25 wheels:

- **ESP32 creates WiFi access point** - No router needed
- **Web-based interface** - No app installation required  
- **Touch-optimized joystick** - Natural touchscreen control
- **Real-time WebSocket** - Low latency communication
- **BLE connection to M25 wheels** - Automatic connection and reconnection
- **AES encryption** - Secure wheel communication
- **Works on any device** - Phone, tablet, laptop

## Hardware Required
- ESP32-WROOM-32 or compatible board
- USB cable for programming
- (Optional) Power bank for portable operation

## Quick Start
### 1. Install Required Libraries
Open Arduino IDE and install these libraries via Library Manager:

- **WebSockets** by Markus Sattler

### 2. Configure Device
Edit `device_config.h`:

```cpp
// Set your encryption key (from QR code)
#define ENCRYPTION_KEY { \
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, \
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0  \
}

// Set wheel MAC addresses (find using BLE scanner)
#define LEFT_WHEEL_MAC  "AA:BB:CC:DD:EE:FF"
#define RIGHT_WHEEL_MAC "11:22:33:44:55:66"

// Choose which wheel to connect to
#define CONNECT_LEFT_WHEEL  // or CONNECT_RIGHT_WHEEL
```

### 3. Upload HTML Files
1. Install **ESP32 Sketch Data Upload** plugin for Arduino IDE
   - Download from: https://github.com/me-no-dev/arduino-esp32fs-plugin
   - Extract to `Arduino/tools/ESP32FS/tool/esp32fs.jar`
2. In Arduino IDE: **Tools → ESP32 Sketch Data Upload**
3. Wait for upload to complete

### 4. Upload Sketch
1. Open `wifi_joystick.ino` in Arduino IDE
2. Select your ESP32 board under Tools → Board
3. Select the correct COM port under Tools → Port
4. Click Upload

### 3. Connect from Phone
1. **ESP32 powers on** and creates WiFi network:
   - **Network name**: `M25-Controller`
   - **Password**: `m25wheel`

2. **Connect phone to WiFi**:
   - Open WiFi settings on phone
   - Connect to `M25-Controller`
   - Enter password: `m25wheel`

3. **Open web interface**:
   - Open browser (Chrome, Safari, Firefox)
   - Go to: **http://192.168.4.1**
   - Joystick interface will load

4. **Control wheelchair**:
   - Touch and drag the joystick
   - Release to stop (deadman switch)
   - Use emergency stop button if needed
   - Watch BLE status indicator (should show "Connected")

## File Structure

```
wifi_joystick/
├── wifi_joystick.ino    # Main Arduino sketch
├── device_config.h      # Configuration (keys, MAC addresses)
├── data/
│   └── index.html       # Web interface (uploaded to SPIFFS)
└── README.md
```

## Configuration
### Finding Wheel MAC Addresses

Use a BLE scanner app or the Python scripts:

```bash
# Using Python
python m25_bluetooth.py scan

# Or use BLE scanner app on phone
# - Android: "BLE Scanner" or "nRF Connect"
# - iOS: "LightBlue Explorer"
```

### Getting Encryption Key

Extract from QR code using the Python tool:

```bash
python m25_qr_to_key.py path/to/qr_image.png
```

### Change WiFi Credentials

Edit in `device_config.h`:

```cpp
#define WIFI_SSID "M25-Controller"
#define WIFI_PASSWORD "m25wheel"  // Min 8 characters
```

### Adjust Control Sensitivity
Modify the joystick-to-speed mapping:

```cpp
// Scale to M25 speed range
leftSpeed = (int)(left * 100);   // Change 100 to reduce max speed
rightSpeed = (int)(right * 100);
```

For example, use `50` for half speed:
```cpp
leftSpeed = (int)(left * 50);
rightSpeed = (int)(right * 50);
```

### Change Update Rate
Default is 20Hz (50ms interval):

```cpp
const unsigned long COMMAND_INTERVAL = 50;  // 50ms = 20Hz
```

Increase for more responsive control (e.g., 25ms = 40Hz):
```cpp
const unsigned long COMMAND_INTERVAL = 25;  // 40Hz
```

## How It Works
```
┌─────────────┐     WiFi      ┌─────────────┐     BLE      ┌──────────┐
│   Phone     │◄──────────────►│    ESP32    │◄────────────►│  M25     │
│  (Browser)  │   WebSocket   │             │   Encrypted  │  Wheels  │
└─────────────┘   Joystick    └─────────────┘   Commands   └──────────┘
                   Data              |
                                     |
                                  SPIFFS
                                (index.html)
```

1. **Phone → ESP32**: Touch joystick sends X/Y coordinates via WebSocket
2. **ESP32 Processing**: Converts joystick to differential wheel speeds
3. **ESP32 → M25**: Sends AES-encrypted commands to wheels via BLE
4. **HTML served from SPIFFS**: External file for easy editing

## Joystick Coordinate System
```
        Y+ (Forward)
         |
    -X ←─┼─→ +X
         |
        Y- (Backward)
```

- **X axis**: -1.0 (left) to +1.0 (right)
- **Y axis**: -1.0 (back) to +1.0 (forward)
- **Deadman**: Active only when touching joystick

## Features
### Implemented
- WiFi AP mode with web server
- Touch-optimized HTML5 joystick (external file)
- Real-time WebSocket communication
- BLE client connection to M25 wheels
- Automatic BLE scanning and connection
- AES-128 encryption for wheel commands
- BLE status indicator on web interface
- Automatic reconnection on BLE disconnect
- Deadman switch (release to stop)
- Emergency stop button
- SPIFFS file system for HTML

### TODO
- M25 protocol encoding refinement
- Battery level display
- Assist level control
- Hill hold toggle
- Settings page
- Connect to both wheels simultaneously
- Response handling from wheels

## Troubleshooting
### Can't find WiFi network
- Check ESP32 is powered on (LED indicator)
- Look for network "M25-Controller"
- Try restarting ESP32
- Check serial monitor for errors

### Can't connect to http://192.168.4.1
- Ensure phone is connected to ESP32's WiFi
- Try http://192.168.4.1/ with trailing slash
- Clear browser cache
- Disable mobile data (force WiFi usage)

### Joystick not responding
- Check WebSocket connection status at top
- Should show "Connected" in green
- Try refreshing the page
- Check serial monitor for errors

### ESP32 keeps restarting
- Watchdog timer triggered
- Check power supply (needs stable 5V)
- Reduce WebSocket update rate
- BLE scan may be blocking - check serial output

### HTML not loading
- Ensure data folder was uploaded using ESP32 Sketch Data Upload
- Check SPIFFS mounted successfully in serial monitor
- Try uploading data folder again

### BLE won't connect
- Verify wheel MAC address is correct
- Check wheel is powered on and advertising
- Ensure encryption key matches wheel
- Check serial monitor for connection errors

### HTML editing
Since HTML is now external, you can edit `data/index.html` directly:
1. Edit the HTML file
2. Upload data folder again (ESP32 Sketch Data Upload)
3. Refresh browser

## Advanced Usage
### Edit HTML Interface
The HTML file is in `data/index.html` - edit it directly:

1. Make changes to `data/index.html`
2. Upload data folder: **Tools → ESP32 Sketch Data Upload**
3. Refresh browser (Ctrl+F5 to clear cache)

This makes it much easier to customize the interface!

### Add Multiple Joysticks
Modify WebSocket handler to support multiple clients:

```cpp
// Track multiple clients
std::map<uint8_t, JoystickState> clients;

void webSocketEvent(uint8_t num, WStype_t type, ...) {
    // Store per-client joystick state
    clients[num] = joystick;
}
```

### HTTPS/SSL Support
For secure connections, use `ESPAsyncWebServer` with SSL:

```cpp
#include <ESPAsyncWebServer.h>
AsyncWebServer server(443);  // HTTPS port
// Add SSL certificate
```

## Integration Notes

The M25 protocol packet structure is simplified in this implementation. You may need to adjust the `sendWheelCommand()` function based on actual protocol requirements from your Python implementation.

Current packet structure:
```cpp
plainPacket[0] = 0x01;  // Command ID
plainPacket[1] = leftSpeed & 0xFF;
plainPacket[2] = (leftSpeed >> 8) & 0xFF;
plainPacket[3] = rightSpeed & 0xFF;
plainPacket[4] = (rightSpeed >> 8) & 0xFF;
// Bytes 5-15 = padding
```

Refer to `m25_protocol.py` for the complete protocol specification.

## Performance
- **Latency**: ~30-50ms (WiFi + WebSocket)
- **BLE update rate**: 20Hz (50ms interval)
- **Concurrent users**: 1-4 (depends on ESP32 model)
- **WiFi range**: ~10-30m (typical AP range)
- **BLE range**: ~5-10m to wheelchair

## Safety Notes
- **Always test in safe environment** before real use
- **Keep emergency stop accessible** at all times
- **Deadman switch prevents runaway** (release stops)
- **BLE disconnection stops commands** automatically
- **Consider hardware e-stop** on wheelchair for ultimate safety
- **Monitor connection status** - both WiFi and BLE must be active

## See Also
- [ESP32 Setup Guide](../../../doc/esp32-setup.md)
- [M25 Protocol](../../../doc/m25-protocol.md)
- [Fake M25 Wheel](../fake_m25_wheel/) - BLE testing
- [Device Config](device_config.h) - Configuration template
