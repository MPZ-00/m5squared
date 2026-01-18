# Mock M25 Wheel Simulator
A software simulator that emulates an M25 wheelchair wheel for testing purposes. This allows you to develop and test applications without needing the physical hardware.

## Features
- Full AES-128-CBC encryption/decryption support
- Responds to M25 protocol commands (APP_MGMT service)
- Simulates wheel state (battery, speed, drive modes, etc.)
- TCP socket server mode for easy testing
- Configurable encryption keys
- Debug logging for troubleshooting

## Quick Start
### 1. Start the Simulator
```bash
# Using default settings (USB default key, socket mode on port 5000)
python mock_wheel_simulator.py

# With custom encryption key
python mock_wheel_simulator.py --key 416c6265725f4d32355f656d6f74696f

# On a different port
python mock_wheel_simulator.py --port 8000

# Enable debug logging
python mock_wheel_simulator.py --debug
```

### 2. Run Test Client
In a separate terminal:

```bash
python test_mock_wheel.py
```

This will run a series of test commands and verify the simulator is working correctly.

## Command Line Options
```
--key KEY          Encryption key (32 hex chars = 16 bytes)
                   Default: Alber M25 USB default key

--wheel-id ID      Wheel ID: 1=COMMON, 2=LEFT, 3=RIGHT
                   Default: 2 (LEFT)

--mode MODE        Communication mode: socket or bluetooth
                   Default: socket (bluetooth not yet implemented)

--port PORT        TCP port for socket mode
                   Default: 5000

--debug            Enable debug logging
```

## Supported Commands
The simulator currently supports these M25 protocol commands:

### System Management (SERVICE_ID_APP_MGMT = 1)
- **WRITE_SYSTEM_MODE** (0x10): Set system mode (Connect/Standby)
- **READ_SYSTEM_MODE** (0x11): Get current system mode
- **WRITE_DRIVE_MODE** (0x20): Set drive mode (Normal/Sport/etc.)
- **READ_DRIVE_MODE** (0x21): Get current drive mode
- **WRITE_REMOTE_SPEED** (0x30): Set target motor speed
- **READ_CURRENT_SPEED** (0x91): Get current motor speed
- **WRITE_ASSIST_LEVEL** (0x40): Set assist level (1-10)
- **READ_ASSIST_LEVEL** (0x41): Get current assist level

### Response Behavior
- Valid commands receive either **ACK** (0xFF) or a **STATUS** response with data
- Unknown commands receive **NACK** with error code
- All responses are properly encrypted

## Integration with Existing Code
You can use the simulator with your existing M25 code by connecting to it as a socket instead of Bluetooth:

```python
import socket
from m25_crypto import M25Encryptor, M25Decryptor

# Create socket connection to simulator
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 5000))

# Use existing encryption/decryption classes
encryptor = M25Encryptor(key)
decryptor = M25Decryptor(key)

# Send encrypted packet
spp_packet = build_your_packet()
encrypted = encryptor.encrypt(spp_packet)
sock.send(encrypted)

# Receive encrypted response
response = sock.recv(1024)
decrypted = decryptor.decrypt(response)
```

## Simulated State
The simulator maintains realistic state:

### Battery
- State of Charge: 85%
- Voltage: 36.5V
- Temperature: 25°C

### Motor
- Current speed (updated by WRITE_REMOTE_SPEED)
- Target speed
- Temperature: 30°C

### Drive Settings
- System Mode: 0x01 (Connected)
- Drive Mode: 0x01 (Normal)
- Assist Level: 5

### Statistics
- Odometer: 12,345m
- Trip distance: 1,234m
- Operating hours: 450h

## Testing Your Application
1. **Start the simulator** in one terminal
2. **Run your application** configured to connect to `127.0.0.1:5000`
3. **Monitor simulator logs** to see received commands
4. **Use debug mode** (`--debug`) to see detailed packet information

## Example: Testing with Existing m25_spp.py
Modify your existing code to use a TCP socket instead of Bluetooth:

```python
# Instead of:
# conn = BluetoothConnection(address, key)
# conn.connect()
# Use:
import socket
from m25_crypto import M25Encryptor, M25Decryptor

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 5000))
encryptor = M25Encryptor(key)
decryptor = M25Decryptor(key)

# Rest of your code stays the same
```

## Troubleshooting
### Simulator not responding
- Check if simulator is running: `netstat -an | grep 5000` (Linux/Mac) or `netstat -an | findstr 5000` (Windows)
- Verify encryption key matches between simulator and client
- Enable `--debug` mode to see detailed logs

### Decryption failures
- Ensure both simulator and client use the same 16-byte key
- Check that you're sending properly formatted M25 packets
- Verify packet structure (header, length, CRC)

### Connection refused
- Make sure simulator is started before client
- Check firewall settings
- Try different port with `--port` option

## Architecture
```
┌─────────────┐                    ┌──────────────────┐
│             │  Encrypted M25     │                  │
│  Your App   │ ◄───────────────►  │  Mock Simulator  │
│  (Client)   │   TCP Socket       │   (Server)       │
│             │   Port 5000        │                  │
└─────────────┘                    └──────────────────┘
                                            │
                                            ▼
                                   ┌─────────────────┐
                                   │ Simulated State │
                                   │ - Battery: 85%  │
                                   │ - Speed: 0      │
                                   │ - Mode: Connect │
                                   └─────────────────┘
```

## Future Enhancements
- [ ] Bluetooth mode (BLE peripheral emulation)
- [ ] More service handlers (BATT_MGMT, VERSION_MGMT, etc.)
- [ ] Configurable state from config file
- [ ] Realistic motor physics simulation
- [ ] Battery drain simulation
- [ ] Error injection for testing error handling
- [ ] Multiple simultaneous client support
- [ ] Web UI for monitoring state

## See Also
- [m25_protocol.py](m25_protocol.py) - Protocol constants and CRC
- [m25_crypto.py](m25_crypto.py) - Encryption/decryption implementation
- [m25_protocol_data.py](m25_protocol_data.py) - Command IDs and constants
- [m25_spp.py](m25_spp.py) - Real Bluetooth connection implementation

## License
Same license as the rest of the M5Squared project.
