#!/usr/bin/env python3
"""
M25 Bluetooth Toolkit - Find wheels, talk to wheels, understand wheels.

Your one-stop shop for wheelchair Bluetooth shenanigans.
Scan, connect, send packets, receive responses. What more could you want?
"""

import argparse
import json
import os
import select
import subprocess
import sys
import time
from pathlib import Path

try:
    import bluetooth
    HAS_PYBLUEZ = True
except ImportError:
    HAS_PYBLUEZ = False

try:
    from m25_protocol import calculate_crc, remove_delimiters
    from m25_utils import parse_hex
except ImportError:
    print("ERROR: m25_protocol.py or m25_utils.py not found", file=sys.stderr)
    sys.exit(1)


# Connection state file
STATE_FILE = Path("/tmp/m25_rfcomm_state.json")

# M25 device identifiers
M25_DEVICE_NAMES = ["emotion", "M25", "e-motion", "Alber", "WHEEL"]

# Default RFCOMM channel for M25 (discovered via SDP)
DEFAULT_M25_CHANNEL = 6


def load_state():
    """Load connection state from file"""
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text())
        except (OSError, json.JSONDecodeError):
            pass
    return {}


def save_state(state):
    """Save connection state to file"""
    try:
        STATE_FILE.write_text(json.dumps(state, indent=2))
    except Exception as e:
        print(f"Warning: Could not save state: {e}", file=sys.stderr)


def clear_state():
    """Clear connection state"""
    try:
        STATE_FILE.unlink(missing_ok=True)
    except OSError:
        pass

def scan_devices(duration=10, filter_m25=False):
    """
    Scan for nearby Bluetooth devices using PyBluez

    Args:
        duration: Scan duration in seconds
        filter_m25: If True, only show potential M25 devices

    Returns:
        List of (address, name) tuples
    """
    if not HAS_PYBLUEZ:
        print("ERROR: PyBluez not installed. Install with: pip install pybluez", file=sys.stderr)
        print("       Or on Debian/Ubuntu: apt install python3-bluez", file=sys.stderr)
        return []

    print(f"Scanning for Bluetooth devices ({duration} seconds)...")
    print("Make sure your M25 wheel is powered on and in range.\n")

    try:
        devices = bluetooth.discover_devices(
            duration=duration,
            lookup_names=True,
            lookup_class=True,
            flush_cache=True
        )

        if not devices:
            print("No Bluetooth devices found.")
            print("Tips: Ensure Bluetooth is enabled and you have root privileges.")
            return []

        results = []
        print(f"Found {len(devices)} device(s):\n")
        print(f"{'MAC Address':<20} {'Name':<25} {'Class':<12} {'M25?'}")
        print("-" * 65)

        for addr, name, dev_class in devices:
            name = name or "(unknown)"
            is_m25 = any(m.lower() in name.lower() for m in M25_DEVICE_NAMES)
            m25_marker = "  <-- M25" if is_m25 else ""

            if filter_m25 and not is_m25:
                continue

            print(f"{addr:<20} {name:<25} 0x{dev_class:06x}{m25_marker}")
            results.append((addr, name))

        return results

    except bluetooth.BluetoothError as e:
        print(f"ERROR: Bluetooth scan failed: {e}", file=sys.stderr)
        print("Make sure Bluetooth is enabled and you have root privileges.", file=sys.stderr)
        return []

def find_services(address):
    """
    Discover RFCOMM services for a device

    Args:
        address: Bluetooth MAC address

    Returns:
        List of (channel, name) tuples
    """
    if not HAS_PYBLUEZ:
        print("ERROR: PyBluez not installed", file=sys.stderr)
        return []

    print(f"Discovering services for {address}...")

    try:
        services = bluetooth.find_service(address=address)

        rfcomm_services = []
        for svc in services:
            if svc.get("protocol") == "RFCOMM":
                port = svc.get("port")
                name = svc.get("name", "Unknown")
                rfcomm_services.append((port, name))

        if rfcomm_services:
            print(f"\nRFCOMM Services:")
            print(f"{'Channel':<10} {'Service Name'}")
            print("-" * 40)
            for port, name in rfcomm_services:
                print(f"{port:<10} {name}")
        else:
            print("No RFCOMM services found via SDP.")
            print(f"Will try default M25 channel: {DEFAULT_M25_CHANNEL}")

        return rfcomm_services

    except bluetooth.BluetoothError as e:
        print(f"ERROR: Service discovery failed: {e}", file=sys.stderr)
        return []

def get_rfcomm_status():
    """Get current rfcomm bindings"""
    try:
        result = subprocess.run(
            ["rfcomm", "-a"],
            capture_output=True,
            text=True,
            timeout=5
        )
        return result.stdout.strip()
    except Exception as e:
        return f"Error: {e}"


def find_free_rfcomm_device():
    """Find an available rfcomm device number"""
    for i in range(10):
        if not Path(f"/dev/rfcomm{i}").exists():
            return i
    return 0  # Fallback to rfcomm0


def connect_rfcomm(address, channel=None, device_num=None):
    """
    Bind rfcomm device to Bluetooth address

    Args:
        address: Bluetooth MAC address
        channel: RFCOMM channel (auto-detect if None)
        device_num: rfcomm device number (auto if None)

    Returns:
        Device path (e.g., /dev/rfcomm0) or None on failure
    """
    # Auto-detect channel via SDP if not specified
    if channel is None:
        if HAS_PYBLUEZ:
            services = find_services(address)
            if services:
                # Prefer SPP service
                for port, name in services:
                    if "serial" in name.lower() or "spp" in name.lower():
                        channel = port
                        break
                if channel is None:
                    channel = services[0][0]  # First available

        if channel is None:
            channel = DEFAULT_M25_CHANNEL
            print(f"Using default channel: {channel}")

    # Find free device number
    if device_num is None:
        device_num = find_free_rfcomm_device()

    device_path = f"/dev/rfcomm{device_num}"

    # Release if already bound
    if Path(device_path).exists():
        print(f"Releasing existing {device_path}...")
        subprocess.run(["rfcomm", "release", str(device_num)], capture_output=True)
        time.sleep(0.5)

    # Bind rfcomm device
    print(f"Binding {device_path} to {address} channel {channel}...")

    try:
        result = subprocess.run(
            ["rfcomm", "bind", str(device_num), address, str(channel)],
            capture_output=True,
            text=True,
            timeout=10
        )

        if result.returncode != 0:
            print(f"ERROR: rfcomm bind failed: {result.stderr}", file=sys.stderr)
            return None

        # Wait for device to appear
        for _ in range(10):
            if Path(device_path).exists():
                break
            time.sleep(0.2)

        if not Path(device_path).exists():
            print(f"ERROR: {device_path} did not appear", file=sys.stderr)
            return None

        print(f"Connected! Device: {device_path}")

        # Save state
        save_state({
            "device": device_path,
            "device_num": device_num,
            "address": address,
            "channel": channel,
            "connected_at": time.strftime("%Y-%m-%d %H:%M:%S")
        })

        return device_path

    except subprocess.TimeoutExpired:
        print("ERROR: rfcomm bind timed out", file=sys.stderr)
        return None
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return None


def disconnect_rfcomm(device_num=None):
    """Release rfcomm device"""
    state = load_state()

    if device_num is None:
        device_num = state.get("device_num", 0)

    device_path = f"/dev/rfcomm{device_num}"

    print(f"Releasing {device_path}...")

    try:
        result = subprocess.run(
            ["rfcomm", "release", str(device_num)],
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode == 0:
            print("Disconnected.")
            clear_state()
            return True
        else:
            print(f"Warning: {result.stderr.strip()}")
            clear_state()
            return False

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return False

def flush_input(fd, timeout=0.1):
    """Flush any pending input data"""
    flushed = 0
    while True:
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            break
        data = os.read(fd, 1024)
        if not data:
            break
        flushed += len(data)
    return flushed


def find_real_header(buffer):
    """Find position of real 0xEF header (not EFEF escape) in buffer"""
    i = 0
    while i < len(buffer):
        if buffer[i] == 0xEF:
            if i + 1 < len(buffer) and buffer[i + 1] == 0xEF:
                i += 2  # Skip escape sequence
                continue
            return i  # Real header
        i += 1
    return -1


def read_packet(fd, timeout_sec=5.0, verbose=False):
    """Read packet using accumulation + destuffing (like Android app)"""
    buffer = bytearray()
    deadline = time.time() + timeout_sec

    while time.time() < deadline:
        remaining = deadline - time.time()
        if remaining <= 0:
            break

        wait_time = 0.3 if len(buffer) > 0 else 0.1
        ready, _, _ = select.select([fd], [], [], min(wait_time, remaining))
        if ready:
            chunk = os.read(fd, 600)
            if chunk:
                buffer.extend(chunk)
                if verbose:
                    print(f"  Read {len(chunk)} bytes, buffer now {len(buffer)}", file=sys.stderr)

        if len(buffer) > 0:
            header_pos = find_real_header(buffer)
            if header_pos < 0:
                if verbose:
                    print(f"  Discarding {len(buffer)} stale bytes (no EF header)", file=sys.stderr)
                buffer.clear()
                continue
            elif header_pos > 0:
                if verbose:
                    print(f"  Discarding {header_pos} leading bytes", file=sys.stderr)
                del buffer[:header_pos]

        if len(buffer) >= 3:
            destuffed = remove_delimiters(bytes(buffer))
            if destuffed and len(destuffed) >= 3:
                length_field = (destuffed[1] << 8) | destuffed[2]
                expected_len = length_field + 1

                if len(destuffed) >= expected_len:
                    packet = destuffed[:expected_len]
                    crc_offset = length_field - 1
                    crc_calc = calculate_crc(packet, length_field - 1)
                    crc_recv = (packet[crc_offset] << 8) | packet[crc_offset + 1]

                    if crc_calc == crc_recv:
                        if verbose:
                            print(f"  CRC valid: 0x{crc_recv:04x}", file=sys.stderr)
                        return packet

        time.sleep(0.05)

    if verbose:
        print(f"  Timeout ({len(buffer)} bytes in buffer)", file=sys.stderr)
    return None


def transact(device_path, packet_hex, timeout_sec=5.0, verbose=False):
    """Send a packet and receive the response"""
    try:
        packet_bytes = parse_hex(packet_hex)
    except ValueError as e:
        print(f"ERROR: Invalid hex: {e}", file=sys.stderr)
        return None

    try:
        fd = os.open(device_path, os.O_RDWR)
    except Exception as e:
        print(f"ERROR: Cannot open {device_path}: {e}", file=sys.stderr)
        return None

    try:
        flushed = flush_input(fd, 0.1)
        if verbose and flushed > 0:
            print(f"Flushed {flushed} pending bytes", file=sys.stderr)

        if verbose:
            print(f"Sending {len(packet_bytes)} bytes...", file=sys.stderr)
        os.write(fd, packet_bytes)

        time.sleep(0.05)

        return read_packet(fd, timeout_sec, verbose)

    finally:
        os.close(fd)


def transact_with_retry(device_path, packet_hex, retries=5, timeout_sec=5.0, verbose=False):
    """Transact with retry logic (~95% success rate)"""
    for attempt in range(retries):
        if verbose and attempt > 0:
            print(f"\n--- Retry {attempt + 1}/{retries} ---", file=sys.stderr)

        response = transact(device_path, packet_hex, timeout_sec, verbose)

        if response:
            return response

        # Wait before retry (1.0s gives best results ~95% success rate)
        if attempt < retries - 1:
            time.sleep(1.0)

    return None

def interactive_mode(device_path, verbose=False):
    """Interactive mode for testing M25 communication"""
    print("\n=== M25 Interactive Mode ===")
    print(f"Device: {device_path}")
    print("\nCommands:")
    print("  send <hex>  - Send packet and show response")
    print("  raw <hex>   - Send raw bytes (no retry)")
    print("  flush       - Flush input buffer")
    print("  status      - Show connection status")
    print("  quit        - Exit")
    print()

    while True:
        try:
            cmd = input("m25> ").strip()

            if not cmd:
                continue

            if cmd.lower() in ("quit", "exit", "q"):
                break

            elif cmd.lower() == "flush":
                try:
                    fd = os.open(device_path, os.O_RDWR)
                    flushed = flush_input(fd, 0.5)
                    os.close(fd)
                    print(f"Flushed {flushed} bytes")
                except Exception as e:
                    print(f"Error: {e}")

            elif cmd.lower() == "status":
                state = load_state()
                print(f"Device: {state.get('device', 'N/A')}")
                print(f"Address: {state.get('address', 'N/A')}")
                print(f"Channel: {state.get('channel', 'N/A')}")
                print(f"Connected: {state.get('connected_at', 'N/A')}")

            elif cmd.lower().startswith("send "):
                hex_str = cmd[5:].replace(" ", "")
                response = transact_with_retry(device_path, hex_str, retries=5, verbose=verbose)
                if response:
                    print(f"Response: {response.hex()}")
                else:
                    print("No response (all retries failed)")

            elif cmd.lower().startswith("raw "):
                hex_str = cmd[4:].replace(" ", "")
                response = transact(device_path, hex_str, timeout_sec=3.0, verbose=verbose)
                if response:
                    print(f"Response: {response.hex()}")
                else:
                    print("No response")

            else:
                print("Unknown command. Type 'quit' to exit.")

        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except EOFError:
            break

def main():
    parser = argparse.ArgumentParser(
        description="M25 Bluetooth Toolkit - Scan, connect, and communicate",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  sudo python m25_bluetooth.py scan
  sudo python m25_bluetooth.py scan --m25
  sudo python m25_bluetooth.py services XX:XX:XX:XX:XX:XX
  sudo python m25_bluetooth.py connect XX:XX:XX:XX:XX:XX
  python m25_bluetooth.py transact -p "ef0024..."
  python m25_bluetooth.py status
  python m25_bluetooth.py disconnect
        """
    )

    subparsers = parser.add_subparsers(dest="command", help="Command")

    # scan
    scan_parser = subparsers.add_parser("scan", help="Scan for Bluetooth devices")
    scan_parser.add_argument("-d", "--duration", type=int, default=10, help="Scan duration (seconds)")
    scan_parser.add_argument("--m25", action="store_true", help="Only show M25-like devices")

    # services
    svc_parser = subparsers.add_parser("services", help="List RFCOMM services")
    svc_parser.add_argument("address", help="Bluetooth MAC address")

    # connect
    conn_parser = subparsers.add_parser("connect", help="Connect to device")
    conn_parser.add_argument("address", help="Bluetooth MAC address")
    conn_parser.add_argument("-c", "--channel", type=int, help="RFCOMM channel (auto-detect if omitted)")
    conn_parser.add_argument("-n", "--device-num", type=int, help="rfcomm device number")
    conn_parser.add_argument("-i", "--interactive", action="store_true", help="Enter interactive mode after connect")
    conn_parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    # disconnect
    disc_parser = subparsers.add_parser("disconnect", help="Disconnect from device")
    disc_parser.add_argument("-n", "--device-num", type=int, help="rfcomm device number")

    # transact
    tx_parser = subparsers.add_parser("transact", help="Send packet and receive response")
    tx_parser.add_argument("-p", "--packet", required=True, help="Packet hex to send")
    tx_parser.add_argument("-d", "--device", help="Device path (default: from connection state)")
    tx_parser.add_argument("-t", "--timeout", type=float, default=5.0, help="Timeout seconds")
    tx_parser.add_argument("-r", "--retries", type=int, default=5, help="Number of retries")
    tx_parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    # status
    subparsers.add_parser("status", help="Show connection status")

    # interactive (standalone)
    int_parser = subparsers.add_parser("interactive", help="Interactive mode")
    int_parser.add_argument("-d", "--device", help="Device path")
    int_parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Handle commands
    if args.command == "scan":
        scan_devices(duration=args.duration, filter_m25=args.m25)

    elif args.command == "services":
        find_services(args.address)

    elif args.command == "connect":
        device_path = connect_rfcomm(args.address, args.channel, args.device_num)
        if device_path and args.interactive:
            interactive_mode(device_path, args.verbose)

    elif args.command == "disconnect":
        disconnect_rfcomm(args.device_num)

    elif args.command == "transact":
        device_path = args.device
        if not device_path:
            state = load_state()
            device_path = state.get("device")

        if not device_path:
            print("ERROR: No device specified and no active connection.", file=sys.stderr)
            print("Use: python m25_bluetooth.py connect <MAC> first", file=sys.stderr)
            return 1

        if not Path(device_path).exists():
            print(f"ERROR: Device {device_path} does not exist.", file=sys.stderr)
            print("Use: sudo python m25_bluetooth.py connect <MAC> first", file=sys.stderr)
            return 1

        response = transact_with_retry(
            device_path,
            args.packet,
            retries=args.retries,
            timeout_sec=args.timeout,
            verbose=args.verbose
        )

        if response:
            print(response.hex())
            return 0
        else:
            print("ERROR: All retries failed", file=sys.stderr)
            return 1

    elif args.command == "status":
        state = load_state()
        rfcomm_status = get_rfcomm_status()

        print("=== M25 Connection Status ===\n")

        if state:
            print(f"Device:      {state.get('device', 'N/A')}")
            print(f"Address:     {state.get('address', 'N/A')}")
            print(f"Channel:     {state.get('channel', 'N/A')}")
            print(f"Connected:   {state.get('connected_at', 'N/A')}")

            device_path = state.get('device')
            if device_path and Path(device_path).exists():
                print(f"Status:      ACTIVE")
            else:
                print(f"Status:      STALE (device missing)")
        else:
            print("No active connection.\n")

        print(f"\nrfcomm -a output:")
        print(rfcomm_status or "(empty)")

    elif args.command == "interactive":
        device_path = args.device
        if not device_path:
            state = load_state()
            device_path = state.get("device")

        if not device_path or not Path(device_path).exists():
            print("ERROR: No active connection.", file=sys.stderr)
            print("Use: sudo python m25_bluetooth.py connect <MAC> first", file=sys.stderr)
            return 1

        interactive_mode(device_path, args.verbose)

    return 0


if __name__ == "__main__":
    sys.exit(main())
