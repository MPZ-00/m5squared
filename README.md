# m5squared

**Your wheelchair, your rules.**

Python toolkit for the Alber e-motion M25 power-assist wheels. It started as a fork of [roll2own/m5squared](https://github.com/roll2own/m5sqared) and grew into a Python-focused control and analysis stack.

Presented at 39C3 Hamburg: *["Pwn2Roll: Who Needs a 595€ Remote When You Have wheelchair.py?"](https://media.ccc.de/v/39c3-pwn2roll-who-needs-a-595-remote-when-you-have-wheelchair-py)*

## Overview

This repository contains the Python control stack for the M25 protocol. It focuses on protocol handling, transport abstractions, a GUI control surface, and supporting utilities for analysis and testing.

The project keeps the control stack split into clear layers:

- Core control logic with mapper and supervisor components
- Transport abstraction for BLE, mock devices, and future transports
- GUI and CLI tools for interactive control and diagnostics
- Protocol utilities for encryption, decryption, packet framing, and analysis

## What This Does

- Decrypt and inspect the M25 Bluetooth protocol.
- Send protocol commands to read status and change settings.
- Provide a modular Python architecture with safer control flow.
- Support Windows and Linux Bluetooth backends.
- Offer mock and demo tooling for development and testing.

## The 595 Euro Remote vs. This Script

| Feature | Official ECS Remote | m5squared |
| --- | --- | --- |
| Price | 595 euro | Free |
| Read battery | Yes | Yes |
| Change assist level | Yes | Yes |
| Toggle hill hold | Yes | Yes |
| Read raw sensor data | No | Yes |
| Adjust drive parameters | No | Yes |
| Works on Linux | No | Yes |

## Quick Start

### Easiest Setup on Linux

```bash
./setup-linux.sh
```

That script sets up the local environment, installs the project, and prepares the common Linux dependencies.

### Run the Tools

```bash
# Windows: Double-click start.bat
# Linux/macOS: ./start.sh
# Or:
python launch.py
```

See [QUICKSTART.md](QUICKSTART.md) for more options and details.

### Manual Setup

```bash
# Setup (Ubuntu/Debian)
sudo apt install python3.12-venv python3-bluez
python3 -m venv .venv --system-site-packages
.venv/bin/pip install -e .
source .venv/bin/activate

# Get your AES keys (scan the QR codes on your wheel hubs)
python m25_qr_to_key.py "YourQRCodeHere"

# Talk to your wheels
python m25_ecs.py --left-addr AA:BB:CC:DD:EE:FF --right-addr 11:22:33:44:55:66 \
                  --left-key HEXKEY --right-key HEXKEY
```

## Architecture

This project uses a layered control architecture.

- Application layer: GUI, CLI, and demo entry points.
- Control layer: mapper, supervisor, and state handling.
- Protocol layer: m25_protocol.py and m25_crypto.py.
- Transport layer: BLE, mock, and other transport adapters.

### Key Components

- core/mapper.py: transforms input events into protocol commands.
- core/supervisor.py: safety state machine and control guardrails.
- core/transport/: unified interface for BLE, mock devices, and future transports.
- core/README.md: deeper reference for the shared control core.

## Tools

| Script | What it does |
| --- | --- |
| m25_qr_to_key.py | QR code to AES key conversion |
| m25_ecs.py | Read status and change settings |
| m25_gui.py | GUI interface for the Python control stack |
| launch.py | Unified launcher for GUI and CLI entry points |
| m25_decrypt.py | Decrypt captured Bluetooth packets |
| m25_encrypt.py | Encrypt packets for transmission |
| m25_analyzer.py | Inspect packet streams |
| m25_parking.py | Remote movement control |
| m25_bluetooth.py | Linux Bluetooth scan/connect/send/receive |
| m25_bluetooth_windows.py | Windows Bluetooth support via Bleak |
| m25_bluetooth_winrt.py | Windows WinRT Bluetooth support |

## Getting Your Keys

Each wheel has a QR code sticker. That's your key to the kingdom.

1. Scan the QR code (22 characters of proprietary encoding).
2. Run `python m25_qr_to_key.py "ABCD1234..."`.
3. Get a 16-byte hex key.
4. Use it with `--left-key` / `--right-key`.

Both wheels have different keys. Yes, you need both.

Once you've saved your keys, consider taping over the QR codes.

## Protocol TL;DR

Bluetooth SPP on channel 6. Packets look like:

```text
[0xEF] [length:2] [IV encrypted with ECB:16] [payload encrypted with CBC:n] [CRC:2]
```

Why encrypt the IV with ECB first? Nobody knows. But it works.

## Requirements

### Python Dependencies

- Python 3.8+ (3.12 recommended)
- pycryptodome for AES encryption and decryption
- python-dotenv for configuration management
- bluez / python3-bluez for native Linux Bluetooth support
- bleak for cross-platform BLE
- winrt-runtime / winrt-Windows.Devices.Bluetooth for optional Windows WinRT support
- pygame for optional gamepad input during testing

All Python dependencies are specified in [pyproject.toml](pyproject.toml) and install automatically with `pip install -e .`.

## Configuration

Create a `.env` file from the template to store your credentials securely:

```bash
cp .env.example .env
```

Edit `.env` and fill in:

- `M25_LEFT_MAC` - Left wheel MAC address
- `M25_LEFT_KEY` - Left wheel encryption key
- `M25_RIGHT_MAC` - Right wheel MAC address
- `M25_RIGHT_KEY` - Right wheel encryption key

The `.env` file is gitignored and won't be committed.

## Legal Stuff

This is for **your own wheels**. Don't be creepy.

- Research and education: Yes
- Your own devices: Yes
- Other people's wheelchairs: Absolutely not

## Links

### Media Coverage

- [[DE] Warum eine Multiple-Sklerose-Erkrankte ihren Rollstuhl hackte](https://www.spiegel.de/netzwelt/hackerkonferenz-39c3-warum-eine-multiple-sklerose-erkrankte-ihren-rollstuhl-hackte-a-169573b3-dbc9-4aed-bc33-1cc39cee6971)
- [[DE] Wie eine Multiple-Sklerose-Erkrankte die Paywall ihres Rollstuhls knackte](https://www.derstandard.at/story/3000000302326/wie-eine-multiple-sklerose-erkrankte-die-paywall-ihres-rollstuhls-knackte)
- [[DE] 39C3: Rollstuhl-Security – Wenn ein QR-Code alle Schutzmechanismen aushebelt](https://www.heise.de/news/39C3-Rollstuhl-Security-Wenn-ein-QR-Code-alle-Schutzmechanismen-aushebelt-11126816.html)
- [[EN] Louis Rossmann: Wheelchairs have paywalls and digital locks now (YouTube video)](https://www.youtube.com/watch?v=5yWcXPDJQ7k)


### Repository Links

- Upstream project: [roll2own/m5squared](https://github.com/roll2own/m5squared)
- Supplementary resources: [m5squared-resources](https://github.com/roll2own/m5squared-resources)
- Architecture docs: [core/README.md](core/README.md)
- Project docs: [doc/](doc/)

## Security, Safety and Common Sense

This is non-profit research code. No exploits, no vulnerabilities. It's just documentation and tools for interoperability with a protocol that already exists.

The security model here is identical to what the official apps use: if you don't have the QR code, you don't get access. We're not bypassing anything, just using the same protocol with our own code. Your wheels, your keys, your choice of software.

That said, this only applies to your own equipment. Using these tools on someone else's wheelchair without explicit consent isn't just unethical, it might cause serious safety issues. Also: creepy.

The author(s) take responsible disclosure seriously. These are mobility devices that people depend on every day, and that should come before making a point.

## Development

- Core protocol and transport work
- GUI and CLI improvements
- Logging and diagnostics cleanup
- Documentation updates
- Test coverage and tooling

Pull requests are welcome, especially for transport reliability improvements, protocol handling fixes, cross-platform BLE bug fixes, documentation, and examples.