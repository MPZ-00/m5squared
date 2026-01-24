# ESP32 Setup Guide for M5Squared
## Overview
The ESP32 is a powerful microcontroller with built-in Wi-Fi and Bluetooth Low Energy (BLE), making it ideal for running M5squared wheelchair control. This guide covers setting up an ESP32 to control M25 wheels via BLE, with optional Wi-Fi for remote control.

## Why ESP32?
**Advantages over PC-based control:**
- **Portable**: Small form factor, can be integrated into wheelchair
- **Battery-powered**: Low power consumption (can run on power bank)
- **Dual connectivity**: BLE for wheels + Wi-Fi for remote control/monitoring
- **Real-time**: Direct hardware control without OS overhead
- **Cost-effective**: ESP32 boards cost $5-15

**Use cases:**
1. Standalone gamepad controller (no laptop needed)
2. Wi-Fi remote control via smartphone/web interface
3. Wheelchair automation/assistance features
4. Data logging and telemetry

## Hardware Requirements
### ESP32 Board Options
**Recommended boards:**

1. **ESP32-DevKitC** ($8-12)
   - 30 GPIO pins
   - USB-C or Micro-USB
   - Good for prototyping

2. **ESP32-WROOM-32** ($5-10)
   - Standard module
   - Requires external USB adapter for programming

3. **ESP32-S3-DevKitC-1** ($12-18)
   - Newer, faster processor
   - USB OTG support
   - Better BLE performance

4. **LilyGO T-Display ESP32** ($15-20)
   - Built-in 1.14" TFT display
   - Battery management
   - Great for standalone controller

**Minimum specs:**
- ESP32 with BLE 4.2+
- 4MB flash (512KB for code, rest for filesystem)
- 520KB SRAM
- USB port for programming

### Additional Components
**For gamepad input:**
- USB Host Shield (for wired USB controllers)
- OR Bluetooth gamepad (pairs directly with ESP32)
- OR Analog joystick module (2x ADC channels)

**For power:**
- Power bank (5V USB, 2A+)
- OR LiPo battery (3.7V) + charging module
- OR direct 5V supply

**Optional:**
- Breadboard and jumper wires
- Case/enclosure
- Status LEDs
- Emergency stop button

## Software Setup
### Option 1: MicroPython (Recommended)
MicroPython is easier to work with and allows running Python code similar to the PC version.

#### 1. Install Esptool
```bash
pip install esptool
```

#### 2. Download MicroPython Firmware
Get the latest ESP32 firmware from:
https://micropython.org/download/esp32/

For ESP32-S3:
https://micropython.org/download/esp32s3/

```bash
# Download (example - check website for latest)
wget https://micropython.org/resources/firmware/esp32-20231005-v1.21.0.bin
```

#### 3. Erase Flash
```bash
# Windows
python -m esptool --chip esp32 --port COM3 erase_flash

# Linux
python -m esptool --chip esp32 --port /dev/ttyUSB0 erase_flash

# macOS
python -m esptool --chip esp32 --port /dev/cu.usbserial-* erase_flash
```

#### 4. Flash MicroPython
```bash
# Windows
python -m esptool --chip esp32 --port COM3 --baud 460800 write_flash -z 0x1000 esp32-20231005-v1.21.0.bin

# Linux/macOS
python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash -z 0x1000 esp32-20231005-v1.21.0.bin
```

#### 5. Test Connection
```bash
# Install mpremote for file transfer
pip install mpremote

# Test connection
mpremote connect COM3  # Windows
mpremote connect /dev/ttyUSB0  # Linux

# Should get MicroPython REPL prompt
>>> print("Hello ESP32!")
```

### Option 2: Arduino/PlatformIO
For C++ development with more control over hardware.

#### 1. Install PlatformIO
```bash
pip install platformio

# Or use VS Code extension
```

#### 2. Create Project
```bash
mkdir m5squared-esp32
cd m5squared-esp32
pio init --board esp32dev
```

#### 3. Configure platformio.ini
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    NimBLE-Arduino
    AsyncTCP
    ESPAsyncWebServer
monitor_speed = 115200
```

## Port M5Squared to ESP32
### Architecture
```
┌─────────────────────────────────────────────┐
│ ESP32 Board                                 │
│                                             │
│  ┌─────────────┐      ┌─────────────────┐ │
│  │ Wi-Fi       │◄────►│ Web Server      │ │
│  │ (Optional)  │      │ Remote Control  │ │
│  └─────────────┘      └─────────────────┘ │
│                              │             │
│  ┌─────────────┐      ┌──────▼──────────┐ │
│  │ Input       │─────►│ Control Logic   │ │
│  │ (Gamepad)   │      │ (Supervisor)    │ │
│  └─────────────┘      └──────┬──────────┘ │
│                              │             │
│  ┌─────────────┐      ┌──────▼──────────┐ │
│  │ BLE Radio   │◄────►│ Wheel Manager   │ │
│  │             │      │ (Left + Right)  │ │
│  └─────────────┘      └─────────────────┘ │
│         │                                  │
└─────────┼──────────────────────────────────┘
          │
          ▼
    ┌─────────┐    ┌─────────┐
    │  Left   │    │  Right  │
    │  Wheel  │    │  Wheel  │
    └─────────┘    └─────────┘
```

### Core Files to Port
**Essential modules:**
1. `m25_crypto.py` - Encryption/decryption (port to MicroPython)
2. `m25_protocol.py` - Protocol handling
3. `m25_bluetooth_ble.py` - BLE communication (adapt for ESP32 BLE)
4. `core/supervisor.py` - Control logic
5. `core/mapper.py` - Input mapping

**MicroPython differences:**
- No `asyncio.Queue` - use `uasyncio`
- BLE handled by `ubluetooth` module
- Limited standard library

### Example MicroPython Code
#### 1. BLE Connection (ESP32)
```python
# esp32_ble.py
import ubluetooth
import struct
from micropython import const

_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)

class ESP32BLE:
    def __init__(self, name="M5Squared"):
        self._ble = ubluetooth.BLE()
        self._ble.active(True)
        self._ble.irq(self._irq)
        self._connections = set()
        self._payload = b''
        
    def _irq(self, event, data):
        if event == _IRQ_CENTRAL_CONNECT:
            conn_handle, _, _ = data
            self._connections.add(conn_handle)
            print(f"Connected: {conn_handle}")
            
        elif event == _IRQ_CENTRAL_DISCONNECT:
            conn_handle, _, _ = data
            self._connections.discard(conn_handle)
            print(f"Disconnected: {conn_handle}")
            
    async def connect_wheel(self, address):
        # Scan and connect to M25 wheel
        # Implementation depends on wheel's BLE advertising
        pass
        
    async def send_packet(self, data):
        # Send encrypted command to wheel
        # Use ubluetooth to write to TX characteristic
        pass
```

#### 2. Input Handling
```python
# esp32_input.py
from machine import Pin, ADC
import uasyncio as asyncio

class GamepadInput:
    def __init__(self):
        # Analog joystick on ADC pins
        self.x_axis = ADC(Pin(34))  # GPIO 34
        self.y_axis = ADC(Pin(35))  # GPIO 35
        self.x_axis.atten(ADC.ATTN_11DB)  # 0-3.3V range
        self.y_axis.atten(ADC.ATTN_11DB)
        
        # Deadman button
        self.deadman = Pin(25, Pin.IN, Pin.PULL_UP)
        
    def read(self):
        # Read joystick (0-4095)
        x = self.x_axis.read()
        y = self.y_axis.read()
        
        # Convert to -1.0 to 1.0
        vx = (x - 2048) / 2048.0
        vy = (y - 2048) / 2048.0
        
        # Deadman (active low)
        deadman = not self.deadman.value()
        
        return vx, vy, deadman
```

#### 3. Main Loop
```python
# main.py
import uasyncio as asyncio
from esp32_ble import ESP32BLE
from esp32_input import GamepadInput
from m25_crypto import M25Encryptor
from m25_protocol import build_drive_packet

async def main():
    # Initialize
    ble = ESP32BLE()
    gamepad = GamepadInput()
    
    # Load encryption keys (from config file)
    left_key = b'...'  # 16 bytes
    right_key = b'...'
    
    left_enc = M25Encryptor(left_key)
    right_enc = M25Encryptor(right_key)
    
    # Connect to wheels
    await ble.connect_wheel("AA:BB:CC:DD:EE:FF")  # Left
    await ble.connect_wheel("AA:BB:CC:DD:EE:FF")  # Right
    
    print("Connected! Ready to drive.")
    
    # Main control loop
    while True:
        vx, vy, deadman = gamepad.read()
        
        if deadman:
            # Build drive packet
            left_speed = int((vy - vx) * 100)  # Differential drive
            right_speed = int((vy + vx) * 100)
            
            left_packet = build_drive_packet(left_speed)
            right_packet = build_drive_packet(right_speed)
            
            # Encrypt and send
            await ble.send_packet(left_enc.encrypt(left_packet))
            await ble.send_packet(right_enc.encrypt(right_packet))
        else:
            # Send stop command
            stop_packet = build_drive_packet(0)
            await ble.send_packet(left_enc.encrypt(stop_packet))
            await ble.send_packet(right_enc.encrypt(stop_packet))
        
        # 20Hz control loop
        await asyncio.sleep_ms(50)

# Run
asyncio.run(main())
```

### Uploading Files to ESP32
```bash
# Copy Python files to ESP32
mpremote connect COM3 fs cp m25_crypto.py :
mpremote connect COM3 fs cp m25_protocol.py :
mpremote connect COM3 fs cp esp32_ble.py :
mpremote connect COM3 fs cp esp32_input.py :
mpremote connect COM3 fs cp main.py :

# Create config file
mpremote connect COM3 fs cp .env :config.txt

# List files
mpremote connect COM3 fs ls

# Run main.py
mpremote connect COM3 run main.py
```

## Wi-Fi Remote Control (Optional)
Add a web server for smartphone/tablet control:

```python
# esp32_web.py
import network
import socket
import json

class WebController:
    def __init__(self):
        self.wlan = network.WLAN(network.STA_IF)
        self.wlan.active(True)
        
    def connect_wifi(self, ssid, password):
        self.wlan.connect(ssid, password)
        while not self.wlan.isconnected():
            pass
        print(f"Connected: {self.wlan.ifconfig()}")
        
    async def serve(self, control_callback):
        addr = socket.getaddrinfo('0.0.0.0', 80)[0][-1]
        s = socket.socket()
        s.bind(addr)
        s.listen(1)
        
        while True:
            cl, addr = s.accept()
            request = cl.recv(1024).decode()
            
            if "POST /drive" in request:
                # Parse JSON body
                body = request.split('\r\n\r\n')[1]
                data = json.loads(body)
                
                vx = data.get('vx', 0)
                vy = data.get('vy', 0)
                
                # Call control callback
                await control_callback(vx, vy)
                
                cl.send('HTTP/1.1 200 OK\r\n\r\n')
            
            cl.close()
```

## Troubleshooting
### ESP32 Not Detected
**Windows:**
- Install CP2102 or CH340 USB driver
- Check Device Manager for COM port
- Try different USB cable (must support data)

**Linux:**
- Add user to dialout group: `sudo usermod -a -G dialout $USER`
- Check permissions: `ls -l /dev/ttyUSB*`
- Install pyserial: `pip install pyserial`

### Flash Failed / Wrong Boot Mode
**Error: "Wrong boot mode detected (0x13)"**

This is the most common ESP32 upload issue. The ESP32 needs to be manually put into bootloader/download mode.

**Solution - Hold BOOT during upload:**

1. **Press and hold** the BOOT button on your ESP32
2. Click **Upload** in Arduino IDE (while still holding BOOT)
3. Keep holding BOOT through the "Connecting......" dots
4. Once you see **"Writing at 0x..."** or upload percentage, **release BOOT**
5. Upload continues automatically

**Timing is key:** Hold BOOT during the connection phase, release once writing starts.

**Alternative method - BOOT + RESET sequence:**

1. Press and hold **BOOT**
2. Press and release **RESET** (while still holding BOOT)
3. Release **BOOT**
4. Click **Upload** immediately

**Identifying the buttons:**

- **BOOT**: Usually labeled BOOT, IO0, or GPIO0
- **RESET**: Usually labeled RST, RESET, or EN
- Both are small tactile switches on the board

**Success indicators:**

```
Connecting....
Connected to ESP32 on COM8:
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Writing at 0x00054070 [==============================] 100.0%
Hash of data verified.
Hard resetting via RTS pin...
```

**If still failing:**

- Try lower baud rate: `--baud 115200`
- Check USB cable quality (must support data transfer)
- Try a different USB port
- Erase flash completely first: `esptool --port COM8 erase_flash`
- Some boards have auto-reset issues - manual BOOT method always works

### BLE Connection Issues
- ESP32 can only handle 3-4 BLE connections simultaneously
- Increase BLE stack size in `sdkconfig` (Arduino/PlatformIO)
- Use NimBLE instead of Bluedroid (more efficient)
- Check M25 wheels are in pairing mode

### Memory Issues
MicroPython has limited RAM (~100KB free after boot):
- Use `gc.collect()` frequently
- Precompile Python to bytecode (`.mpy` files)
- Use `micropython.mem_info()` to check usage
- Consider ESP32-S3 with more RAM (512KB)

## Power Considerations
**ESP32 power consumption:**
- Active (Wi-Fi + BLE): ~160-260mA
- BLE only: ~100-140mA  
- Light sleep: ~0.8mA
- Deep sleep: ~10μA

**Power supply options:**
1. USB power bank (10,000mAh = ~40 hours active)
2. LiPo battery (3.7V, 2000mAh = ~12 hours active)
3. Wheelchair battery (with voltage regulator)

**Power saving:**
- Disable Wi-Fi when not needed
- Use light sleep during idle
- Lower CPU frequency (80MHz instead of 240MHz)
- Turn off unused peripherals

## Next Steps
1. **Get ESP32 board** - Start with ESP32-DevKitC
2. **Flash MicroPython** - Follow steps above
3. **Test BLE** - Scan for M25 wheels
4. **Port core modules** - Start with crypto and protocol
5. **Add input** - Test with analog joystick first
6. **Integrate** - Connect all components
7. **Build enclosure** - 3D print or custom case

## Resources
- **MicroPython ESP32**: https://docs.micropython.org/en/latest/esp32/quickref.html
- **ESP32 BLE**: https://docs.micropython.org/en/latest/library/ubluetooth.html
- **PlatformIO ESP32**: https://docs.platformio.org/en/latest/platforms/espressif32.html
- **NimBLE Arduino**: https://github.com/h2zero/NimBLE-Arduino
- **M25 Protocol**: [m25-protocol.md](m25-protocol.md)
- **BLE Notifications**: [ble-notifications-how-it-works.md](ble-notifications-how-it-works.md)

## Alternative: ESP32 as Wi-Fi Bridge
If you want to keep the PC-based control but add wireless capability:

```
┌──────────┐  Wi-Fi   ┌──────────┐  BLE    ┌─────────┐
│ PC/Phone │◄────────►│  ESP32   │◄───────►│ M25     │
│ (Control)│          │ (Bridge) │         │ Wheels  │
└──────────┘          └──────────┘         └─────────┘
```

The ESP32 acts as a Wi-Fi-to-BLE bridge, allowing remote control from any device while keeping the complex logic on the PC.

## Support
For ESP32-specific questions:
- ESP32 Forum: https://esp32.com
- MicroPython Forum: https://forum.micropython.org
- M5Squared Issues: https://github.com/roll2own/m5squared/issues
