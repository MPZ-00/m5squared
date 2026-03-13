# m25_wheel_rfcomm - Fake M25 Wheel (RFCOMM / SPP)
A modular ESP32 simulator for the Alber e-motion M25 wheelchair wheel.

This is a clean rewrite of `fake_m25_wheel` with the following goals:
- **RFCOMM (Bluetooth Classic SPP) as the primary transport** - this matches
  `m25_spp.py` and how the real wheels communicate.
- **One function, one task** - every function has a single clear responsibility.
- **No hidden globals** - state is passed explicitly; each module exposes a
  minimal public API.
- **Optional BLE** - BLE GATT can be enabled alongside RFCOMM via `config.h`
  while BLE support on real wheels is confirmed.

---

## File structure
| File | Responsibility |
|------|----------------|
| `config.h` | **All** compile-time settings (name, key, pins, timeouts, feature flags) |
| `state.h` | `WheelState` struct + pure mutation helpers |
| `protocol.h` | CRC-16, byte stuffing / unstuffing, frame encode / decode |
| `crypto.h` | AES-128 ECB / CBC wrappers (mbedtls) |
| `packet.h` | Full M25 packet decode (incoming) and ACK encode (outgoing) |
| `command.h` | Map SPP service/param IDs to `WheelState` updates |
| `transport_rfcomm.h` | Bluetooth Classic SPP via `BluetoothSerial` |
| `transport_ble.h` | BLE GATT (optional, controlled by `TRANSPORT_BLE_ENABLED`) |
| `led.h` | LED visual feedback (battery level, connection, speed blink) |
| `buzzer.h` | Active and passive buzzer audio feedback |
| `cli.h` | Serial command-line interface; delegates transport actions via callbacks |
| `safety.h` | Command timeout, battery drain timer, button debounce |
| `m25_wheel_rfcomm.ino` | `setup()` + `loop()` only; wires all modules together |

---

## Quick start
### 1. Configure `config.h`
```cpp
// Set the compiled fallback side used only when NVS is empty
#define WHEEL_SIDE_LEFT   // or WHEEL_SIDE_RIGHT

// Set both wheel keys here, or place M25_LEFT_KEY / M25_RIGHT_KEY in a local
// .env file next to platformio.ini and let load_env.py inject them.
#define ENCRYPTION_KEY_LEFT  { 0xAA, 0xBB, ... }
#define ENCRYPTION_KEY_RIGHT { 0xCC, 0xDD, ... }
```

### 2. Set upload port in `platformio.ini`
```ini
upload_port = COM8   ; change to your actual port
```

### 3. Build and upload
```
pio run -t upload
pio device monitor
```

### 4. Set wheel side once
```
pio device monitor
config set left
```

Use `config set right` for the other wheel. The setting is stored in ESP32 NVS
and survives reboot and re-upload until changed or cleared with `config reset`.

### 5. Connect from Python
```python
from m25_bluetooth import BluetoothConnection

conn = BluetoothConnection(address="XX:XX:XX:XX:XX:XX", key=your_key)
conn.connect(channel=6)   # see RFCOMM channel note below
```

---

## RFCOMM channel note
The real M25 wheel advertises its SPP service on **RFCOMM channel 6**.
`m25_spp.py` hard-codes `channel=6` when connecting.

`BluetoothSerial` assigns channels automatically via the ESP32 Bluetooth
stack. The actual channel used appears in the Serial Monitor at startup:

```
[RFCOMM] NOTE: Check actual RFCOMM channel in BT stack output.
[RFCOMM] m25_spp.py expects channel 6; update if needed.
```

If the assigned channel differs from 6, update `m25_spp.py`:

```python
conn.connect(channel=<actual_channel>)
```

Or perform SDP discovery instead of hard-coding the channel:

```python
import bluetooth
services = bluetooth.find_service(uuid="00001101-0000-1000-8000-00805F9B34FB",
                                  address=address)
channel = services[0]["port"]
conn.connect(channel=channel)
```

---

## Serial CLI commands
| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `status` | Show current wheel state |
| `key` | Show encryption key |
| `config [show]` | Show active wheel-side configuration |
| `config set left\|right` | Persist wheel side and reboot |
| `config reset` | Clear saved side and reboot |
| `hardware` | Show pin assignments |
| `battery [0-100]` | Get / set battery level |
| `speed <val>` | Get / set raw speed |
| `assist [0-2]` | Get / set assist level |
| `profile [0-5]` | Get / set drive profile |
| `hillhold [on\|off]` | Get / set hill hold |
| `rotate [n]` | Simulate n rotations |
| `reset` | Reset rotation counter |
| `debug [flag\|all\|none]` | Toggle debug flags |
| `audio [on\|off]` | Toggle audio feedback |
| `visual [on\|off]` | Toggle speed LED |
| `beep [1-10]` | Play beeps |
| `tone <freq>` | Play tone (Hz) |
| `send` | Send ACK now |
| `disconnect` | Disconnect client |
| `advertise` | Restart advertising |
| `restart` | Reboot ESP32 |
| `power off` | Enter deep sleep |

### Debug flags
```
debug protocol   Frame parsing details
debug crypto     Encrypt/decrypt steps
debug crc        CRC check details
debug commands   Decoded command details
debug raw        Raw hex dumps
debug all        Enable all
debug none       Disable all
```

---

## Enabling BLE alongside RFCOMM
In `config.h`:

```cpp
#define TRANSPORT_RFCOMM_ENABLED  1
#define TRANSPORT_BLE_ENABLED     1
```

Both transports run in parallel. Received frames from either transport
are decoded the same way; ACKs are sent to whichever transport is connected.

---

## Hardware wiring (defaults)
| Signal | Pin |
|--------|-----|
| LED Red (low battery) | 25 |
| LED Yellow (mid battery) | 26 |
| LED Green (full battery) | 27 |
| LED White (connection) | 32 |
| LED Blue (speed) | 14 |
| Active buzzer | 22 |
| Passive buzzer | 23 |
| Button (GND when pressed) | 33 |

All pins can be changed in `config.h` without touching any other file.
