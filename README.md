# m5squared - ESP32 Fork

**Your wheelchair, your rules. Now with embedded control.**

## Project Vision

**ESP32-based controller for M25 wheelchair protocol**

This fork builds on top of [roll2own/m5squared](https://github.com/roll2own/m5squared), adding:
- **Modular control architecture** with safety state machine
- **ESP32 firmware target** for standalone embedded control
- **Windows + BLE support** via Bleak/WinRT
- **Analog joystick control** as primary interface (gamepad support for testing only)
- **Core transport abstraction layer** for extensibility

### What Problem This Solves

The original m5squared proven the M25 protocol can be reverse-engineered and controlled via Python. This fork extends that work to:
- **Enable embedded controllers:** Run control logic on ESP32 hardware without a PC
- **Provide safety guarantees:** State machine ensures safe transitions and deadman switch support
- **Support multiple platforms:** Windows BLE support alongside Linux
- **Modularize architecture:** Clean separation between transport, protocol, and control layers

### Current Status

This is an **active development fork** focused on embedded control and architectural improvements. The core Python toolkit remains stable, while ESP32 firmware and advanced features are in active development.

**Upstream:** Based on roll2own/m5squared, presented at 39C3 Hamburg: *["Pwn2Roll: Who Needs a 595€ Remote When You Have wheelchair.py?"](https://media.ccc.de/v/39c3-pwn2roll-who-needs-a-599-remote-when-you-have-wheelchair-py)*

## What This Does

- **Decrypt the "encrypted" Bluetooth protocol:** AES-128-CBC, nothing fancy
- **Replace the €595 ECS remote:** Same features, zero cost, more control
- **Bypass all in-app purchases:** All "premium" features, free (speed boost, parking mode)
- **Access dealer-only parameters:** Your wheels, your data
- **Control via ESP32:** Standalone embedded controller with analog joystick input
- **Modular architecture:** Clean transport abstraction for BLE, mock devices, or future protocols

## The €595 Remote vs. This Fork

| Feature                    | Official ECS Remote | wheelchair.py | This Fork      |
| -------------------------- | ------------------- | ------------- | -------------- |
| Price                      | €595                | Free          | Free           |
| Read battery               | Yes                 | Yes           | Yes            |
| Change assist level        | Yes                 | Yes           | Yes            |
| Toggle hill hold           | Yes                 | Yes           | Yes            |
| Read raw sensor data       | No                  | Yes           | Yes            |
| Adjust drive parameters    | No                  | Yes           | Yes            |
| Works on Linux             | No                  | Obviously     | Yes            |
| Works on Windows (BLE)     | No                  | Limited       | Yes            |
| ESP32 embedded control     | No                  | No            | Yes (Beta)     |
| Safety state machine       | No                  | No            | Yes            |
| Analog joystick control    | No                  | No            | Yes            |

## Project Status

### Component Status

| Component                      | Status       | Notes                                          |
| ------------------------------ | ------------ | ---------------------------------------------- |
| Control pipeline (core)        | **Stable**   | Mapper, supervisor, state machine tested       |
| GUI (Python/tkinter)           | **Stable**   | Windows + Linux support                        |
| BLE Windows (Bleak/WinRT)      | **Working**  | Functional, some edge cases                    |
| ESP32 firmware (Arduino)       | **Beta**     | Basic control working, needs refinement        |
| Bidirectional communication    | **In Progress** | ACK verification and response handling      |
| Custom hardware enclosure      | **Planned**  | ESP32 + joystick enclosure design              |
| Cruise control                 | **Planned**  | Protocol support exists, needs integration     |

### What's Stable vs. Experimental

**Stable (Production-Ready):**
- Core Python toolkit (encryption, decryption, protocol)
- GUI control interface
- Input mapping (analog joystick, gamepad for testing)
- Core architecture (mapper, supervisor, transport abstraction)

**Working (Beta):**
- Windows BLE via Bleak
- ESP32 firmware (basic functionality)
- Safety state machine

**In Progress:**
- Full ESP32 feature parity
- Bidirectional communication and response handling

**Experimental:**
- ESP32 WiFi bridge mode
- WiFi/mobile joystick interface
- MicroPython port

## Quick Start

**Easiest Way (All Platforms):**

```bash
# Windows: Double-click start.bat
# Linux/Mac: ./start.sh
# Or:
python launch.py
```

The GUI includes optional core architecture support with safety features, plus an optional (but cautioned) deadman disable mode for controlled testing.

See [QUICKSTART.md](QUICKSTART.md) for more options and details.

**Traditional Setup:**

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

## Architecture Overview

This fork introduces a **modular control architecture** to separate concerns and enable multiple transport implementations.

### Core Layers

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                    │
│              (GUI, CLI, Gamepad Demo)                   │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                    Control Layer                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │   Mapper     │  │  Supervisor  │  │ State Machine│   │
│  │ (Input->Cmd) │  │  (Safety)    │  │   (Modes)    │   │
│  └──────────────┘  └──────────────┘  └──────────────┘   │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                 Protocol Layer                          │
│         m25_protocol.py, m25_crypto.py                  │
│     (Encryption, packet framing, command encoding)      │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                 Transport Layer                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │ BLE (Linux)  │  │ BLE (Windows)│  │ Mock/Testing │   │
│  │   bluez      │  │ Bleak/WinRT  │  │              │   │
│  └──────────────┘  └──────────────┘  └──────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### Key Components

- **Mapper (`core/mapper.py`):** Transforms input events (analog joystick primary, gamepad for testing) into protocol commands
- **Supervisor (`core/supervisor.py`):** Safety state machine with deadman switch logic
- **Transport Abstraction (`core/transport/`):** Unified interface for BLE, mock devices, future protocols
- **Protocol Layer:** AES-128-CBC encryption, packet framing, CRC validation

### Safety State Machine

The supervisor implements a **state machine** to ensure safe operation:
- **IDLE:** No input, motors disabled
- **ACTIVE:** Valid input received, motors enabled
- **DEADMAN_VIOLATED:** Input lost, motors disabled until reset
- **EMERGENCY_STOP:** Critical failure, requires manual reset

All state transitions are logged and validated.

## Hardware

### Supported Hardware

**PC Control (Stable):**
- Windows 10/11 with BLE adapter
- Linux with BlueZ support
- Gamepad supported by `pygame` (for testing/development only)

**ESP32 Embedded Control (Beta - Primary Target):**
- ESP32-DevKit or compatible board
- **Analog joystick (2-axis, center-return)** - Primary control interface
- Optional: LEDs, buzzer for status feedback
- Future: WiFi/mobile joystick interface

### ESP32 Pinout Reference

See [esp32/arduino/remote_control/device_config.h](esp32/arduino/remote_control/device_config.h) for default pinout:
- Joystick X/Y: GPIO 34/35 (ADC1 channels)
- LEDs: GPIO 25, 26, 27
- Buzzer: GPIO 5 (optional)
- Button: GPIO 0 (boot button as deadman switch)

### Future Hardware Plans

- Custom enclosure design for ESP32 + analog joystick integration
- 3D-printable housing compatible with wheelchair mounting
- Rechargeable battery pack with power management
- WiFi/mobile joystick as alternative input method

## Safety Disclaimer

### Critical Safety Information

This software controls powered mobility devices. Improper use can result in serious injury or death.

**Before using this software:**
- Ensure you have adequate space to test safely
- Always have a way to physically stop the wheelchair
- Start with minimal assist levels in controlled environments
- Understand that software bugs can cause unexpected behavior
- Never rely solely on electronic deadman switches

**You assume all risks when using this software. The authors provide no warranties of any kind.**

**Specific Risks:**
- Loss of wireless connection can cause loss of control
- Software bugs may cause unexpected movement
- Battery depletion can cause sudden loss of control
- Hardware failures in DIY controllers are possible

**Testing Recommendations:**
- Test in open space away from obstacles
- Have a spotter present during initial tests
- Keep original M25 remote accessible as backup
- Test emergency stop procedures before normal use
- Document any unexpected behaviors

Use at your own risk. This is experimental software for research and education.

## Windows Setup
### 1. Install Python 3.12
- Download and install Python 3.12 from python.org  
- Enable “Add Python to PATH” during setup  
- Verify:
  - `py -3.12 --version`

### 2. Create a virtual environment
```powershell
cd C:\path\to\your\m5squared
py -3.12 -m venv .venv
```

### 3. Allow PowerShell script execution (current session only)
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

Optional, persistent for your user:
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

### 4. Activate the virtual environment
```powershell
.\.venv\Scripts\Activate.ps1
```

### 5. Upgrade pip
```powershell
python -m pip install --upgrade pip
```

### 6. Install the project in editable mode
```powershell
pip install -e .
```


## Tools & Components

### Python Scripts

| Script                      | What it does                                      | 
| --------------------------- | ------------------------------------------------- |
| `m25_qr_to_key.py`          | QR code → AES key (their encoding is... creative) |
| `m25_ecs.py`                | The main event: read status, change settings      |
| `m25_gui.py`                | GUI interface with core architecture support      |
| `launch.py`                 | Unified launcher for GUI with platform detection  |
| `m25_decrypt.py`            | Decrypt captured Bluetooth packets                |
| `m25_encrypt.py`            | Encrypt packets for transmission                  |
| `m25_analyzer.py`           | Make sense of the packet soup                     |
| `m25_parking.py`            | Remote movement control (use responsibly)         |
| `m25_bluetooth.py`          | Scan, connect, send/receive (Linux)               |
| `m25_bluetooth_windows.py`  | Windows Bluetooth support via Bleak               |
| `m25_bluetooth_winrt.py`    | Windows WinRT Bluetooth (experimental)            |

### Core Architecture (`core/`)

| Module                      | Purpose                                           | 
| --------------------------- | ------------------------------------------------- |
| `core/mapper.py`            | Input event → protocol command transformation     |
| `core/supervisor.py`        | Safety state machine and deadman logic            |
| `core/types.py`             | Type definitions and data structures              |
| `core/interfaces.py`        | Abstract interfaces for extensibility             |
| `core/transport/`           | Transport layer abstraction (BLE, mock, etc.)     |

### ESP32 Firmware (`esp32/`)

| Directory                          | Purpose                                    | 
| ---------------------------------- | ------------------------------------------ |
| `esp32/arduino/remote_control/`    | Main ESP32 joystick controller firmware    |
| `esp32/arduino/fake_m25_wheel/`    | Test fixture: simulates M25 wheel          |
| `esp32/micropython/`               | MicroPython port (experimental)            |

### Demo Applications (`demos/`)

| Demo                        | Purpose                                           | 
| --------------------------- | ------------------------------------------------- |
| `demo_core.py`              | Core architecture demonstration                   |
| `demo_gamepad.py`           | Gamepad testing (development/testing only)        |
| `demo_gamepad_live.py`      | Live gamepad testing with BLE                     |
| `demo_integrated.py`        | Full integrated system demo                       |

**Note:** Gamepad demos are for testing with available hardware. Primary target is ESP32 with analog joystick.

## Getting Your Keys

Each wheel has a QR code sticker. That's your key to the kingdom.

1. Scan the QR code (22 characters of proprietary encoding)
2. Run: `python m25_qr_to_key.py "ABCD1234..."`
3. Get a 16-byte hex key
4. Use it with `--left-key` / `--right-key`

Both wheels have different keys. Yes, you need both.

## Protocol TL;DR

Bluetooth SPP on channel 6. Packets look like:

```
[0xEF] [length:2] [IV encrypted with ECB:16] [payload encrypted with CBC:n] [CRC:2]
```

Why encrypt the IV with ECB first? Nobody knows. But it works.

## Requirements

### Python Dependencies

- **Python 3.8+** (3.12 recommended)
- **Core:**
  - `pycryptodome` - AES encryption/decryption
  - `python-dotenv` - Configuration management
- **Linux Bluetooth:**
  - `bluez` / `python3-bluez` - Native BlueZ support
- **Windows Bluetooth:**
  - `bleak` - Cross-platform BLE (installed automatically)
  - `winrt-runtime` / `winrt-Windows.Devices.Bluetooth` - WinRT support (optional)
- **Input (Testing/Development):**
  - `pygame` - Gamepad input handling (optional, for testing only)

### ESP32 Firmware

- **Arduino IDE** with ESP32 board support
- Libraries: `BLE` (ESP32 built-in), `AES` (available via Library Manager)
- USB driver for ESP32 (CH340/CP2102 depending on board)

All Python dependencies are specified in [pyproject.toml](pyproject.toml) and install automatically with `pip install -e .`

## Configuration

Create a `.env` file from the template to store your credentials securely:

```bash
cp .env.example .env
```

Edit `.env` and fill in:
- `M25_LEFT_MAC` - Left wheel MAC address
- `M25_LEFT_KEY` - Left wheel encryption key (from QR code)
- `M25_RIGHT_MAC` - Right wheel MAC address  
- `M25_RIGHT_KEY` - Right wheel encryption key

The `.env` file is gitignored and won't be committed.

## Contributing & Development

This fork is under active development. Current development happens on the `feat/esp32` branch.

### Branch Structure

- **main:** Synced with upstream roll2own/m5squared
- **feat/esp32:** Active development branch (this fork's features)

### Development Focus Areas

- ESP32 firmware stabilization
- Custom hardware enclosure design for joystick integration
- WiFi/mobile joystick interface development
- Extended protocol support (cruise control, push counter)
- Documentation improvements

Pull requests welcome, especially for:
- Hardware testing and validation
- ESP32 firmware improvements
- Cross-platform BLE bug fixes
- Documentation and examples

## Legal Stuff

This is for **your own wheels**. Don't be creepy.

- Research and education: Yes
- Your own devices: Yes
- Other people's wheelchairs: Absolutely not

## Links

### This Fork

- **Repository:** [github.com/roll2own/m5squared](https://github.com/roll2own/m5squared) (feat/esp32 branch)
- **ESP32 Documentation:** [esp32/README.md](esp32/README.md)
- **Core Architecture:** [core/README.md](core/README.md)
- **Protocol Documentation:** [`doc/`](doc/)

### Upstream (Original Project)

- **Repository:** [github.com/roll2own/m5squared](https://github.com/roll2own/m5squared) (main branch)
- **Supplementary Resources:** [m5squared-resources repo](https://github.com/roll2own/m5squared-resources)
- **39C3 Talk:** *["Pwn2Roll: Who Needs a 595€ Remote When You Have wheelchair.py?"](https://media.ccc.de/v/39c3-pwn2roll-who-needs-a-599-remote-when-you-have-wheelchair-py)*

### Media Coverage

- [[DE] Warum eine Multiple-Sklerose-Erkrankte ihren Rollstuhl hackte](https://www.spiegel.de/netzwelt/hackerkonferenz-39c3-warum-eine-multiple-sklerose-erkrankte-ihren-rollstuhl-hackte-a-169573b3-dbc9-4aed-bc33-1cc39cee6971)
- [[DE] Wie eine Multiple-Sklerose-Erkrankte die Paywall ihres Rollstuhls knackte](https://www.derstandard.at/story/3000000302326/wie-eine-multiple-sklerose-erkrankte-die-paywall-ihres-rollstuhls-knackte)
- [[DE] 39C3: Rollstuhl-Security – Wenn ein QR-Code alle Schutzmechanismen aushebelt](https://www.heise.de/news/39C3-Rollstuhl-Security-Wenn-ein-QR-Code-alle-Schutzmechanismen-aushebelt-11126816.html)
