# BLE Communication on Windows

## Overview

The `m25_bluetooth_windows.py` module provides Bluetooth Low Energy (BLE) communication for M25 wheelchair wheels on Windows using the Bleak library. It replaces traditional RFCOMM (Bluetooth Classic) which has reliability issues on Windows, including automatic disconnection after pairing and inability to maintain connections to both wheels simultaneously.

## Why BLE Instead of RFCOMM?

**Windows RFCOMM Issues:**
- Auto-disconnects after Bluetooth pairing completes
- Cannot maintain simultaneous connections to both wheels
- Unreliable connection management in PyBluez

**BLE Advantages:**
- Native Windows support through Bleak library
- Stable connections without auto-disconnect
- Can connect to multiple devices simultaneously
- Cross-platform (Windows, Linux, macOS)
- No pairing required
- **Power-efficient notifications** (device pushes data instead of polling)

## Power Efficiency - Notifications vs Polling

**Critical for battery life:** The M25 wheels run on batteries, so power consumption matters.

### Polling (Old Way - Bad for Battery)

```python
# Client constantly reads - device must stay awake
while True:
    data = await bt.receive_packet()  # Device wakes up for EVERY read
    await asyncio.sleep(0.1)
```

**Power consumption:** ~2-5 mA continuous drain on wheel battery

### Notifications (New Way - Battery Friendly)

```python
# Device pushes data only when needed - sleeps otherwise
await bt.start_notifications(callback)  # Device wakes ONLY to send data
```

**Power consumption:** ~0.1-0.5 mA (10-50x more efficient!)

This can extend wheelchair battery life by hours during operation.

## Nordic UART Service (NUS)

The M25 wheels use Nordic UART Service, a proprietary BLE service created by Nordic Semiconductor that emulates a traditional serial UART connection over BLE.

### Service and Characteristics

| Component | UUID | Direction | Property | Purpose |
|-----------|------|-----------|----------|---------|
| **NUS Service** | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | - | - | Main service container |
| **TX Characteristic** | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | Client → Device | Write | Send data to wheel |
| **RX Characteristic** | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | Device → Client | Notify | Receive data from wheel |

### Naming Convention

- **TX** = Transmit from client perspective (write to device)
- **RX** = Receive from client perspective (notify from device)

From the device's perspective, these are reversed (its RX is our TX).

### Characteristic Discovery

The module implements a two-stage discovery process:

1. **Nordic UART Detection**: Searches for the official NUS service and characteristics by UUID
2. **Generic Fallback**: If NUS not found, searches for any characteristics with:
   - Write or Write-Without-Response properties (for TX)
   - Notify or Read properties (for RX)

This ensures compatibility with both standard NUS implementations and devices using custom UUIDs.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer                                            │
│ (demos/demo_gamepad_live.py, etc.)                          │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Transport Layer                                              │
│ (core/transport/bluetooth.py)                               │
│ - Platform detection (Windows vs Linux)                     │
│ - Encrypts data before sending                              │
└──────────────────────┬──────────────────────────────────────┘
                       │
         ┌─────────────┴─────────────┐
         │                           │
┌────────▼──────────┐    ┌──────────▼──────────┐
│ Windows (BLE)     │    │ Linux (RFCOMM)      │
│ m25_bluetooth_    │    │ m25_spp.py          │
│   windows.py      │    │                     │
│ - Bleak library   │    │ - PyBluez           │
│ - Nordic UART     │    │ - Serial Port       │
│ - send_async()    │    │                     │
└───────────────────┘    └─────────────────────┘
```

## Encryption

The module integrates M25Encryptor/M25Decryptor for secure communication:

- **Constructor parameter**: `key` (16 bytes)
- **Auto-encryption**: `send_packet()` encrypts data if key provided
- **Manual bypass**: `send_async()` sends pre-encrypted/raw data
- **Auto-decryption**: `receive_packet()` decrypts data if key provided

### Two Send Methods

| Method | Use Case | Encryption | Used By |
|--------|----------|------------|---------|
| `send_packet(data)` | Direct usage, testing | Auto-encrypts if key set | CLI tools, manual tests |
| `send_async(encrypted_data)` | Transport layer | Assumes pre-encrypted | BluetoothTransport |

The transport layer handles encryption externally and uses `send_async()` to avoid double-encryption.

## Usage Examples

### Basic Connection (No Encryption)

```python
from m25_bluetooth_windows import M25WindowsBluetooth

# Create instance
bt = M25WindowsBluetooth(
    address="AA:BB:CC:DD:EE:FF",
    name="left_wheel",
    debug=True
)

# Connect
await bt.connect()

# Send raw data
await bt.send_async(b'\x01\x02\x03')

# Disconnect
await bt.disconnect()
```

### Connection with Encryption

```python
from m25_bluetooth_windows import M25WindowsBluetooth

# Create instance with encryption key
bt = M25WindowsBluetooth(
    address="AA:BB:CC:DD:EE:FF",
    key=b"16-byte-key-here",
    name="left_wheel",
    debug=True
)

# Connect
await bt.connect()

# Send unencrypted data (will be encrypted automatically)
await bt.send_packet(b'\x01\x02\x03')

# Receive data (will be decrypted automatically)
data = await bt.receive_packet()

# Disconnect
await bt.disconnect()
```

### Power-Efficient Notifications (Recommended)

Instead of polling, use notifications to save battery power:

```python
from m25_bluetooth_windows import M25WindowsBluetooth

bt = M25WindowsBluetooth(
    address="AA:BB:CC:DD:EE:FF",
    key=b"16-byte-key-here",
    name="left_wheel",
    debug=True
)

await bt.connect()

# Option 1: Callback (push model)
def handle_data(data: bytes):
    print(f"Wheel sent: {data.hex()}")

await bt.start_notifications(handle_data)
# Device now pushes data when available (power-efficient!)
await asyncio.sleep(60)  # Listen for 60 seconds

# Option 2: Queue (pull model)
await bt.start_notifications()  # No callback = uses queue
while True:
    data = await bt.wait_notification(timeout=5.0)
    if data:
        print(f"Got: {data.hex()}")
    else:
        print("No data (device sleeping)")

# Cleanup
await bt.stop_notifications()
await bt.disconnect()
```

### Scanning for Devices

```python
from m25_bluetooth_windows import M25WindowsBluetooth

bt = M25WindowsBluetooth(debug=True)

# Scan for all BLE devices (10 seconds)
devices = await bt.scan(duration=10)

# Scan for M25 wheels only
devices = await bt.scan(duration=10, filter_m25=True)

# Results: [(address, name), ...]
for addr, name in devices:
    print(f"{addr}: {name}")
```

### Synchronous Wrappers

For backward compatibility and simpler code:

```python
from m25_bluetooth_windows import scan_devices, connect_device

# Scan (blocks until complete)
devices = scan_devices(duration=10, filter_m25=True)

# Connect (blocks until connected)
bt = connect_device(
    address="AA:BB:CC:DD:EE:FF",
    key=b"16-byte-key-here"
)

if bt:
    print("Connected!")
```

## Class Reference

### `M25WindowsBluetooth`

#### Constructor

```python
M25WindowsBluetooth(
    address: str = None,
    key: bytes = None,
    name: str = "wheel",
    debug: bool = False
)
```

**Parameters:**
- `address`: Bluetooth MAC address (can be set later)
- WARNING:** Uses polling which drains battery. Prefer `start_notifications()` for production.

**Returns:** Decrypted bytes or None

##### `async start_notifications(callback = None) -> bool`

Enable BLE notifications for power-efficient data reception.

**Parameters:**
- `callback`: Optional function to handle incoming data. If None, data goes to queue.

**Returns:** True if successful

**Power savings:** 10-50x more efficient than polling!

##### `async stop_notifications() -> bool`

Disable BLE notifications.

**Returns:** True if successful

##### `async wait_notification(timeout: float = None) -> Optional[bytes]`

Wait for notification data from queue (use after `start_notifications()` without callback).

**Parameters:**
- `timeout`: Wait timeout in seconds (None = wait forever)

**Returns:** Received bytes or None on timeoutional, enables encryption)
- `name`: Friendly name for logging (e.g., "left_wheel", "right_wheel")
- `debug`: Enable verbose debug output

#### Methods

##### `async scan(duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]`

Scan for BLE devices.

**Returns:** List of (address, name) tuples

##### `async connect(address: str = None, timeout: int = 10) -> bool`

Connect to device and discover characteristics.

**Returns:** True if successful

##### `async disconnect()`

Disconnect from device.

##### `async send_packet(data: bytes) -> bool`

Send data with automatic encryption (if key provided).

**Use for:** Manual testing, CLI tools

##### `async send_async(encrypted_data: bytes) -> bool`

Send pre-encrypted or raw data without additional encryption.

**Use for:** Transport layer integration

##### `async receive_packet(timeout: int = 5) -> Optional[bytes]`

Receive data with automatic decryption (if key provided).

**Returns:** Decrypted bytes or None

##### `is_connected() -> bool`

Check if currently connected.

##### `async connect_async(timeout: int = 10) -> bool`

Alias for `connect()` (compatibility).

##### `async disconnect_async()`

Alias for `disconnect()` (compatibility).

## Integration with Transport Layer

The `core/transport/bluetooth.py` module uses platform detection:

```python
import sys

if sys.platform == 'win32':
    from m25_bluetooth_windows import M25WindowsBluetooth as BluetoothImpl
else:
    from m25_spp import BluetoothConnection as BluetoothImpl
```

The transport layer:
1. Creates connection with encryption key
2. Encrypts packets using M25Encryptor
3. Sends encrypted data via `send_async()` (no double-encryption)
4. Manages async event loop properly

## Debugging

Enable debug output to see:
- Connection status
- Characteristic discovery (Nordic UART vs fallback)
- Encryption status (enabled/disabled)
- Sent/received byte counts
- Errors and warnings

```python
bt = M25WindowsBluetooth(address="...", debug=True)
```

**Example output:**
```
[left_wheel] Connecting to AA:BB:CC:DD:EE:FF...
[left_wheel] Connected, discovering characteristics...
[left_wheel] TX: 6e400002-b5a3-f393-e0a9-e50e24dcca9e
[left_wheel] RX: 6e400003-b5a3-f393-e0a9-e50e24dcca9e
[left_wheel] Ready (encryption=enabled)
[left_wheel] Sent 16 bytes (encrypted=True)
[left_wheel] Received 8 bytes (decrypted=True)
[left_wheel] Disconnected
```

## Troubleshooting

### "No TX characteristic found"

- Device may not support Nordic UART Service
- Check device is powered on and advertising
- Try generic characteristic fallback

### "Not connected or no TX char"

- Ensure `connect()` completed successfully
- Check `is_connected()` returns True
- Verify characteristics were discovered

### Connection timeout

- Increase timeout: `connect(timeout=30)`
- Check device is in range and powered on
- Ensure no other app is connected to device

### Encryption errors

- Verify key is exactly 16 bytes
- Check key matches device configuration
- Test without encryption first to isolate issue

## Command-Line Interface

The module includes a CLI for testing:

```bash
# Scan for all devices
python m25_bluetooth_windows.py scan

# Scan for M25 wheels only
python m25_bluetooth_windows.py scan --m25 --duration 20

# Connect to device
python m25_bluetooth_windows.py connect AA:BB:CC:DD:EE:FF

# Disconnect
python m25_bluetooth_windows.py disconnect

# Check status
python m25_bluetooth_windows.py status
```

## Dependencies

- **bleak** (v0.19.0+): Cross-platform BLE library
- **m25_crypto**: Encryption/decryption (M25Encryptor, M25Decryptor)
- **m25_protocol**: Protocol utilities (calculate_crc, remove_delimiters)
- **m25_utils**: Helper functions (parse_hex)

Install dependencies:
```bash
pip install bleak
```

## See Also

- [M25 Protocol Documentation](m25-protocol.md)
- [Windows Setup Guide](windows-setup.md)
- [Usage and Setup](usage-setup.md)
