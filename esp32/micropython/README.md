# MicroPython Implementation for M25 Control
Minimal port of m25_gui.py functionality to ESP32 MicroPython.

## Hardware Requirements
- ESP32 with BLE support (ESP32-WROOM-32, ESP32-DevKitC)
- Optional: ESP32 with display (LilyGO T-Display, M5Stack)

## MicroPython Firmware
Flash MicroPython with BLE support:
```bash
esptool.py --chip esp32 --port COM3 erase_flash
esptool.py --chip esp32 --port COM3 write_flash -z 0x1000 esp32-ble-firmware.bin
```

Download firmware: https://micropython.org/download/ESP32_GENERIC/

## File Structure
```
micropython/
├── main.py              # Entry point
├── m25_ble.py           # BLE connection handler
├── m25_crypto.py        # AES encryption (ported from main)
├── m25_protocol.py      # Packet builder (ported from main)
├── web_server.py        # Optional: WiFi web interface
└── config.py            # Keys and MAC addresses
```

## Core Dependencies
MicroPython modules needed:
- `ubluetooth` - Built-in BLE
- `ucryptolib` - AES encryption (built-in)
- `socket` - For web server (built-in)

## Hardware Pin Configuration
### For joystick input (analog):
```python
from machine import ADC, Pin

joy_x = ADC(Pin(34))  # GPIO34 - X axis
joy_y = ADC(Pin(35))  # GPIO35 - Y axis
button = Pin(25, Pin.IN, Pin.PULL_UP)  # GPIO25 - deadman
```

### For display (if using T-Display):
```python
# ST7789 Display
spi = SPI(2, baudrate=40000000, polarity=1, phase=1, sck=Pin(18), mosi=Pin(19))
display = st7789.ST7789(spi, 135, 240, reset=Pin(23), dc=Pin(16), cs=Pin(5))
```

## Memory Constraints
ESP32 has ~120KB free RAM with MicroPython. Optimizations needed:
- Stream encryption (don't buffer full packets)
- Minimal protocol implementation (ECS control only)
- No GUI framework (use web interface or simple display driver)

## Port Status
- [ ] BLE connection via ubluetooth
- [ ] AES encryption with ucryptolib
- [ ] Protocol packet building
- [ ] Basic control loop
- [ ] Web server for remote control
- [ ] Display driver integration

## Porting Notes
Main differences from desktop Python:
- `ubluetooth` uses callbacks, not async/await
- `ucryptolib.aes` API differs from pycryptodome
- No tkinter - use web interface or framebuf for display
- Limited heap - use bytearray() for buffers
- No threading - use async or state machine
