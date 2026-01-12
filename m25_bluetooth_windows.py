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
    from m25_crypto import M25Encryptor, M25Decryptor
except ImportError:
    print("ERROR: m25_protocol.py, m25_utils.py, or m25_crypto.py not found", file=sys.stderr)
    sys.exit(1)


# M25 device name prefixes
M25_DEVICE_PREFIXES = ["emotion", "M25", "e-motion", "Alber", "WHEEL"]

# State file for storing connection info
STATE_FILE = Path.home() / ".m5squared" / "windows_state.json"


class M25WindowsBluetooth:
    """Windows Bluetooth handler for M25 devices using Bleak"""
    
    def __init__(self, address: str = None, key: bytes = None, name: str = "wheel", debug: bool = False):
        """
        Initialize BLE connection
        
        Args:
            address: Bluetooth address (can be set later)
            key: Encryption key (16 bytes, can be set later)
            name: Friendly name for logging
            debug: Enable debug output
        """
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        
        self.client: Optional[BleakClient] = None
        self.connected = False
        
        # Encryption (only if key provided)
        self.encryptor = M25Encryptor(key) if key else None
        self.decryptor = M25Decryptor(key) if key else None
        
        # BLE characteristics
        self._tx_char = None
        self._rx_char = None
        
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
            
            # Filter if requested
            if filter_m25:
                if not any(prefix.lower() in name.lower() for prefix in M25_DEVICE_PREFIXES):
                    continue
            
            results.append((addr, name))
            print(f"  {addr:20s} {name}")
        
        return results
    
    async def connect(self, address: str = None, timeout: int = 10) -> bool:
        """
        Connect to an M25 device
        
        Args:
            address: Bluetooth MAC address (uses self.address if not provided)
            timeout: Connection timeout
            
        Returns:
            True if connected successfully
        """
        if not HAS_BLEAK:
            return False
        
        # Use provided address or instance address
        addr = address or self.address
        if not addr:
            print("No address provided", file=sys.stderr)
            return False
        
        self.address = addr
        
        if self.debug:
            print(f"[{self.name}] Connecting to {addr}...")
        
        try:
            self.client = BleakClient(addr, timeout=timeout)
            await self.client.connect()
            self.connected = self.client.is_connected
            
            if self.connected:
                if self.debug:
                    print(f"[{self.name}] Connected, discovering characteristics...")
                
                # Discover TX/RX characteristics
                await self._discover_characteristics()
                
                if self.debug:
                    print(f"[{self.name}] Ready (encryption={'enabled' if self.encryptor else 'disabled'})")
            
            return self.connected
            
        except Exception as e:
            print(f"[{self.name}] Connection error: {e}", file=sys.stderr)
            self.connected = False
            return False
    
    async def disconnect(self):
        """Disconnect from the device"""
        if self.client and self.connected:
            try:
                await self.client.disconnect()
            except Exception as e:
                if self.debug:
                    print(f"[{self.name}] Disconnect error: {e}", file=sys.stderr)
            finally:
                self.connected = False
                self.client = None
                
            if self.debug:
                print(f"[{self.name}] Disconnected")
    
    async def send_packet(self, data: bytes) -> bool:
        """
        Send raw packet data (will be encrypted if encryptor is set)
        
        Args:
            data: Raw packet bytes (unencrypted if encryptor exists)
            
        Returns:
            True if sent successfully
        """
        if not self.connected or not self.client or not self._tx_char:
            if self.debug:
                print(f"[{self.name}] Not connected or no TX char", file=sys.stderr)
            return False
        
        try:
            # Encrypt if encryptor is available
            send_data = self.encryptor.encrypt(data) if self.encryptor else data
            
            await self.client.write_gatt_char(self._tx_char, send_data, response=False)
            
            if self.debug:
                print(f"[{self.name}] Sent {len(send_data)} bytes (encrypted={self.encryptor is not None})")
            
            return True
            
        except Exception as e:
            print(f"[{self.name}] Send error: {e}", file=sys.stderr)
            return False
    
    async def send_async(self, encrypted_data: bytes) -> bool:
        """
        Send already-encrypted data
        
        Args:
            encrypted_data: Pre-encrypted data to send
            
        Returns:
            True if sent successfully
        """
        if not self.connected or not self.client or not self._tx_char:
            if self.debug:
                print(f"[{self.name}] Not connected or no TX char", file=sys.stderr)
            return False
        
        try:
            await self.client.write_gatt_char(self._tx_char, encrypted_data, response=False)
            
            if self.debug:
                print(f"[{self.name}] Sent {len(encrypted_data)} bytes (pre-encrypted)")
            
            return True
            
        except Exception as e:
            print(f"[{self.name}] Send error: {e}", file=sys.stderr)
            return False
    
    async def receive_packet(self, timeout: int = 5) -> Optional[bytes]:
        """
        Receive raw packet data (decrypts if decryptor is set)
        
        Args:
            timeout: Receive timeout in seconds
            
        Returns:
            Received bytes (decrypted if decryptor exists) or None
        """
        if not self.connected or not self.client or not self._rx_char:
            if self.debug:
                print(f"[{self.name}] Not connected or no RX char", file=sys.stderr)
            return None
        
        try:
            # Read from characteristic
            data = await self.client.read_gatt_char(self._rx_char)
            
            if data:
                # Decrypt if decryptor is available
                decrypted = self.decryptor.decrypt(bytes(data)) if self.decryptor else bytes(data)
                
                if self.debug:
                    print(f"[{self.name}] Received {len(data)} bytes (decrypted={self.decryptor is not None})")
                
                return decrypted
            
            return None
            
        except Exception as e:
            if self.debug:
                print(f"[{self.name}] Receive error: {e}", file=sys.stderr)
            return None
    
    def is_connected(self) -> bool:
        """Check if currently connected"""
        return self.connected and self.client and self.client.is_connected
    
    # Alias for compatibility
    async def connect_async(self, timeout: int = 10) -> bool:
        """Async connect (alias for connect)"""
        return await self.connect(timeout=timeout)
    
    async def disconnect_async(self):
        """Async disconnect (alias for disconnect)"""
        await self.disconnect()
    
    async def _discover_characteristics(self):
        """Discover TX/RX characteristics for Nordic UART service"""
        if not self.client or not self.client.is_connected:
            return
        
        # Nordic UART UUIDs
        NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
        NUS_TX_CHAR = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write (client -> device)
        NUS_RX_CHAR = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify (device -> client)
        
        services = self.client.services
        
        # Try Nordic UART first
        for service in services:
            if service.uuid.lower() == NUS_SERVICE:
                for char in service.characteristics:
                    if char.uuid.lower() == NUS_TX_CHAR:
                        self._tx_char = char.uuid
                        if self.debug:
                            print(f"[{self.name}] TX: {char.uuid}")
                    elif char.uuid.lower() == NUS_RX_CHAR:
                        self._rx_char = char.uuid
                        if self.debug:
                            print(f"[{self.name}] RX: {char.uuid}")
        
        # Fallback: find any write/notify characteristics
        if not self._tx_char or not self._rx_char:
            if self.debug:
                print(f"[{self.name}] Nordic UART not found, searching for write/notify characteristics...")
            
            for service in services:
                for char in service.characteristics:
                    if not self._tx_char and ("write" in char.properties or "write-without-response" in char.properties):
                        self._tx_char = char.uuid
                        if self.debug:
                            print(f"[{self.name}] TX (fallback): {char.uuid}")
                    if not self._rx_char and ("notify" in char.properties or "read" in char.properties):
                        self._rx_char = char.uuid
                        if self.debug:
                            print(f"[{self.name}] RX (fallback): {char.uuid}")
        
        if not self._tx_char:
            print(f"[{self.name}] WARNING: No TX characteristic found", file=sys.stderr)
        if not self._rx_char:
            print(f"[{self.name}] WARNING: No RX characteristic found", file=sys.stderr)


# Synchronous wrappers for backward compatibility
def scan_devices(duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]:
    """Synchronous wrapper for scan"""
    bt = M25WindowsBluetooth()
    return asyncio.run(bt.scan(duration, filter_m25))


def connect_device(address: str, key: bytes = None, timeout: int = 10) -> M25WindowsBluetooth:
    """Synchronous wrapper for connect"""
    bt = M25WindowsBluetooth(address=address, key=key)
    success = asyncio.run(bt.connect(timeout=timeout))
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
