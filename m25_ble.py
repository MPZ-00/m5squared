"""
M25 BLE Connection - Windows-compatible Bluetooth using Bleak

Uses BLE (Bluetooth Low Energy) instead of RFCOMM since Windows has issues
with RFCOMM connections to M25 wheels (auto-disconnect after pairing).
"""

import asyncio
import sys
import logging
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
    HAS_BLEAK = True
except ImportError:
    HAS_BLEAK = False

from m25_crypto import M25Encryptor, M25Decryptor


logger = logging.getLogger(__name__)


# Nordic UART Service (common for BLE serial communication)
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # Write
UART_RX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # Notify


class M25BLEConnection:
    """BLE connection to M25 wheel with encryption"""
    
    def __init__(self, address: str, key: bytes, name: str = "wheel", debug: bool = False):
        """
        Initialize BLE connection
        
        Args:
            address: Bluetooth address (MAC on Windows, UUID on macOS)
            key: Encryption key (16 bytes)
            name: Friendly name for logging
            debug: Enable debug output
        """
        if not HAS_BLEAK:
            raise RuntimeError("bleak not installed. Install with: pip install bleak")
        
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        
        self.client: Optional[BleakClient] = None
        self._connected = False
        
        self.encryptor = M25Encryptor(key)
        self.decryptor = M25Decryptor(key)
        
        # Characteristics for sending/receiving
        self._tx_char = None
        self._rx_char = None
        
        # Buffer for received data
        self._rx_buffer = bytearray()
        self._rx_event = asyncio.Event()
    
    async def connect_async(self, timeout: float = 10.0) -> bool:
        """
        Connect to M25 device via BLE
        
        Args:
            timeout: Connection timeout in seconds
            
        Returns:
            True if connected successfully
        """
        try:
            if self.debug:
                logger.info(f"[{self.name}] Scanning for device {self.address}...")
            
            # Create client
            self.client = BleakClient(self.address, timeout=timeout)
            
            if self.debug:
                logger.info(f"[{self.name}] Connecting...")
            
            # Connect
            await self.client.connect()
            
            if not self.client.is_connected:
                logger.error(f"[{self.name}] Connection failed")
                return False
            
            if self.debug:
                logger.info(f"[{self.name}] Connected, discovering services...")
            
            # Get services
            services = self.client.services
            
            if self.debug:
                logger.info(f"[{self.name}] Found {len(services)} services")
                for service in services:
                    logger.debug(f"  Service: {service.uuid}")
                    for char in service.characteristics:
                        logger.debug(f"    Char: {char.uuid} - {char.properties}")
            
            # Find UART service or first writable/readable characteristics
            uart_service = self.client.services.get_service(UART_SERVICE_UUID)
            
            if uart_service:
                # Use Nordic UART Service
                self._tx_char = UART_TX_CHAR_UUID
                self._rx_char = UART_RX_CHAR_UUID
                if self.debug:
                    logger.info(f"[{self.name}] Using Nordic UART Service")
            else:
                # Find first writable/readable characteristics
                for service in services:
                    for char in service.characteristics:
                        if "write" in char.properties or "write-without-response" in char.properties:
                            if self._tx_char is None:
                                self._tx_char = char.uuid
                                if self.debug:
                                    logger.info(f"[{self.name}] TX char: {char.uuid}")
                        
                        if "notify" in char.properties or "read" in char.properties:
                            if self._rx_char is None:
                                self._rx_char = char.uuid
                                if self.debug:
                                    logger.info(f"[{self.name}] RX char: {char.uuid}")
            
            if not self._tx_char:
                logger.error(f"[{self.name}] No writable characteristic found")
                await self.disconnect_async()
                return False
            
            # Start notifications if available
            if self._rx_char:
                try:
                    await self.client.start_notify(self._rx_char, self._notification_handler)
                    if self.debug:
                        logger.info(f"[{self.name}] Notifications enabled")
                except Exception as e:
                    logger.warning(f"[{self.name}] Could not enable notifications: {e}")
            
            self._connected = True
            logger.info(f"[{self.name}] BLE connection established")
            return True
            
        except Exception as e:
            logger.error(f"[{self.name}] Connection error: {e}")
            if self.client:
                try:
                    await self.client.disconnect()
                except:
                    pass
            return False
    
    def _notification_handler(self, sender, data: bytearray):
        """Handle incoming BLE notifications"""
        self._rx_buffer.extend(data)
        self._rx_event.set()
        
        if self.debug:
            logger.debug(f"[{self.name}] RX: {len(data)} bytes")
    
    async def send_async(self, data: bytes) -> bool:
        """
        Send encrypted data to device
        
        Args:
            data: Encrypted packet data
            
        Returns:
            True if sent successfully
        """
        if not self._connected or not self.client:
            return False
        
        try:
            await self.client.write_gatt_char(self._tx_char, data, response=False)
            
            if self.debug:
                logger.debug(f"[{self.name}] TX: {len(data)} bytes")
            
            return True
            
        except Exception as e:
            logger.error(f"[{self.name}] Send error: {e}")
            return False
    
    async def disconnect_async(self):
        """Disconnect from device"""
        self._connected = False
        
        if self.client:
            try:
                if self.client.is_connected:
                    await self.client.disconnect()
                    if self.debug:
                        logger.info(f"[{self.name}] Disconnected")
            except Exception as e:
                logger.error(f"[{self.name}] Disconnect error: {e}")
            finally:
                self.client = None
    
    def is_connected(self) -> bool:
        """Check if currently connected"""
        return self._connected and self.client and self.client.is_connected
    
    # Synchronous wrappers (for compatibility, but should use async methods)
    def connect(self, timeout: float = 10.0) -> bool:
        """Synchronous connect (not recommended - use connect_async)"""
        return asyncio.run(self.connect_async(timeout))
    
    def disconnect(self):
        """Synchronous disconnect (not recommended - use disconnect_async)"""
        asyncio.run(self.disconnect_async())
