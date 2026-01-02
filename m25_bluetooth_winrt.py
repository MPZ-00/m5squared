#!/usr/bin/env python3
"""
M25 Windows RFCOMM Bluetooth Module using WinRT

Native Windows Bluetooth Classic (RFCOMM/SPP) support via Windows Runtime APIs.
This provides proper Serial Port Profile communication like the Linux version.

Requires:
    pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Bluetooth.Rfcomm 
                winrt-Windows.Networking winrt-Windows.Networking.Sockets 
                winrt-Windows.Storage.Streams
"""

import asyncio
import sys
import time
from typing import Optional

try:
    from winrt.windows.devices.bluetooth import BluetoothDevice
    from winrt.windows.devices.bluetooth.rfcomm import RfcommServiceId
    from winrt.windows.networking.sockets import StreamSocket
    from winrt.windows.storage.streams import DataReader, DataWriter, InputStreamOptions
    HAS_WINRT = True
except ImportError:
    HAS_WINRT = False
    print("ERROR: WinRT Bluetooth not installed.", file=sys.stderr)
    print("Install with: pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Bluetooth.Rfcomm winrt-Windows.Networking winrt-Windows.Networking.Sockets winrt-Windows.Storage.Streams", file=sys.stderr)

try:
    from m25_crypto import M25Encryptor, M25Decryptor
    from m25_protocol import HEADER_MARKER
except ImportError:
    print("ERROR: m25_crypto.py or m25_protocol.py not found", file=sys.stderr)
    sys.exit(1)


def mac_to_u64(mac: str) -> int:
    """Convert MAC address string to uint64
    
    Args:
        mac: MAC address as "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF"
        
    Returns:
        MAC address as 64-bit integer
    """
    return int(mac.replace(":", "").replace("-", ""), 16)


class WinRTBluetoothConnection:
    """Windows RFCOMM Bluetooth connection using WinRT APIs"""
    
    def __init__(self, address: str, key: bytes, name: str = "wheel", debug: bool = False):
        """Initialize connection
        
        Args:
            address: Bluetooth MAC address (e.g., "AA:BB:CC:DD:EE:FF")
            key: Encryption key (16 bytes)
            name: Friendly name for logging
            debug: Enable debug output
        """
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        
        self.socket: Optional[StreamSocket] = None
        self.reader: Optional[DataReader] = None
        self.writer: Optional[DataWriter] = None
        self.device: Optional[BluetoothDevice] = None
        
        self.encryptor = M25Encryptor(key)
        self.decryptor = M25Decryptor(key)
        
        self._connected = False
        
    async def connect_async(self, channel: int = 6) -> bool:
        """Connect to device via RFCOMM
        
        Args:
            channel: RFCOMM channel (default: 6 for M25 SPP)
            
        Returns:
            True if connected successfully
        """
        if not HAS_WINRT:
            return False
            
        try:
            # Convert MAC to uint64
            mac_int = mac_to_u64(self.address)
            
            if self.debug:
                print(f"[{self.name}] Connecting to {self.address} (0x{mac_int:012X})...", file=sys.stderr)
            
            # Get Bluetooth device
            self.device = await BluetoothDevice.from_bluetooth_address_async(mac_int)
            
            if not self.device:
                print(f"[{self.name}] Failed to get Bluetooth device", file=sys.stderr)
                return False
                
            # Get RFCOMM services
            rfcomm_services = await self.device.get_rfcomm_services_async()
            
            if not rfcomm_services or not rfcomm_services.services:
                print(f"[{self.name}] No RFCOMM services found", file=sys.stderr)
                return False
            
            # Use Serial Port service (or first available)
            service = None
            for svc in rfcomm_services.services:
                if self.debug:
                    print(f"[{self.name}] Found service: {svc.service_id}", file=sys.stderr)
                # Try to find SPP service, or use first available
                if service is None:
                    service = svc
            
            if not service:
                print(f"[{self.name}] No suitable RFCOMM service found", file=sys.stderr)
                return False
                
            # Create socket and connect
            self.socket = StreamSocket()
            await self.socket.connect_async(
                service.connection_host_name,
                service.connection_service_name
            )
            
            # Set up reader and writer
            self.reader = DataReader(self.socket.input_stream)
            self.reader.input_stream_options = InputStreamOptions.PARTIAL
            
            self.writer = DataWriter(self.socket.output_stream)
            
            self._connected = True
            
            if self.debug:
                print(f"[{self.name}] Connected successfully", file=sys.stderr)
                
            return True
            
        except Exception as e:
            print(f"[{self.name}] Connection error: {e}", file=sys.stderr)
            return False
    
    def connect(self, channel: int = 6):
        """Synchronous wrapper for connect_async"""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            return loop.run_until_complete(self.connect_async(channel))
        finally:
            loop.close()
    
    async def disconnect_async(self):
        """Disconnect from device"""
        self._connected = False
        
        if self.reader:
            self.reader.detach_stream()
            self.reader = None
            
        if self.writer:
            self.writer.detach_stream()
            self.writer = None
            
        if self.socket:
            self.socket.close()
            self.socket = None
            
        if self.device:
            self.device.close()
            self.device = None
            
        if self.debug:
            print(f"[{self.name}] Disconnected", file=sys.stderr)
    
    def disconnect(self):
        """Synchronous wrapper for disconnect_async"""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self.disconnect_async())
        finally:
            loop.close()
    
    async def send_async(self, data: bytes) -> bool:
        """Send raw data
        
        Args:
            data: Data to send
            
        Returns:
            True if sent successfully
        """
        if not self._connected or not self.writer:
            return False
            
        try:
            self.writer.write_bytes(list(data))
            await self.writer.store_async()
            return True
        except Exception as e:
            if self.debug:
                print(f"[{self.name}] Send error: {e}", file=sys.stderr)
            return False
    
    async def receive_async(self, timeout: float = 2.0) -> Optional[bytes]:
        """Receive data with timeout
        
        Args:
            timeout: Timeout in seconds
            
        Returns:
            Received data or None
        """
        if not self._connected or not self.reader:
            return None
            
        buffer = b''
        start_time = time.time()
        
        try:
            while True:
                elapsed = time.time() - start_time
                if elapsed >= timeout:
                    break
                
                # Try to read available data
                try:
                    # Load with short timeout
                    load_task = asyncio.create_task(self.reader.load_async(1024))
                    bytes_read = await asyncio.wait_for(load_task, timeout=max(0.1, timeout - elapsed))
                    
                    if bytes_read > 0:
                        data = bytes(self.reader.read_buffer(bytes_read))
                        buffer += data
                        
                        # Check if we have a complete frame
                        if len(buffer) >= 3 and buffer[0] == HEADER_MARKER:
                            frame_length = (buffer[1] << 8) | buffer[2]
                            if len(buffer) >= frame_length + 1:
                                break
                    else:
                        if buffer:
                            break
                        await asyncio.sleep(0.01)
                        
                except asyncio.TimeoutError:
                    if buffer:
                        break
                    continue
                    
            return buffer if buffer else None
            
        except Exception as e:
            if self.debug:
                print(f"[{self.name}] Receive error: {e}", file=sys.stderr)
            return buffer if buffer else None
    
    def send_packet(self, spp_data: bytes) -> bytes:
        """Encrypt and send SPP packet (synchronous for compatibility)
        
        Args:
            spp_data: Unencrypted SPP packet data
            
        Returns:
            Encrypted data that was sent
        """
        encrypted = self.encryptor.encrypt_packet(spp_data)
        
        if self.debug:
            print(f"  TX [{self.name}]: {encrypted.hex()}", file=sys.stderr)
            print(f"      SPP: {spp_data.hex()}", file=sys.stderr)
        
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self.send_async(encrypted))
        finally:
            loop.close()
            
        return encrypted
    
    def receive(self, timeout: float = 1.0) -> Optional[bytes]:
        """Receive data (synchronous for compatibility)
        
        Args:
            timeout: Timeout in seconds
            
        Returns:
            Received data or None
        """
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            return loop.run_until_complete(self.receive_async(timeout))
        finally:
            loop.close()
    
    def transact(self, spp_data: bytes, timeout: float = 1.0) -> Optional[bytes]:
        """Send packet and receive decrypted response (synchronous)
        
        This matches the BluetoothConnection API from m25_spp.py
        
        Args:
            spp_data: Unencrypted SPP packet to send
            timeout: Receive timeout in seconds
            
        Returns:
            Decrypted SPP response data or None
        """
        self.send_packet(spp_data)
        encrypted_response = self.receive(timeout)
        
        if encrypted_response:
            decrypted = self.decryptor.decrypt_packet(encrypted_response)
            if self.debug:
                print(f"  RX [{self.name}]: {encrypted_response.hex()}", file=sys.stderr)
                print(f"      SPP: {decrypted.hex()}", file=sys.stderr)
            return decrypted
            
        return None


async def connect_parallel(left_addr: str, left_key: bytes, 
                          right_addr: str, right_key: bytes,
                          debug: bool = False):
    """Connect to both wheels in parallel
    
    Args:
        left_addr: Left wheel MAC address
        left_key: Left wheel encryption key
        right_addr: Right wheel MAC address
        right_key: Right wheel encryption key
        debug: Enable debug output
        
    Returns:
        (left_conn, right_conn) tuple, or (None, None) on failure
    """
    left_conn = WinRTBluetoothConnection(left_addr, left_key, "Left", debug)
    right_conn = WinRTBluetoothConnection(right_addr, right_key, "Right", debug)
    
    # Connect both in parallel
    results = await asyncio.gather(
        left_conn.connect_async(),
        right_conn.connect_async(),
        return_exceptions=True
    )
    
    left_ok = results[0] if not isinstance(results[0], Exception) else False
    right_ok = results[1] if not isinstance(results[1], Exception) else False
    
    if not left_ok:
        print("Failed to connect to left wheel", file=sys.stderr)
        return None, None
        
    if not right_ok:
        print("Failed to connect to right wheel", file=sys.stderr)
        return None, None
        
    return left_conn, right_conn


if __name__ == "__main__":
    print("WinRT RFCOMM Bluetooth Module")
    print("Use m25_ecs.py or m25_gui.py for wheelchair control")
