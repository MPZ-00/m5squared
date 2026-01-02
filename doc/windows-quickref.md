# Windows Quick Reference

Because wrestling with PowerShell is easier than paying 595€ for a Bluetooth remote.

## First Time Setup

### The Easy Way

```powershell
# Run this and follow the prompts
.\setup-windows.ps1
```

Or if you're a Command Prompt person:

```cmd
setup-windows.bat
```

### The Manual Way

```powershell
# Create virtual environment
python -m venv .venv

# Activate it
.\.venv\Scripts\Activate.ps1

# If PowerShell yells at you about execution policies:
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# Install everything
pip install -e .

# Setup your secrets
Copy-Item .env.example .env
notepad .env
```

## Getting Your Keys

Each wheel has a QR code. That string is useless until you convert it:

```powershell
python m25_qr_to_key.py "YourQRCodeString"
# Outputs: 20a61dd47a91f690d93ca601d113806c
```

Stick those keys in your `.env` file:

```env
M25_LEFT_MAC=AA:BB:CC:DD:EE:FF
M25_LEFT_KEY=your_hex_key_here
M25_RIGHT_MAC=11:22:33:44:55:66
M25_RIGHT_KEY=your_other_hex_key
```

## Daily Use

### GUI (Recommended for Windows)

```powershell
python m25_gui.py
# or after install:
m25-gui
```

Point, click, done. All the features, none of the typing.

### Command Line

```powershell
# Find your wheels
python m25_bluetooth_windows.py scan --m25

# Read everything (pulls from .env)
python m25_ecs.py

# Change assist level (0=Normal, 1=Outdoor, 2=Learning)
python m25_ecs.py --set-level 1

# Toggle hill hold
python m25_ecs.py --hill-hold on

# Read battery
python m25_ecs.py --battery

# Override .env with manual addresses
python m25_ecs.py --left-addr AA:BB:CC:DD:EE:FF --left-key HEXKEY
```

## Configuration Check

```powershell
# See what's configured
python m25_config.py

# Validate everything
python m25_config.py --validate
```

## Common Issues

### PowerShell Won't Let You Run Scripts

```powershell
# Temporary fix (this session only)
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# Permanent fix (for your user)
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

### Virtual Environment Won't Activate

Make sure you're in the project directory, then:

```powershell
.\.venv\Scripts\Activate.ps1
```

You should see `(.venv)` in your prompt.

### Bluetooth Doesn't Work

```powershell
# Check if bleak is installed
pip show bleak

# Reinstall if needed
pip install bleak

# Make sure Windows Bluetooth is actually on
# Settings → Devices → Bluetooth & other devices
```

### Module Not Found

```powershell
# Make sure venv is active (see (.venv) in prompt?)
.\.venv\Scripts\Activate.ps1

# Reinstall
pip install -e .
```

### .env File Ignored

```powershell
# File exists?
Test-Path .env

# python-dotenv installed?
pip show python-dotenv

# Test it
python m25_config.py
```

### Connection Times Out

- Wheels powered on?
- Within 10 meters?
- Windows Bluetooth enabled?
- Try power cycling the wheels

## Tools Reference

| Script | What It Does |
|--------|-------------|
| `m25_ecs.py` | Main control interface |
| `m25_gui.py` | GUI version (easier) |
| `m25_bluetooth_windows.py` | Windows Bluetooth scanner |
| `m25_qr_to_key.py` | Convert QR codes to keys |
| `m25_config.py` | Check .env configuration |
| `m25_decrypt.py` | Decrypt captured packets |
| `m25_encrypt.py` | Encrypt packets |
| `m25_analyzer.py` | Protocol analysis |

## Useful Commands

```powershell
# Check Python version
python --version

# List installed packages
pip list

# Update everything
pip install --upgrade -e .

# Exit virtual environment
deactivate

# Dry run (no Bluetooth needed)
python m25_ecs.py --dry-run

# Get help on any tool
python m25_ecs.py --help
```

## Quick Cheat Sheet

| Task | Command |
|------|---------|
| Activate venv | `.\.venv\Scripts\Activate.ps1` |
| GUI | `python m25_gui.py` |
| Scan wheels | `python m25_bluetooth_windows.py scan --m25` |
| Convert QR | `python m25_qr_to_key.py "STRING"` |
| Check config | `python m25_config.py` |
| Read status | `python m25_ecs.py` |
| Set level | `python m25_ecs.py --set-level 1` |
| Hill hold | `python m25_ecs.py --hill-hold on` |

## More Documentation

- Full Windows setup: `doc/windows-setup.md`
- General usage: `doc/usage-setup.md`
- Protocol details: `doc/m25-protocol.md`
- Main README: `README.md`

## Remember

This is for **your own wheels**. Don't be weird.
