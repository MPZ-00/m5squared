#!/usr/bin/env python3
"""
M25 Windows Bluetooth Module - Cross-platform Bluetooth using Bleak

Windows doesn't support PyBluez well, so we use Bleak (Bluetooth Low Energy Async library)
which works on Windows, Linux, and macOS.

This module provides the same interface as m25_bluetooth.py but uses Windows-compatible APIs.
"""

import asyncio
import sys
import json
from pathlib import Path
from typing import Optional, List, Tuple

try:
    from bleak import BleakScanner, BleakClient
    HAS_BLEAK = True
except ImportError:
    HAS_BLEAK = False
    print("ERROR: bleak not installed. Install with: pip install bleak", file=sys.stderr)

try:
    from m25_protocol import calculate_crc, remove_delimiters
    from m25_utils import parse_hex
except ImportError:
    print("ERROR: m25_protocol.py or m25_utils.py not found", file=sys.stderr)
    sys.exit(1)


# M25 device name prefixes
M25_DEVICE_PREFIXES = ["emotion", "M25", "e-motion", "Alber", "WHEEL"]

# State file for storing connection info
STATE_FILE = Path.home() / ".m5squared" / "windows_state.json"


class M25WindowsBluetooth:
    """Windows Bluetooth handler for M25 devices using Bleak"""
    
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.connected = False
        
    async def scan(self, duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]:
        """
        Scan for Bluetooth devices
        
        Args:
            duration: Scan duration in seconds
            filter_m25: Only show M25-like devices
            
        Returns:
            List of (address, name) tuples
        """
        if not HAS_BLEAK:
            return []
            
        print(f"Scanning for Bluetooth devices ({duration} seconds)...")
        print("Make sure your M25 wheels are powered on.\n")
        
        devices = await BleakScanner.discover(timeout=duration, return_adv=True)
        
        results = []
        for addr, (device, adv_data) in devices.items():
            name = device.name or "Unknown"
            
            # Filter for M25 devices if requested
            if filter_m25:
                if not any(prefix.lower() in name.lower() for prefix in M25_DEVICE_PREFIXES):
                    continue
                    
            results.append((addr, name))
            print(f"  [{addr}] {name}")
            if adv_data.rssi:
                print(f"      RSSI: {adv_data.rssi} dBm")
            print()
        
        if not results:
            print("No devices found.")
            if filter_m25:
                print("Tip: Try without --m25 filter to see all devices")
        else:
            print(f"Found {len(results)} device(s)")
            
        return results
    
    async def connect(self, address: str, timeout: int = 10) -> bool:
        """
        Connect to an M25 device
        
        Args:
            address: Bluetooth MAC address
            timeout: Connection timeout
            
        Returns:
            True if connected successfully
        """
        if not HAS_BLEAK:
            return False
            
        print(f"Connecting to {address}...")
        
        try:
            self.client = BleakClient(address, timeout=timeout)
            await self.client.connect()
            self.connected = self.client.is_connected
            
            if self.connected:
                print(f"Connected to {address}")
                self._save_state(address)
                return True
            else:
                print(f"Failed to connect to {address}")
                return False
                
        except Exception as e:
            print(f"Connection error: {e}", file=sys.stderr)
            return False
    
    async def disconnect(self):
        """Disconnect from current device"""
        if self.client and self.connected:
            await self.client.disconnect()
            self.connected = False
            self._clear_state()
            print("Disconnected")
    
    async def send_packet(self, data: bytes) -> bool:
        """
        Send data packet to connected device
        
        Args:
            data: Raw bytes to send
            
        Returns:
            True if sent successfully
        """
        if not self.connected or not self.client:
            print("Not connected to any device", file=sys.stderr)
            return False
            
        try:
            # For M25, we need to find the correct characteristic
            # This is a simplified version - you may need to adjust based on device
            services = await self.client.get_services()
            
            # Look for Serial Port Profile or similar characteristic
            for service in services:
                for char in service.characteristics:
                    if "write" in char.properties:
                        await self.client.write_gatt_char(char, data)
                        return True
                        
            print("No writable characteristic found", file=sys.stderr)
            return False
            
        except Exception as e:
            print(f"Send error: {e}", file=sys.stderr)
            return False
    
    async def receive_packet(self, timeout: int = 5) -> Optional[bytes]:
        """
        Receive data packet from device
        
        Args:
            timeout: Receive timeout in seconds
            
        Returns:
            Received bytes or None
        """
        if not self.connected or not self.client:
            print("Not connected to any device", file=sys.stderr)
            return None
            
        try:
            # For notifications/indications
            # This is simplified - proper implementation would set up notification handlers
            services = await self.client.get_services()
            
            for service in services:
                for char in service.characteristics:
                    if "read" in char.properties:
                        data = await self.client.read_gatt_char(char)
                        return bytes(data)
                        
            return None
            
        except Exception as e:
            print(f"Receive error: {e}", file=sys.stderr)
            return None
    
    def _save_state(self, address: str):
        """Save connection state"""
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        state = {"address": address, "connected": True}
        STATE_FILE.write_text(json.dumps(state, indent=2))
    
    def _clear_state(self):
        """Clear connection state"""
        if STATE_FILE.exists():
            STATE_FILE.unlink()
    
    @staticmethod
    def load_state() -> dict:
        """Load saved connection state"""
        if STATE_FILE.exists():
            try:
                return json.loads(STATE_FILE.read_text())
            except:
                pass
        return {}


# Synchronous wrappers for backward compatibility
def scan_devices(duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]:
    """Synchronous wrapper for scan"""
    bt = M25WindowsBluetooth()
    return asyncio.run(bt.scan(duration, filter_m25))


def connect_device(address: str, timeout: int = 10) -> M25WindowsBluetooth:
    """Synchronous wrapper for connect"""
    bt = M25WindowsBluetooth()
    success = asyncio.run(bt.connect(address, timeout))
    return bt if success else None


def main():
    """Command-line interface for Windows Bluetooth operations"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="M25 Windows Bluetooth Toolkit",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Scan for all devices:
    python m25_bluetooth_windows.py scan
    
  Scan for M25 wheels only:
    python m25_bluetooth_windows.py scan --m25
    
  Connect to a device:
    python m25_bluetooth_windows.py connect AA:BB:CC:DD:EE:FF
    
  Check connection status:
    python m25_bluetooth_windows.py status
        """
    )
    
    subparsers = parser.add_subparsers(dest="command", help="Command to execute")
    
    # Scan command
    scan_parser = subparsers.add_parser("scan", help="Scan for Bluetooth devices")
    scan_parser.add_argument("--duration", type=int, default=10,
                            help="Scan duration in seconds (default: 10)")
    scan_parser.add_argument("--m25", action="store_true",
                            help="Filter for M25 devices only")
    
    # Connect command
    connect_parser = subparsers.add_parser("connect", help="Connect to a device")
    connect_parser.add_argument("address", help="Bluetooth MAC address")
    connect_parser.add_argument("--timeout", type=int, default=10,
                               help="Connection timeout (default: 10)")
    
    # Disconnect command
    subparsers.add_parser("disconnect", help="Disconnect from device")
    
    # Status command
    subparsers.add_parser("status", help="Show connection status")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return
    
    if args.command == "scan":
        devices = scan_devices(args.duration, args.m25)
        if devices:
            print(f"\nTo connect: python m25_bluetooth_windows.py connect <address>")
    
    elif args.command == "connect":
        bt = connect_device(args.address, args.timeout)
        if bt:
            print("Connection successful!")
            print("Use m25_ecs.py to interact with the device")
        else:
            sys.exit(1)
    
    elif args.command == "disconnect":
        bt = M25WindowsBluetooth()
        asyncio.run(bt.disconnect())
    
    elif args.command == "status":
        state = M25WindowsBluetooth.load_state()
        if state:
            print(f"Status: Connected to {state.get('address', 'Unknown')}")
        else:
            print("Status: Not connected")


if __name__ == "__main__":
    main()
