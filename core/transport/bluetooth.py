"""
Bluetooth Transport - Wraps existing m25_ Bluetooth code.

Uses BLE (Bleak) cross-platform - power-efficient with notification support.
Falls back to SPP/RFCOMM on Linux if BLE not available.
"""

import asyncio
import logging
from typing import Optional, Any
import struct
import sys

from core.types import CommandFrame, VehicleState

try:
    from m25_bluetooth_ble import M25BluetoothBLE
    try:
        from m25_bluetooth_ble import detect_m25_ble_profile
    except ImportError:
        detect_m25_ble_profile = None
    HAS_BLE = True
except ImportError:
    M25BluetoothBLE = None
    detect_m25_ble_profile = None
    HAS_BLE = False

try:
    from m25_spp import BluetoothConnection as RFCOMMConnection, PacketBuilder
    HAS_RFCOMM = True
except ImportError:
    RFCOMMConnection = None
    PacketBuilder = None
    HAS_RFCOMM = False

from m25_transport import (
    TRANSPORT_AUTO,
    TRANSPORT_BLE,
    TRANSPORT_RFCOMM,
    normalize_m25_version,
    preferred_transport_for_version,
)

BLUETOOTH_TYPE = "BLE" if HAS_BLE else ("RFCOMM" if HAS_RFCOMM else "Unavailable")
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
    
    def __init__(self, debug: bool = False, force_rfcomm: bool = False, m25_version: str = "auto") -> None:
        """
        Initialize Bluetooth transport.
        
        Args:
            debug: Enable debug logging
            force_rfcomm: Force RFCOMM/SPP instead of BLE (uses more power, not recommended)
        """
        self._debug = debug
        self._left_conn: Optional[Any] = None
        self._right_conn: Optional[Any] = None
        self._left_transport = TRANSPORT_AUTO
        self._right_transport = TRANSPORT_AUTO
        self._left_builder: Optional[Any] = None
        self._right_builder: Optional[Any] = None
        self._connected = False

        requested_version = normalize_m25_version(m25_version)
        self._bluetooth_type = preferred_transport_for_version(requested_version)

        # Backward-compatible override.
        if force_rfcomm:
            self._bluetooth_type = TRANSPORT_RFCOMM
            if HAS_BLE:
                logger.warning("Forcing RFCOMM mode - uses more power than BLE")

        if self._bluetooth_type == TRANSPORT_BLE and not HAS_BLE:
            raise RuntimeError("BLE requested but m25_bluetooth_ble.py is not available")
        if self._bluetooth_type == TRANSPORT_RFCOMM and not HAS_RFCOMM:
            raise RuntimeError("RFCOMM requested but m25_spp.py is not available")
        
        # Cache for vehicle state
        self._vehicle_state: Optional[VehicleState] = None
        
        logger.info(f"Bluetooth transport mode: {self._bluetooth_type}")

    async def _select_transport(self, address: str) -> str:
        """Choose BLE or RFCOMM for one wheel."""
        if self._bluetooth_type != TRANSPORT_AUTO:
            return self._bluetooth_type

        if HAS_BLE and detect_m25_ble_profile is not None:
            profile = await detect_m25_ble_profile(address, timeout=5)
            if profile == "M25V2" or (profile and profile.startswith("Fake")):
                return TRANSPORT_BLE
            if profile == "M25V1":
                return TRANSPORT_RFCOMM

        if HAS_RFCOMM:
            return TRANSPORT_RFCOMM
        if HAS_BLE:
            return TRANSPORT_BLE
        raise RuntimeError("No supported Bluetooth transport available")

    def _build_connection(self, address: str, key: bytes, name: str, transport_kind: str):
        """Instantiate the appropriate connection class."""
        if transport_kind == TRANSPORT_BLE:
            if M25BluetoothBLE is None:
                raise RuntimeError("BLE transport requested but unavailable")
            return M25BluetoothBLE(address, key, name=name, debug=self._debug)
        if RFCOMMConnection is None:
            raise RuntimeError("RFCOMM transport requested but unavailable")
        return RFCOMMConnection(address, key, name=name, debug=self._debug)
    
    async def _connect_wheel(self, conn, transport_kind: str, channel: int = 6) -> bool:
        """Connect to a single wheel based on Bluetooth type"""
        if transport_kind == TRANSPORT_BLE:
            return await conn.connect_async(timeout=10)
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, conn.connect, channel)
        return True
    
    async def _send_packet(self, conn, packet: bytes, transport_kind: str) -> None:
        """Send a packet to a wheel based on Bluetooth type"""
        if transport_kind == TRANSPORT_BLE:
            await conn.send_async(conn.encryptor.encrypt(packet))
            return
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, conn.send_packet, packet)
    
    async def _disconnect_wheel(self, conn, transport_kind: str) -> None:
        """Disconnect from a single wheel based on Bluetooth type"""
        if transport_kind == TRANSPORT_BLE:
            await conn.disconnect_async()
            return
        conn.disconnect()
    
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
            self._left_transport = await self._select_transport(left_addr)
            self._right_transport = await self._select_transport(right_addr)

            logger.info(f"Connecting to left wheel: {left_addr}")
            logger.info(f"Left transport: {self._left_transport}")
            self._left_conn = self._build_connection(left_addr, left_key, "left", self._left_transport)
            
            # Connect to left wheel
            success = await self._connect_wheel(self._left_conn, self._left_transport)
            if not success:
                logger.error("Left wheel connection failed")
                return False
            
            logger.info(f"Connecting to right wheel: {right_addr}")
            logger.info(f"Right transport: {self._right_transport}")
            self._right_conn = self._build_connection(right_addr, right_key, "right", self._right_transport)
            
            # Connect to right wheel
            success = await self._connect_wheel(self._right_conn, self._right_transport)
            if not success:
                logger.error("Right wheel connection failed")
                return False
            
            # Initialize packet builders
            if PacketBuilder is None:
                raise RuntimeError("PacketBuilder unavailable (m25_spp import failed)")
            self._left_builder = PacketBuilder()
            self._right_builder = PacketBuilder()
            assert self._left_builder is not None
            assert self._right_builder is not None
            
            # Initialize connection with SYSTEM_MODE_CONNECT
            logger.info("Initializing connection...")
            pkt_left = self._left_builder.build_write_system_mode(SYSTEM_MODE_CONNECT)
            pkt_right = self._right_builder.build_write_system_mode(SYSTEM_MODE_CONNECT)
            
            # Send initialization packets
            await self._send_packet(self._left_conn, pkt_left, self._left_transport)
            await self._send_packet(self._right_conn, pkt_right, self._right_transport)
            
            await asyncio.sleep(0.3)  # Wait for ACK
            
            # Enable remote control mode
            logger.info("Enabling remote control mode...")
            pkt_left = self._left_builder.build_write_drive_mode(DRIVE_MODE_REMOTE)
            pkt_right = self._right_builder.build_write_drive_mode(DRIVE_MODE_REMOTE)
            
            await self._send_packet(self._left_conn, pkt_left, self._left_transport)
            await self._send_packet(self._right_conn, pkt_right, self._right_transport)
            
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
                        await self._send_packet(self._left_conn, pkt_left, self._left_transport)
                    if self._right_conn:
                        await self._send_packet(self._right_conn, pkt_right, self._right_transport)
                    
                    await asyncio.sleep(0.1)
            
            # Close connections
            if self._left_conn:
                await self._disconnect_wheel(self._left_conn, self._left_transport)
                self._left_conn = None
            
            if self._right_conn:
                await self._disconnect_wheel(self._right_conn, self._right_transport)
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
        if not self._left_builder or not self._right_builder:
            return False
        
        try:
            # Left wheel needs negative speed (mounted on opposite side)
            left_speed = -frame.left_speed
            right_speed = frame.right_speed
            
            # Build remote speed packets
            pkt_left = self._left_builder.build_write_remote_speed(left_speed)
            pkt_right = self._right_builder.build_write_remote_speed(right_speed)
            
            # Send to both wheels
            await self._send_packet(self._left_conn, pkt_left, self._left_transport)
            await self._send_packet(self._right_conn, pkt_right, self._right_transport)
            
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
