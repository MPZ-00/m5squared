# Windows Setup Guide

Complete setup guide for running m5squared on Windows 10/11. This guide ensures full parity with the Linux version.

## Prerequisites

### 1. Install Python 3.12+

Download from [python.org](https://www.python.org/downloads/windows/) and install with these settings:
- Check "Add Python to PATH"
- Check "Install for all users" (optional, but recommended)
- Verify installation:
```powershell
python --version
# Should output: Python 3.12.x or higher
```

### 2. Enable Bluetooth

Windows 10/11 has built-in Bluetooth support:
1. Go to Settings → Devices → Bluetooth & other devices
2. Turn on Bluetooth
3. Make sure your M25 wheels are powered on

### 3. Install Build Tools (for some dependencies)

Some Python packages may need Microsoft C++ Build Tools:
- Download from: https://visualstudio.microsoft.com/visual-cpp-build-tools/
- Install "Desktop development with C++"
- Or install Visual Studio Community Edition

## Project Setup

### 1. Clone/Download the Project

```powershell
cd C:\Users\YourName\Documents
git clone https://github.com/roll2own/m5squared.git
cd m5squared
```

### 2. Create Virtual Environment

```powershell
# Create the virtual environment
python -m venv .venv

# Activate it (PowerShell)
.\.venv\Scripts\Activate.ps1

# If you get an execution policy error, run this first:
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

For Command Prompt users:
```cmd
.venv\Scripts\activate.bat
```

### 3. Upgrade pip and Install Dependencies

```powershell
# Make sure you're in the activated virtual environment
python -m pip install --upgrade pip
pip install -e .
```

This will install:
- pycryptodome (AES encryption)
- python-dotenv (environment variable management)
- bleak (Windows Bluetooth Low Energy support)

### 4. Configure Your Secrets

Create your `.env` file from the template:
```powershell
Copy-Item .env.example .env
notepad .env
```

Fill in your wheel information:
```env
M25_LEFT_MAC=AA:BB:CC:DD:EE:FF
M25_RIGHT_MAC=11:22:33:44:55:66
M25_LEFT_KEY=your_64_char_hex_key_from_qr_conversion
M25_RIGHT_KEY=your_64_char_hex_key_from_qr_conversion
```

### 5. Get Your AES Keys

Each M25 wheel has a QR code sticker with a "Cyber Security Key":

```powershell
# Scan the QR code with your phone, then convert it:
python m25_qr_to_key.py "YourQRCodeStringHere"

# Output example: 20a61dd47a91f690d93ca601d113806c
```

Copy these keys to your `.env` file.

## Finding Your Wheels

### Method 1: Windows Settings (Quick)
1. Open Settings → Devices → Bluetooth & other devices
2. Look for devices starting with "M25V1_" or "emotion"
3. Click on device → More Bluetooth options
4. Note the MAC address (format: AA:BB:CC:DD:EE:FF)

### Method 2: PowerShell Script (Advanced)
```powershell
# Scan for Bluetooth devices (requires admin)
python m25_bluetooth_windows.py scan
```

## Testing the Setup

### Dry Run (No Bluetooth Required)
```powershell
# Test packet generation without connecting
python m25_ecs.py --dry-run
```

### GUI Interface (Recommended for Windows)
```powershell
# Launch the graphical interface
python m25_gui.py

# Credentials are loaded automatically from .env
```

### Command Line Usage
```powershell
# Connect and read status
python m25_ecs.py

# Change assist level (0=Normal, 1=Outdoor, 2=Learning)
python m25_ecs.py --set-level 1

# Toggle hill hold on/off
python m25_ecs.py --hill-hold on
```

## Windows-Specific Notes

### Bluetooth Stack
Windows uses a different Bluetooth stack than Linux:
- Linux: BlueZ (RFCOMM sockets)
- Windows: WinRT Bluetooth APIs (via `bleak` library)

The `m25_bluetooth_windows.py` module handles these differences automatically.

### Permissions
Unlike Linux, Windows doesn't require special permissions for Bluetooth:
- No need for `sudo`
- No need to add yourself to groups
- Just make sure Bluetooth is enabled

### Serial Ports
Windows assigns COM ports automatically:
- M25 devices appear as Bluetooth Serial (COM ports)
- Check Device Manager → Ports (COM & LPT)
- The scripts handle this automatically

## Troubleshooting

### "Module not found" errors
```powershell
# Make sure virtual environment is activated
.\.venv\Scripts\Activate.ps1

# Reinstall dependencies
pip install -e .
```

### "bleak not found" or Bluetooth errors
```powershell
# Install bleak explicitly
pip install bleak

# Update Windows Bluetooth drivers
# Device Manager → Bluetooth → Right-click adapter → Update driver
```

### PowerShell Execution Policy
If you can't activate the virtual environment:
```powershell
# Temporary (current session only)
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# Permanent (current user)
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

### Connection Timeouts
- Make sure wheels are powered on
- Wheels should be within 10 meters (30 feet)
- Try turning Bluetooth off/on
- Restart the wheels by power cycling them

### "Access Denied" or Permission Errors
- Run PowerShell as Administrator (usually not needed)
- Check Windows Firewall isn't blocking Python
- Ensure antivirus isn't blocking Bluetooth

### .env File Not Found
```powershell
# Check if .env exists
Test-Path .env

# If not, create from template
Copy-Item .env.example .env
```

## GUI vs Command Line

### Use GUI If:
- You want a simple point-and-click interface
- You're new to command line tools
- You prefer visual feedback
- You're on Windows (native experience)

### Use Command Line If:
- You want to script/automate operations
- You're comfortable with terminals
- You want to integrate with other tools
- You need precise control over parameters

## Advanced: Packet Capture on Windows

To analyze Bluetooth traffic:

### Wireshark Setup
1. Install Wireshark: https://www.wireshark.org/
2. Install USBPcap during Wireshark installation
3. Start capture on Bluetooth adapter
4. Filter: `bthci_acl`

### Export Packets
```powershell
# Decrypt captured packets
python m25_decrypt.py -k YOUR_HEX_KEY < captured_packet.hex
```

## Updates and Maintenance

```powershell
# Activate virtual environment
.\.venv\Scripts\Activate.ps1

# Update dependencies
pip install --upgrade -e .

# Pull latest code
git pull origin main
pip install --upgrade -e .
```

## Security Notes

1. The `.env` file contains your encryption keys - keep it private
2. Never commit `.env` to git (it's in `.gitignore`)
3. The keys are unique per wheel
4. Factory reset of wheels generates new keys

## Getting Help

- Check the main documentation: `doc/m25-protocol.md`
- Review the protocol analyzer: `python m25_analyzer.py`
- Test individual components before full integration

## Parity with Linux

This Windows setup provides 100% feature parity with Linux:
- Same command-line tools
- Same protocols and encryption
- Same data access
- Additional GUI for convenience

The only difference is the Bluetooth layer, which is abstracted by the `m25_bluetooth_windows.py` module.
