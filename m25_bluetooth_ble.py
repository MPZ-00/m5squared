#!/usr/bin/env python3
"""
M25 BLE Module - Cross-platform Bluetooth Low Energy using Bleak

Provides BLE communication for M25 wheelchair wheels on Windows, Linux, and macOS.
Uses the Bleak library which has native support for all platforms.

Key Features:
- Cross-platform BLE (Windows, Linux, macOS)
- Power-efficient notifications (device pushes data instead of polling)
- Nordic UART Service with automatic characteristic discovery
- Integrated encryption/decryption
- Async/await API with synchronous wrappers

This replaces platform-specific modules:
- Windows: Better than RFCOMM (no auto-disconnect issues)
- Linux: Alternative to PyBluez RFCOMM (when BLE is available)
- macOS: Native BLE support
"""

import asyncio
import sys
import json
from pathlib import Path
from typing import Any, Optional, List, Tuple

try:
    from bleak import BleakScanner, BleakClient
    HAS_BLEAK = True
except ImportError:
    BleakScanner = None
    BleakClient = None
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

# Known BLE profiles seen on M25 wheels and local simulators.
KNOWN_M25_BLE_PROFILES = [
    {
        "name": "M25V2",
        "service": "49535343-fe7d-4ae5-8fa9-9fafd205e455",
        "tx": "49535343-8841-43f4-a8d4-ecbe34729bb3",
        "rx": "49535343-1e4d-4bd9-ba61-23c647249616",
    },
    {
        "name": "M25V1",
        "service": "c9e61e27-93c0-45c0-b5f9-702e971daa2e",
        "tx": "49535343-026e-3a9b-954c-97daef17e26e",
        "rx": "49535343-026e-3a9b-954c-97daef17e26e",
    },
    {
        "name": "Fake Left",
        "service": "00001101-0000-1000-8000-00805f9b34fb",
        "tx": "00001101-0000-1000-8000-00805f9b34fb",
        "rx": "00001102-0000-1000-8000-00805f9b34fb",
    },
    {
        "name": "Fake Right",
        "service": "00001101-0000-1000-8000-00805f9b34fb",
        "tx": "00001103-0000-1000-8000-00805f9b34fb",
        "rx": "00001104-0000-1000-8000-00805f9b34fb",
    },
    {
        "name": "Nordic UART",
        "service": "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
        "tx": "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
        "rx": "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
    },
]

# State file for storing connection info
STATE_FILE = Path.home() / ".m5squared" / "ble_state.json"


def load_state() -> dict:
    """Load persisted BLE connection state."""
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text())
        except (OSError, json.JSONDecodeError):
            pass
    return {}


def save_state(state: dict) -> None:
    """Save BLE connection state (compat with m25_bluetooth.py)."""
    try:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        STATE_FILE.write_text(json.dumps(state, indent=2))
    except Exception as e:
        print(f"Warning: Could not save state: {e}", file=sys.stderr)


def clear_state() -> None:
    """Clear BLE connection state (compat with m25_bluetooth.py)."""
    try:
        STATE_FILE.unlink(missing_ok=True)
    except OSError:
        pass


async def _find_services_async(address: str, timeout: int = 5) -> List[Tuple[int, str]]:
    """Discover BLE services and return tuples compatible with find_services()."""
    if not HAS_BLEAK or BleakClient is None:
        return []

    client = BleakClient(address, timeout=timeout)
    try:
        await client.connect()
        if not client.is_connected:
            return []

        services = []
        for service in client.services:
            # Keep tuple shape compatible with RFCOMM helper: (channel, name)
            # BLE has no RFCOMM channel, so use -1.
            services.append((-1, str(service.uuid)))
        return services
    except Exception:
        return []
    finally:
        try:
            if client.is_connected:
                await client.disconnect()
        except Exception:
            pass


def find_services(address: str, timeout: int = 5) -> List[Tuple[int, str]]:
    """Compatibility helper similar to m25_bluetooth.find_services()."""
    return asyncio.run(_find_services_async(address, timeout))


class M25BluetoothBLE:
    """Cross-platform BLE handler for M25 devices using Bleak"""
    
    def __init__(self, address: str = "", key: bytes = b"", name: str = "wheel", debug: bool = False):
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
        
        self.client: Optional[Any] = None
        self.connected = False
        
        # Encryption (only if key provided)
        self.encryptor = M25Encryptor(key) if key else None
        self.decryptor = M25Decryptor(key) if key else None
        
        # BLE characteristics
        self._tx_char = None
        self._rx_char = None
        self._tx_requires_response = False
        
        # Notification callback (for power-efficient data reception)
        self._notification_callback = None
        self._notification_queue = asyncio.Queue() if asyncio._get_running_loop() else None
        
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
        if BleakScanner is None:
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
    
    async def connect(self, address: str = "", timeout: int = 10) -> bool:
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
        if BleakClient is None:
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
                
                # Initialize notification queue (or clear if reconnecting)
                if self._notification_queue is None:
                    self._notification_queue = asyncio.Queue()
                else:
                    # Clear any stale data from previous connection
                    while not self._notification_queue.empty():
                        try:
                            self._notification_queue.get_nowait()
                        except asyncio.QueueEmpty:
                            break
                
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
                # Stop notifications if enabled
                if self._rx_char and self._notification_callback:
                    await self.stop_notifications()
                    
                await self.client.disconnect()
            except Exception as e:
                if self.debug:
                    print(f"[{self.name}] Disconnect error: {e}", file=sys.stderr)
            finally:
                self.connected = False
                self.client = None
                
                # Clear notification queue to prevent stale data on reconnect
                if self._notification_queue is not None:
                    while not self._notification_queue.empty():
                        try:
                            self._notification_queue.get_nowait()
                        except asyncio.QueueEmpty:
                            break
                
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
            
            await self.client.write_gatt_char(
                self._tx_char,
                send_data,
                response=self._tx_requires_response,
            )
            
            if self.debug:
                mode = "write-with-response" if self._tx_requires_response else "write-without-response"
                print(
                    f"[{self.name}] Sent {len(send_data)} bytes "
                    f"(encrypted={self.encryptor is not None}, mode={mode})"
                )
            
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
            await self.client.write_gatt_char(
                self._tx_char,
                encrypted_data,
                response=self._tx_requires_response,
            )
            
            if self.debug:
                mode = "write-with-response" if self._tx_requires_response else "write-without-response"
                print(f"[{self.name}] Sent {len(encrypted_data)} bytes (pre-encrypted, mode={mode})")
            
            return True
            
        except Exception as e:
            print(f"[{self.name}] Send error: {e}", file=sys.stderr)
            return False
    
    async def start_notifications(self, callback = None) -> bool:
        """
        Enable BLE notifications for power-efficient data reception
        
        Instead of polling (client constantly reading), the device pushes data
        when available. This significantly reduces power consumption on the wheels.
        
        Args:
            callback: Optional callback function(bytes). If None, uses internal queue.
        
        Returns:
            True if notifications started successfully
            
        Example:
            # Using callback
            def handle_data(data: bytes):
                print(f"Received: {data.hex()}")
            
            await bt.start_notifications(handle_data)
            
            # Using queue (no callback)
            await bt.start_notifications()
            while True:
                data = await bt.wait_notification()
                print(f"Got: {data.hex()}")
        """
        if not self.connected or not self.client or not self._rx_char:
            if self.debug:
                print(f"[{self.name}] Cannot start notifications: not connected or no RX char", file=sys.stderr)
            return False
        
        try:
            self._notification_callback = callback
            
            # Internal handler that decrypts and routes to callback or queue
            def notification_handler(sender, data: bytearray):
                # Decrypt if decryptor available
                decrypted = self.decryptor.decrypt(bytes(data)) if self.decryptor else bytes(data)
                
                if self.debug:
                    print(f"[{self.name}] Notification: {len(data)} bytes (decrypted={self.decryptor is not None})")
                
                # Route to callback or queue
                if self._notification_callback:
                    self._notification_callback(decrypted)
                elif self._notification_queue:
                    self._notification_queue.put_nowait(decrypted)
            
            await self.client.start_notify(self._rx_char, notification_handler)
            
            if self.debug:
                print(f"[{self.name}] Notifications enabled (power-efficient mode)")
            
            return True
            
        except Exception as e:
            print(f"[{self.name}] Failed to start notifications: {e}", file=sys.stderr)
            return False
    
    async def stop_notifications(self) -> bool:
        """
        Disable BLE notifications
        
        Returns:
            True if stopped successfully
        """
        if not self.connected or not self.client or not self._rx_char:
            return False
        
        try:
            await self.client.stop_notify(self._rx_char)
            self._notification_callback = None
            
            if self.debug:
                print(f"[{self.name}] Notifications disabled")
            
            return True
            
        except Exception as e:
            if self.debug:
                print(f"[{self.name}] Failed to stop notifications: {e}", file=sys.stderr)
            return False
    
    async def wait_notification(self, timeout: float = 0.0) -> Optional[bytes]:
        """
        Wait for notification data from queue (non-polling)
        
        Must call start_notifications() first without a callback.
        This is power-efficient as the device only wakes to send data.
        
        Args:
            timeout: Wait timeout in seconds (0.0 = wait forever)
        
        Returns:
            Received bytes (decrypted if key provided) or None on timeout
        """
        if not self._notification_queue:
            if self.debug:
                print(f"[{self.name}] No notification queue - call start_notifications() first", file=sys.stderr)
            return None
        
        try:
            if timeout:
                data = await asyncio.wait_for(self._notification_queue.get(), timeout=timeout)
            else:
                data = await self._notification_queue.get()
            return data
        except asyncio.TimeoutError:
            return None
    
    async def receive_packet(self, timeout: int = 5) -> Optional[bytes]:
        """
        Receive packet data by reading characteristic (POLLING - uses more power)
        
        WARNING: This method polls the device, which consumes more battery power.
        For power-efficient operation, use start_notifications() + wait_notification() instead.
        
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
        return bool(self.connected and self.client and self.client.is_connected)
    
    # Alias for compatibility
    async def connect_async(self, timeout: int = 10) -> bool:
        """Async connect (alias for connect)"""
        return await self.connect(timeout=timeout)
    
    async def disconnect_async(self):
        """Async disconnect (alias for disconnect)"""
        await self.disconnect()
    
    async def _discover_characteristics(self):
        """Discover TX/RX characteristics for known M25 BLE services"""
        if not self.client or not self.client.is_connected:
            return

        services = self.client.services

        matched_profile = _match_known_profile(services)
        if matched_profile:
            self._tx_char = matched_profile["tx"]
            self._rx_char = matched_profile["rx"]
            self._tx_requires_response = _requires_write_response(services, self._tx_char)
            if self.debug:
                mode = "write-with-response" if self._tx_requires_response else "write-without-response"
                print(
                    f"[{self.name}] Matched {matched_profile['name']} "
                    f"(service={matched_profile['service']}, mode={mode})"
                )
            return
        
        # Fallback: find any write/notify characteristics
        if not self._tx_char or not self._rx_char:
            if self.debug:
                print(f"[{self.name}] Known profiles not found, searching for write/notify characteristics...")
            
            for service in services:
                for char in service.characteristics:
                    if not self._tx_char and ("write" in char.properties or "write-without-response" in char.properties):
                        self._tx_char = char.uuid
                        self._tx_requires_response = _char_requires_write_response(char)
                        if self.debug:
                            mode = "write-with-response" if self._tx_requires_response else "write-without-response"
                            print(f"[{self.name}] TX (fallback): {char.uuid} ({mode})")
                    if not self._rx_char and ("notify" in char.properties or "read" in char.properties):
                        self._rx_char = char.uuid
                        if self.debug:
                            print(f"[{self.name}] RX (fallback): {char.uuid}")
        
        if not self._tx_char:
            print(f"[{self.name}] WARNING: No TX characteristic found", file=sys.stderr)
        if not self._rx_char:
            print(f"[{self.name}] WARNING: No RX characteristic found", file=sys.stderr)


def _match_known_profile(services) -> Optional[dict]:
    """Return the first known BLE profile that matches discovered services."""
    for profile in KNOWN_M25_BLE_PROFILES:
        service_uuid = profile["service"].lower()
        tx_uuid = profile["tx"].lower()
        rx_uuid = profile["rx"].lower()

        for service in services:
            if service.uuid.lower() != service_uuid:
                continue

            available = {char.uuid.lower() for char in service.characteristics}
            if tx_uuid in available and rx_uuid in available:
                return {
                    "name": profile["name"],
                    "service": service.uuid,
                    "tx": next(char.uuid for char in service.characteristics if char.uuid.lower() == tx_uuid),
                    "rx": next(char.uuid for char in service.characteristics if char.uuid.lower() == rx_uuid),
                }
    return None


def _char_requires_write_response(char) -> bool:
    """Return True when a characteristic supports only write-with-response."""
    props = {p.lower() for p in getattr(char, "properties", [])}
    can_write = "write" in props
    can_write_no_rsp = "write-without-response" in props
    return can_write and not can_write_no_rsp


def _requires_write_response(services, tx_uuid: str) -> bool:
    """Find tx char in discovered services and derive correct write mode."""
    target = tx_uuid.lower()
    for service in services:
        for char in service.characteristics:
            if char.uuid.lower() == target:
                return _char_requires_write_response(char)
    return False


async def detect_m25_ble_profile(address: str, timeout: int = 5) -> Optional[str]:
    """Connect briefly and detect whether a wheel exposes a known BLE profile."""
    if not HAS_BLEAK:
        return None
    if BleakClient is None:
        return None

    client = BleakClient(address, timeout=timeout)
    try:
        await client.connect()
        if not client.is_connected:
            return None
        matched_profile = _match_known_profile(client.services)
        return matched_profile["name"] if matched_profile else None
    except Exception:
        return None
    finally:
        try:
            if client.is_connected:
                await client.disconnect()
        except Exception:
            pass


# Synchronous wrappers for backward compatibility
def scan_devices(duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]:
    """Synchronous wrapper for scan"""
    bt = M25BluetoothBLE()
    return asyncio.run(bt.scan(duration, filter_m25))


def connect_device(address: str, key: bytes = b"", timeout: int = 10) -> Optional[M25BluetoothBLE]:
    """Synchronous wrapper for connect"""
    bt = M25BluetoothBLE(address=address, key=key)
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
        bt = connect_device(args.address, timeout=args.timeout)
        if bt:
            print("Connection successful!")
            print("Use m25_ecs.py to interact with the device")
        else:
            sys.exit(1)
    
    elif args.command == "disconnect":
        bt = M25BluetoothBLE()
        asyncio.run(bt.disconnect())
    
    elif args.command == "status":
        state = load_state()
        if state:
            print(f"Status: Connected to {state.get('address', 'Unknown')}")
        else:
            print("Status: Not connected")


if __name__ == "__main__":
    main()
