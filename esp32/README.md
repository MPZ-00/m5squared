# ESP32 M5Squared Implementation
MicroPython port of M25 control for embedded ESP32 hardware.

## Structure
```
esp32/
├── README.md                # This file
├── micropython/             # MicroPython implementation
│   ├── main.py              # Main control loop
│   ├── m25_ble.py           # BLE connection handler
│   ├── m25_crypto.py        # AES encryption (ported)
│   ├── config.py            # Configuration
│   └── README.md            # MicroPython details
├── arduino/                 # Arduino C++ examples
│   ├── wifi_joystick/       # Web-based WiFi controller
│   └── fake_m25_wheel/      # BLE wheel simulator
└── examples/
    └── basic_joystick.py    # Simple analog joystick (WIP)
```

## MicroPython (Primary)

Portable ESP32 controller with minimal dependencies. See [micropython/README.md](micropython/README.md).

**Status**: Core modules ported, BLE integration in progress

## Arduino (Reference)

C++ implementations for testing and alternative approaches. The wifi_joystick provides a web-based interface but requires maintaining separate codebase.


## Hardware Setup
### Minimal (Joystick Control)
```
ESP32          Joystick Module
GPIO 34   <--- VRX (X-axis)
GPIO 35   <--- VRY (Y-axis)  
GPIO 25   <--- SW (deadman button)
3.3V      ---> VCC
GND       ---> GND
```

### With Display (T-Display, M5Stack)
Use ESP32 with built-in display for status feedback without phone/PC.

## See Also
- [MicroPython Implementation](micropython/README.md)
- [ESP32 Setup Guide](../doc/esp32-setup.md)
- [M25 Protocol](../doc/m25-protocol.md)
