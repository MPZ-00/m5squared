# Cross-Platform BLE Communication

## Overview

The `m25_bluetooth_ble.py` module provides Bluetooth Low Energy (BLE) communication for M25 wheelchair wheels on **Windows, Linux, and macOS** using the Bleak library. Bleak has native support for all major operating systems.

This replaces platform-specific implementations:
- **Windows**: Replaces RFCOMM (fixes auto-disconnect issues)
- **Linux**: Alternative to PyBluez RFCOMM (when BLE devices available)
- **macOS**: Native BLE support

## Why BLE Instead of RFCOMM?

**RFCOMM/Bluetooth Classic Issues:**
- Windows: Auto-disconnects after Bluetooth pairing
- Windows: Cannot maintain simultaneous connections to both wheels
- Linux: PyBluez dependencies can be complex
- Cross-platform inconsistencies

**BLE Advantages:**
- **Cross-platform**: Single codebase works on Windows, Linux, macOS
- **Native support**: Bleak uses platform-native BLE APIs
- **Stable connections**: No auto-disconnect issues
- **Multiple devices**: Can connect to both wheels simultaneously
- **No pairing required**: Direct BLE connection
- **Power-efficient notifications**: Device pushes data instead of polling

## Power Efficiency - Notifications vs Polling

**Critical for battery life:** The M25 wheels run on batteries (Lithium-ion 10ICR19/66-2, 36.5V), so minimizing power consumption is important.

### Polling (Old Way - Less Efficient)

```python
# Client constantly reads - device must stay awake
while True:
    data = await bt.receive_packet()  # Device wakes up for EVERY read
    await asyncio.sleep(0.1)
```

**Impact:** Device must wake from sleep for every read request, even when it has no data to send. The BLE radio remains more active, consuming more battery power.

### Notifications (New Way - More Efficient)

```python
# Device pushes data only when needed - sleeps otherwise
await bt.start_notifications(callback)  # Device wakes ONLY to send data
```

**Impact:** Device sleeps deeply between events and only wakes its BLE radio when it actually has data to send. This significantly reduces battery consumption and extends operating time.

**Result:** Longer wheelchair operation time between charges, preserving battery for the 280W drive motor rather than communication overhead.

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
│ Application Layer                                           │
│ (demos/demo_gamepad_live.py, etc.)                          │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Transport Layer                                             │
│ (core/transport/bluetooth.py)                               │
│ - Platform detection (Windows vs Linux)                     │
│ - Encrypts data before sending                              │
└──────────────────────┬──────────────────────────────────────┘
                       │
         ┌─────────────┴─────────────┐
         │                           │
┌────────▼──────────┐    ┌───────────▼─────────┐
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
from m25_bluetooth_ble import M25BluetoothBLE

# Create instance
bt = M25BluetoothBLE(
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
from m25_bluetooth_ble import M25BluetoothBLE

# Create instance with encryption key
bt = M25BluetoothBLE(
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
from m25_bluetooth_ble import M25BluetoothBLE

bt = M25BluetoothBLE(
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
from m25_bluetooth_ble import M25BluetoothBLE

bt = M25BluetoothBLE(debug=True)

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
from m25_bluetooth_ble import scan_devices, connect_device

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

### `M25BluetoothBLE`

#### Constructor

```python
M25BluetoothBLE(
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
python m25_bluetooth_ble.py scan

# Scan for M25 wheels only
python m25_bluetooth_ble.py scan --m25 --duration 20

# Connect to device
python m25_bluetooth_ble.py connect AA:BB:CC:DD:EE:FF

# Disconnect
python m25_bluetooth_ble.py disconnect

# Check status
python m25_bluetooth_ble.py status
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

### Platform-Specific Notes

**Windows:**
- Requires Windows 10 version 16299 (Fall Creators Update) or later
- Built-in Bluetooth support (no drivers needed)

**Linux:**
- Requires BlueZ (usually pre-installed)
- May need to run with elevated privileges or add user to `bluetooth` group:
  ```bash
  sudo usermod -a -G bluetooth $USER
  ```

**macOS:**
- Requires macOS 10.13 (High Sierra) or later
- Built-in Bluetooth support (no setup needed)

## See Also

- [M25 Protocol Documentation](m25-protocol.md)
- [Windows Setup Guide](windows-setup.md)
- [Usage and Setup](usage-setup.md)

## Migration from Old Modules

If you're using `m25_bluetooth_windows.py` or platform-specific RFCOMM:

```python
# Old (Windows-only)
from m25_bluetooth_windows import M25WindowsBluetooth
bt = M25WindowsBluetooth(...)

# New (cross-platform)
from m25_bluetooth_ble import M25BluetoothBLE
bt = M25BluetoothBLE(...)
```

The API is identical - just change the import and class name.
