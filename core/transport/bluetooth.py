"""
Bluetooth Transport - Wraps existing m25_ Bluetooth code.

Uses BLE (Bleak) cross-platform - power-efficient with notification support.
Falls back to SPP/RFCOMM on Linux if BLE not available.
"""

import asyncio
import logging
from typing import Optional
import struct
import sys

from core.types import CommandFrame, VehicleState

# Try BLE first (cross-platform, power-efficient, supports notifications)
try:
    from m25_bluetooth_ble import M25BluetoothBLE as BluetoothConnection
    BLUETOOTH_TYPE = "BLE"
except ImportError:
    # Fallback to platform-specific
    if sys.platform == "win32":
        from m25_bluetooth_windows import M25WindowsBluetooth as BluetoothConnection
        BLUETOOTH_TYPE = "BLE-Legacy"
    else:
        from m25_spp import BluetoothConnection
        BLUETOOTH_TYPE = "RFCOMM"

from m25_spp import PacketBuilder
from m25_protocol_data import (
    SYSTEM_MODE_CONNECT,
    DRIVE_MODE_NORMAL,
    DRIVE_MODE_REMOTE,
)
from m25_ecs import ECSRemote, ResponseParser
from m25_utils import parse_key


logger = logging.getLogger(__name__)


class BluetoothTransport:
    """
    Transport adapter for M25 Bluetooth wheels.
    
    Wraps the existing m25_spp.BluetoothConnection to work with
    the new core architecture interfaces.
    """
    
    def __init__(self, debug: bool = False) -> None:
        """
        Initialize Bluetooth transport.
        
        Args:
            debug: Enable debug logging
        """
        self._debug = debug
        self._left_conn: Optional[BluetoothConnection] = None
        self._right_conn: Optional[BluetoothConnection] = None
        self._left_builder: Optional[PacketBuilder] = None
        self._right_builder: Optional[PacketBuilder] = None
        self._connected = False
        
        # Cache for vehicle state
        self._vehicle_state: Optional[VehicleState] = None
    
    async def connect(
        self,
        left_addr: str,
        right_addr: str,
        left_key: bytes,
        right_key: bytes
    ) -> bool:
        """
        Connect to both wheels via Bluetooth.
        
        Args:
            left_addr: Left wheel MAC address
            right_addr: Right wheel MAC address
            left_key: Left wheel encryption key (16 bytes)
            right_key: Right wheel encryption key (16 bytes)
        
        Returns:
            True if both connections successful
        """
        try:
            logger.info(f"Connecting to left wheel: {left_addr}")
            self._left_conn = BluetoothConnection(
                left_addr, left_key, name="left", debug=self._debug
            )
            
            # Use async connect for WinRT
            if sys.platform == "win32":
                success = await self._left_conn.connect_async(6)
                if not success:
                    logger.error("Left wheel connection failed")
                    return False
            else:
                loop = asyncio.get_event_loop()
                await loop.run_in_executor(None, self._left_conn.connect, 6)
            
            logger.info(f"Connecting to right wheel: {right_addr}")
            self._right_conn = BluetoothConnection(
                right_addr, right_key, name="right", debug=self._debug
            )
            
            if sys.platform == "win32":
                success = await self._right_conn.connect_async(6)
                if not success:
                    logger.error("Right wheel connection failed")
                    return False
            else:
                loop = asyncio.get_event_loop()
                await loop.run_in_executor(None, self._right_conn.connect, 6)
            
            # Initialize packet builders
            self._left_builder = PacketBuilder()
            self._right_builder = PacketBuilder()
            
            # Initialize connection with SYSTEM_MODE_CONNECT
            logger.info("Initializing connection...")
            pkt_left = self._left_builder.build_write_system_mode(SYSTEM_MODE_CONNECT)
            pkt_right = self._right_builder.build_write_system_mode(SYSTEM_MODE_CONNECT)
            
            # Use async send for WinRT
            if sys.platform == "win32":
                await self._left_conn.send_async(self._left_conn.encryptor.encrypt(pkt_left))
                await self._right_conn.send_async(self._right_conn.encryptor.encrypt(pkt_right))
            else:
                self._left_conn.send_packet(pkt_left)
                self._right_conn.send_packet(pkt_right)
            
            await asyncio.sleep(0.3)  # Wait for ACK
            
            # Enable remote control mode
            logger.info("Enabling remote control mode...")
            pkt_left = self._left_builder.build_write_drive_mode(DRIVE_MODE_REMOTE)
            pkt_right = self._right_builder.build_write_drive_mode(DRIVE_MODE_REMOTE)
            
            if sys.platform == "win32":
                await self._left_conn.send_async(self._left_conn.encryptor.encrypt(pkt_left))
                await self._right_conn.send_async(self._right_conn.encryptor.encrypt(pkt_right))
            else:
                self._left_conn.send_packet(pkt_left)
                self._right_conn.send_packet(pkt_right)
            
            await asyncio.sleep(0.3)
            
            self._connected = True
            logger.info("Connected successfully")
            return True
        
        except Exception as e:
            logger.error(f"Connection failed: {e}", exc_info=True)
            await self.disconnect()
            return False
    
    async def disconnect(self) -> None:
        """Disconnect from both wheels"""
        try:
            if self._connected:
                # Send stop command
                await self.send_command(CommandFrame.stop())
                
                # Disable remote mode
                if self._left_builder and self._right_builder:
                    logger.info("Disabling remote control mode...")
                    pkt_left = self._left_builder.build_write_drive_mode(DRIVE_MODE_NORMAL)
                    pkt_right = self._right_builder.build_write_drive_mode(DRIVE_MODE_NORMAL)
                    
                    if self._left_conn:
                        if sys.platform == "win32":
                            await self._left_conn.send_async(self._left_conn.encryptor.encrypt(pkt_left))
                        else:
                            self._left_conn.send_packet(pkt_left)
                    if self._right_conn:
                        if sys.platform == "win32":
                            await self._right_conn.send_async(self._right_conn.encryptor.encrypt(pkt_right))
                        else:
                            self._right_conn.send_packet(pkt_right)
                    
                    await asyncio.sleep(0.1)
            
            # Close connections
            if self._left_conn:
                if sys.platform == "win32":
                    await self._left_conn.disconnect_async()
                else:
                    self._left_conn.disconnect()
                self._left_conn = None
            
            if self._right_conn:
                if sys.platform == "win32":
                    await self._right_conn.disconnect_async()
                else:
                    self._right_conn.disconnect()
                self._right_conn = None
            
            self._connected = False
            logger.info("Disconnected")
        
        except Exception as e:
            logger.error(f"Error during disconnect: {e}", exc_info=True)
    
    async def send_command(self, frame: CommandFrame) -> bool:
        """
        Send command frame to both wheels.
        
        Args:
            frame: Command frame with left/right speeds
        
        Returns:
            True if sent successfully
        """
        if not self._connected or not self._left_conn or not self._right_conn:
            return False
        
        try:
            # Left wheel needs negative speed (mounted on opposite side)
            left_speed = -frame.left_speed
            right_speed = frame.right_speed
            
            # Build remote speed packets
            pkt_left = self._left_builder.build_write_remote_speed(left_speed)
            pkt_right = self._right_builder.build_write_remote_speed(right_speed)
            
            # Send to both wheels
            if sys.platform == "win32":
                await self._left_conn.send_async(self._left_conn.encryptor.encrypt(pkt_left))
                await self._right_conn.send_async(self._right_conn.encryptor.encrypt(pkt_right))
            else:
                loop = asyncio.get_event_loop()
                await loop.run_in_executor(None, self._left_conn.send_packet, pkt_left)
                await loop.run_in_executor(None, self._right_conn.send_packet, pkt_right)
            
            logger.debug(f"Sent command: L={left_speed:+4d} R={right_speed:+4d}")
            return True
        
        except Exception as e:
            logger.error(f"Failed to send command: {e}")
            return False
    
    async def read_state(self) -> Optional[VehicleState]:
        """
        Read current vehicle state.
        
        For now, returns cached state. Could be extended to actively
        query battery levels, errors, etc. using ECSRemote.
        
        Returns:
            Current vehicle state or None
        """
        # TODO: Implement actual state reading using ECSRemote
        # For now, return a placeholder
        if self._connected:
            if self._vehicle_state is None:
                self._vehicle_state = VehicleState(
                    battery_left=85,
                    battery_right=83,
                    connected=True
                )
            return self._vehicle_state
        return None
    
    @property
    def is_connected(self) -> bool:
        """Check if connected to both wheels"""
        return self._connected
