# Fake M25 Wheel Simulator

ESP32 sketch simulating an Alber e-motion M25 wheel for testing. Requires ESP32-WROOM-32 or compatible.

## Setup

**Critical:** Each wheel (LEFT/RIGHT) has unique encryption keys!

1. Edit `device_config.h` and convert your `.env` key to byte array:
   ```
   .env:    A1B2C3D4E5F6A7B8...
   Arduino: 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0xA7, 0xB8, ...
   ```

2. Upload, open Serial Monitor (115200), note MAC address

3. Update `.env` with ESP32's MAC and matching key

## Features

- Full M25 protocol: CBC encryption, CRC-16, byte stuffing
- Simulates battery, assist level, speed
- Debug flags in `device_config.h` for verbose output
- Type `help` in Serial Monitor for commands

## Troubleshooting

- **CRC mismatch:** Wrong encryption key for LEFT/RIGHT
- **Connection drops:** Check power/signal strength
