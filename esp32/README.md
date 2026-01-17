# ESP32 M5Squared Implementation
This directory contains ESP32/MicroPython code for running M5squared directly on ESP32 hardware.

## Structure
```
esp32/
├── README.md                # This file
├── config.py                # Configuration template
└── examples/
    └── basic_joystick.py    # Simple analog joystick control (WIP)
```

## Planned Structure
```
esp32/
├── micropython/             # MicroPython implementation (planned)
│   ├── main.py              # Main entry point
│   ├── esp32_ble.py         # BLE connection manager
│   ├── esp32_input.py       # Input handling (joystick/buttons)
│   ├── esp32_web.py         # Wi-Fi web server (optional)
│   └── lib/                 # Ported libraries
│       ├── m25_crypto.py    # Encryption (ported)
│       ├── m25_protocol.py  # Protocol (ported)
│       └── m25_utils.py     # Utilities (ported)
└── arduino/                 # Arduino/PlatformIO (C++) (planned)
```

## Quick Start
**Note: ESP32 implementation is currently in early development.**

### Planned Setup:
1. Flash MicroPython to ESP32 (see [../doc/esp32-setup.md](../doc/esp32-setup.md))
2. Copy files to ESP32:
   ```bash
   mpremote connect COM3 fs cp -r micropython/ :
   ```
3. Configure keys in `config.py`
4. Run: `mpremote connect COM3 run main.py`

### Current Status:
Basic joystick example is in development. Full MicroPython implementation not yet available.

## Hardware Setup
### Minimal Setup (Analog Joystick)
```
ESP32          Joystick Module
------         ----------------
GPIO 34   <--- VRX (X-axis)
GPIO 35   <--- VRY (Y-axis)  
GPIO 25   <--- SW (Button)
3.3V      ---> VCC
GND       ---> GND
```

### Full Setup (with Display)
Use ESP32 board with built-in display (e.g., LilyGO T-Display):
- Display shows connection status
- Display shows battery level
- Display shows current speed

## Status
- [ ] Basic BLE connection
- [ ] Encryption/decryption
- [ ] Protocol encoding
- [ ] Analog joystick input (basic example in progress)
- [ ] Bluetooth gamepad support
- [ ] Wi-Fi web interface
- [ ] Battery monitoring
- [ ] Status display

## Development
Currently in early development phase. A basic analog joystick example is being developed. Core Python modules need to be ported to work with MicroPython's limited standard library.

## See Also
- [ESP32 Setup Guide](../doc/esp32-setup.md)
- [BLE How It Works](../doc/ble-notifications-how-it-works.md)
- [M25 Protocol](../doc/m25-protocol.md)
