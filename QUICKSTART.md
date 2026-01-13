# Quick Start Guide

This guide shows you the easiest ways to start using m5squared wheelchair control.

## The Simplest Way (Recommended)

Just run the launcher:

```bash
python launch.py
```

Or double-click `start.bat` (Windows) or run `./start.sh` (Linux/Mac).

This starts the **GUI** with optional core architecture support. It includes:
- Easy-to-use interface
- Optional core architecture mode (Supervisor with safety features)
- Optional deadman disable (with safety confirmation)
- Ready for testing with mock transport or real hardware

## Other Options

### 1. Gamepad Control (Testing)

Test gamepad input with mock transport:

```bash
python launch.py --gamepad --mock
```

Requires pygame: `pip install pygame`

### 2. Core Architecture Demo

See the core architecture in action:

```bash
python launch.py --demo
```

## GUI Features

The main GUI provides:
- Connect to wheels (or use mock for testing)
- Real-time battery status
- Change assist levels
- Toggle hill hold
- Read sensor data
- **Optional Core Architecture Mode**: Enable Supervisor with safety features
- **Optional Deadman Disable**: Disable deadman requirement (with safety warning)

### Using Core Architecture

In the GUI, check the box:
- ☑ **Use Core Architecture (Supervisor with safety)**

This enables:
- State machine with safety watchdogs
- Safety-critical input transformation
- Pluggable components
- Deadman switch, timeouts, ramping

### Disabling Deadman (USE WITH CAUTION)

If using core architecture, you can optionally disable the deadman requirement:
- ☑ **Disable Deadman Requirement ⚠ (USE WITH CAUTION)**

**WARNING**: This removes a critical safety feature! Only use for testing in a controlled environment.

## Testing Without Hardware

The GUI supports **mock transport** for testing without actual wheelchair hardware. Simply don't enter any MAC addresses and use the demo/test features.

For gamepad testing:

```bash
python launch.py --gamepad --mock
```

## Next Steps

1. **Start the GUI**
   ```bash
   python launch.py
   ```
   Or double-click `start.bat` (Windows) / run `./start.sh` (Linux/Mac)

2. **Test with Gamepad** (if you have one)
   ```bash
   pip install pygame
   python launch.py --gamepad --mock
   ```

3. **Run the Demo** (to see core architecture)
   ```bash
   python launch.py --demo
   ```

## Troubleshooting

### "No module named 'core'"
Make sure you're in the project root directory.

### "pygame not installed"
Install it:
```bash
pip install pygame
```

### "No game controllers found"
Connect a USB gamepad or use the GUI instead.

## Getting Help
```bash
python launch.py --help
```

Shows all available options and examples.
