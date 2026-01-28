# WiFi Virtual Joystick Controller
Web-based touchscreen joystick for controlling M25 wheels via ESP32.

## Overview
This solution uses WiFi instead of BLE for phone connectivity, making it easier to implement and use:

- **ESP32 creates WiFi access point** - No router needed
- **Web-based interface** - No app installation required  
- **Touch-optimized joystick** - Natural touchscreen control
- **Real-time WebSocket** - Low latency communication
- **Works on any device** - Phone, tablet, laptop

## Hardware Required
- ESP32-WROOM-32 or compatible board
- USB cable for programming
- (Optional) Power bank for portable operation

## Quick Start
### 1. Install Required Libraries
Open Arduino IDE and install these libraries via Library Manager:

- **WebSockets** by Markus Sattler

### 2. Upload Sketch
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

## Configuration
### Change WiFi Credentials
Edit in `wifi_joystick.ino`:

```cpp
const char* ssid = "M25-Controller";      // Network name
const char* password = "m25wheel";        // Password (min 8 chars)
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
│  (Browser)  │   WebSocket   │             │   Commands   │  Wheels  │
└─────────────┘   Joystick    └─────────────┘              └──────────┘
                   Data
```

1. **Phone → ESP32**: Touch joystick sends X/Y coordinates via WebSocket
2. **ESP32 Processing**: Converts joystick to differential wheel speeds
3. **ESP32 → M25**: Sends encrypted commands to wheels via BLE

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
- Touch-optimized HTML5 joystick
- Real-time WebSocket communication
- Deadman switch (release to stop)
- Emergency stop button
- Visual feedback (connection status)
- Works on any device with browser

### TODO
- BLE connection to actual M25 wheels
- M25 protocol encoding with encryption
- Battery level display
- Assist level control
- Hill hold toggle
- Settings page
- Multi-client support (multiple controllers)

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
- Add `delay(1)` in loop if missing

## Advanced Usage
### Custom HTML Interface
The HTML page is embedded in the sketch. To modify:

1. Edit the `htmlPage` variable
2. Maintain the WebSocket JavaScript code
3. Test in browser first (separate HTML file)
4. Copy back to sketch as raw string

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

## Integration with M25 Protocol
To connect to real M25 wheels, integrate the Python protocol:

1. **Copy encryption key** from your QR code
2. **Implement AES encryption** (mbedtls library)
3. **Encode M25 protocol packets** (see `m25_protocol.py`)
4. **Add BLE client code** to connect to wheels

See [../fake_m25_wheel/fake_m25_wheel.ino](../fake_m25_wheel/fake_m25_wheel.ino) for BLE examples.

## Performance
- **Latency**: ~30-50ms (WiFi + WebSocket)
- **Update rate**: 20Hz (configurable)
- **Concurrent users**: 1-4 (depends on ESP32 model)
- **Range**: ~10-30m (typical WiFi AP range)

## Safety Notes
- **Always test in safe environment** before real use
- **Keep emergency stop accessible** at all times
- **Deadman switch prevents runaway** (release stops)
- **Consider hardware e-stop** on wheelchair for ultimate safety
- **Monitor for WiFi disconnections** - implement auto-stop on loss

## See Also
- [ESP32 Setup Guide](../../../doc/esp32-setup.md)
- [M25 Protocol](../../../doc/m25-protocol.md)
- [Fake M25 Wheel](../fake_m25_wheel/) - BLE testing
