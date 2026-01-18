#!/usr/bin/env python3
"""
Mock M25 Wheel Simulator - Hardware Emulation for Testing

Simulates an M25 wheel as a Bluetooth server with full encryption support.
Useful for testing applications without physical hardware.

Features:
- Full AES-128-CBC encryption/decryption
- Responds to common M25 protocol commands
- Simulates battery status, speed, and other parameters
- Configurable response behavior
- Can simulate errors and timeouts

Usage:
    python mock_wheel_simulator.py --key <hex_key> --mode bluetooth
    python mock_wheel_simulator.py --mode socket --port 5000  # TCP socket mode
"""

import asyncio
import logging
import sys
import struct
import argparse
from typing import Optional, Dict, Any
from dataclasses import dataclass, field
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from m25_crypto import M25Encryptor, M25Decryptor
from m25_protocol import DEFAULT_USB_KEY
from m25_protocol_data import (
    PROTOCOL_ID_STANDARD,
    POS_PROTOCOL_ID, POS_TELEGRAM_ID, POS_SOURCE_ID, POS_DEST_ID,
    POS_SERVICE_ID, POS_PARAM_ID, POS_PAYLOAD,
    MIN_SPP_PACKET_SIZE,
    
    # Source/Dest IDs
    SRC_ID_M25_WHEEL_LEFT, SRC_ID_M25_WHEEL_RIGHT, SRC_ID_M25_WHEEL_COMMON,
    DEST_ID_M25_WHEEL_COMMON, DEST_ID_SMARTPHONE, DEST_ID_ECS,
    
    # Service IDs
    SERVICE_ID_APP_MGMT, SERVICE_ID_ACTOR_MOTOR, SERVICE_ID_BATT_MGMT,
    SERVICE_ID_VERSION_MGMT, SERVICE_ID_STATS,
    
    # Parameter IDs
    PARAM_ID_ACK,
    PARAM_ID_WRITE_SYSTEM_MODE, PARAM_ID_READ_SYSTEM_MODE,
    PARAM_ID_WRITE_DRIVE_MODE, PARAM_ID_READ_DRIVE_MODE, PARAM_ID_STATUS_DRIVE_MODE,
    PARAM_ID_WRITE_REMOTE_SPEED,
    PARAM_ID_READ_CURRENT_SPEED, PARAM_ID_STATUS_CURRENT_SPEED,
    PARAM_ID_WRITE_ASSIST_LEVEL, PARAM_ID_READ_ASSIST_LEVEL, PARAM_ID_STATUS_ASSIST_LEVEL,
    
    # NACK codes
    NACK_GENERAL, NACK_SID, NACK_PID,
    
    # Service-specific parameter IDs
    SERVICE_NAMES, SOURCE_NAMES, DEST_NAMES
)

# Try to import Bluetooth RFCOMM server support
try:
    from bluetooth_server import RFCOMMServer, HAS_PYBLUEZ
    HAS_BLUETOOTH = True
except ImportError:
    HAS_BLUETOOTH = False
    HAS_PYBLUEZ = False
    RFCOMMServer = None


logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


@dataclass
class WheelState:
    """Simulated wheel state"""
    # Battery
    battery_soc: int = 85  # State of charge (0-100%)
    battery_voltage: float = 36.5  # Volts
    battery_current: float = 0.0  # Amperes
    battery_temperature: int = 25  # Celsius
    
    # Motor
    motor_speed: int = 0  # Current motor speed (-100 to +100)
    target_speed: int = 0  # Target speed from remote control
    motor_temperature: int = 30  # Celsius
    
    # Drive settings
    system_mode: int = 0x01  # 0x01=Connected, 0x02=Standby
    drive_mode: int = 0x01  # 0x01=Normal, 0x02=Sport, etc.
    assist_level: int = 5  # 1-10
    
    # Statistics
    odometer: int = 12345  # Total distance in meters
    trip_distance: int = 1234  # Trip distance in meters
    operating_hours: int = 450  # Total operating hours
    
    # Counters
    telegram_counter: int = 0
    command_count: int = 0
    
    def next_telegram_id(self) -> int:
        """Get next telegram ID"""
        tid = self.telegram_counter
        self.telegram_counter = (self.telegram_counter + 1) & 0xFF
        return tid


class MockWheelSimulator:
    """
    Simulates an M25 wheel with encryption.
    
    Acts as the server/peripheral side, responding to commands
    from the client (smartphone/controller).
    """
    
    def __init__(
        self,
        key: bytes = DEFAULT_USB_KEY,
        wheel_id: int = SRC_ID_M25_WHEEL_LEFT,
        debug: bool = False
    ):
        """
        Initialize wheel simulator.
        
        Args:
            key: 16-byte AES encryption key
            wheel_id: Wheel source ID (LEFT, RIGHT, or COMMON)
            debug: Enable debug logging
        """
        if len(key) != 16:
            raise ValueError(f"Key must be 16 bytes, got {len(key)}")
        
        self.key = key
        self.wheel_id = wheel_id
        self.debug = debug
        
        self.encryptor = M25Encryptor(key)
        self.decryptor = M25Decryptor(key)
        
        self.state = WheelState()
        
        # Command handlers mapping
        self.handlers: Dict[tuple, Any] = {
            # APP_MGMT
            (SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_SYSTEM_MODE): self.handle_write_system_mode,
            (SERVICE_ID_APP_MGMT, PARAM_ID_READ_SYSTEM_MODE): self.handle_read_system_mode,
            (SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_MODE): self.handle_write_drive_mode,
            (SERVICE_ID_APP_MGMT, PARAM_ID_READ_DRIVE_MODE): self.handle_read_drive_mode,
            (SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_REMOTE_SPEED): self.handle_write_remote_speed,
            (SERVICE_ID_APP_MGMT, PARAM_ID_READ_CURRENT_SPEED): self.handle_read_current_speed,
            (SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_ASSIST_LEVEL): self.handle_write_assist_level,
            (SERVICE_ID_APP_MGMT, PARAM_ID_READ_ASSIST_LEVEL): self.handle_read_assist_level,
        }
        
        logger.info(f"Mock Wheel Simulator initialized: {SOURCE_NAMES.get(wheel_id, f'ID_{wheel_id}')}")
        logger.info(f"Encryption key: {key.hex()}")
    
    def process_packet(self, encrypted_packet: bytes) -> Optional[bytes]:
        """
        Process received encrypted packet and generate encrypted response.
        
        Args:
            encrypted_packet: Full encrypted M25 packet
            
        Returns:
            Encrypted response packet or None
        """
        # Decrypt incoming packet
        decrypted = self.decryptor.decrypt(encrypted_packet)
        if decrypted is None:
            logger.warning("Failed to decrypt packet")
            return None
        
        if len(decrypted) < MIN_SPP_PACKET_SIZE:
            logger.warning(f"Packet too short: {len(decrypted)} bytes")
            return None
        
        # Parse packet header
        protocol_id = decrypted[POS_PROTOCOL_ID]
        telegram_id = decrypted[POS_TELEGRAM_ID]
        source_id = decrypted[POS_SOURCE_ID]
        dest_id = decrypted[POS_DEST_ID]
        service_id = decrypted[POS_SERVICE_ID]
        param_id = decrypted[POS_PARAM_ID]
        payload = decrypted[POS_PAYLOAD:] if len(decrypted) > POS_PAYLOAD else b''
        
        if self.debug:
            logger.debug(f"RX: Proto={protocol_id:02X} Tel={telegram_id:02X} "
                        f"Src={SOURCE_NAMES.get(source_id, f'{source_id:02X}')} "
                        f"Dst={DEST_NAMES.get(dest_id, f'{dest_id:02X}')} "
                        f"Srv={SERVICE_NAMES.get(service_id, f'{service_id:02X}')} "
                        f"Param={param_id:02X} Payload={payload.hex()}")
        
        self.state.command_count += 1
        
        # Check if we're the destination
        if dest_id not in [self.wheel_id, DEST_ID_M25_WHEEL_COMMON, 15]:  # 15 = broadcast
            logger.debug(f"Packet not for us (dest={dest_id}, we are {self.wheel_id})")
            return None
        
        # Find handler
        handler_key = (service_id, param_id)
        handler = self.handlers.get(handler_key)
        
        if handler:
            response_spp = handler(telegram_id, source_id, payload)
        else:
            # Unknown command - send NACK
            logger.warning(f"Unknown command: Service={service_id:02X} Param={param_id:02X}")
            response_spp = self.build_nack(telegram_id, source_id, NACK_PID)
        
        # Encrypt response
        if response_spp:
            encrypted_response = self.encryptor.encrypt(response_spp)
            if self.debug:
                logger.debug(f"TX: {response_spp.hex()} -> {encrypted_response.hex()}")
            return encrypted_response
        
        return None
    
    def build_response(
        self,
        telegram_id: int,
        dest_id: int,
        service_id: int,
        param_id: int,
        payload: bytes = b''
    ) -> bytes:
        """Build SPP response packet"""
        return bytes([
            PROTOCOL_ID_STANDARD,
            telegram_id,
            self.wheel_id,
            dest_id,
            service_id,
            param_id
        ]) + payload
    
    def build_ack(self, telegram_id: int, dest_id: int) -> bytes:
        """Build ACK response"""
        return self.build_response(telegram_id, dest_id, SERVICE_ID_APP_MGMT, PARAM_ID_ACK)
    
    def build_nack(self, telegram_id: int, dest_id: int, error_code: int) -> bytes:
        """Build NACK response"""
        return self.build_response(
            telegram_id, dest_id, SERVICE_ID_APP_MGMT, PARAM_ID_ACK, bytes([error_code])
        )
    
    # Command Handlers
    
    def handle_write_system_mode(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle WRITE_SYSTEM_MODE (0x01=Connect, 0x02=Standby)"""
        if len(payload) >= 1:
            mode = payload[0]
            self.state.system_mode = mode
            logger.info(f"System mode set to: 0x{mode:02X}")
        return self.build_ack(telegram_id, source_id)
    
    def handle_read_system_mode(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle READ_SYSTEM_MODE"""
        return self.build_response(
            telegram_id, source_id, SERVICE_ID_APP_MGMT,
            PARAM_ID_READ_SYSTEM_MODE + 1,  # STATUS = READ + 1
            bytes([self.state.system_mode])
        )
    
    def handle_write_drive_mode(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle WRITE_DRIVE_MODE"""
        if len(payload) >= 1:
            mode = payload[0]
            self.state.drive_mode = mode
            logger.info(f"Drive mode set to: 0x{mode:02X}")
        return self.build_ack(telegram_id, source_id)
    
    def handle_read_drive_mode(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle READ_DRIVE_MODE"""
        return self.build_response(
            telegram_id, source_id, SERVICE_ID_APP_MGMT,
            PARAM_ID_STATUS_DRIVE_MODE,
            bytes([self.state.drive_mode])
        )
    
    def handle_write_remote_speed(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle WRITE_REMOTE_SPEED - motor control command"""
        if len(payload) >= 2:
            # Remote speed is typically 2 bytes: signed int16
            speed = struct.unpack('>h', payload[0:2])[0]  # Big-endian signed short
            self.state.target_speed = speed
            
            # Simulate motor response (ramp towards target)
            if abs(speed) < 10:
                self.state.motor_speed = 0
            else:
                # Simple simulation
                self.state.motor_speed = int(speed * 0.8)  # 80% of target
            
            logger.info(f"Remote speed command: {speed} -> motor speed: {self.state.motor_speed}")
        
        return self.build_ack(telegram_id, source_id)
    
    def handle_read_current_speed(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle READ_CURRENT_SPEED"""
        # Return current motor speed as signed int16
        speed_bytes = struct.pack('>h', self.state.motor_speed)
        return self.build_response(
            telegram_id, source_id, SERVICE_ID_APP_MGMT,
            PARAM_ID_STATUS_CURRENT_SPEED,
            speed_bytes
        )
    
    def handle_write_assist_level(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle WRITE_ASSIST_LEVEL"""
        if len(payload) >= 1:
            level = payload[0]
            self.state.assist_level = max(1, min(10, level))  # Clamp to 1-10
            logger.info(f"Assist level set to: {self.state.assist_level}")
        return self.build_ack(telegram_id, source_id)
    
    def handle_read_assist_level(self, telegram_id: int, source_id: int, payload: bytes) -> bytes:
        """Handle READ_ASSIST_LEVEL"""
        return self.build_response(
            telegram_id, source_id, SERVICE_ID_APP_MGMT,
            PARAM_ID_STATUS_ASSIST_LEVEL,
            bytes([self.state.assist_level])
        )
    
    def get_state(self) -> WheelState:
        """Get current wheel state"""
        return self.state


class SocketServer:
    """TCP socket server for testing"""
    
    def __init__(self, simulator: MockWheelSimulator, port: int = 5000):
        self.simulator = simulator
        self.port = port
        self.server: Optional[asyncio.Server] = None
    
    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle connected client"""
        addr = writer.get_extra_info('peername')
        logger.info(f"Client connected: {addr}")
        
        try:
            while True:
                # Read enough data to get at least one packet
                # M25 packets are typically 37-300 bytes with byte stuffing
                data = await reader.read(512)
                if not data:
                    break
                
                logger.debug(f"Received {len(data)} bytes: {data[:50].hex()}...")
                
                # Process packet
                response = self.simulator.process_packet(data)
                
                if response:
                    writer.write(response)
                    await writer.drain()
                    logger.debug(f"Sent {len(response)} bytes")
        
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Error handling client: {e}", exc_info=True)
        finally:
            logger.info(f"Client disconnected: {addr}")
            writer.close()
            await writer.wait_closed()
    
    async def start(self):
        """Start the server"""
        self.server = await asyncio.start_server(
            self.handle_client, '127.0.0.1', self.port
        )
        
        addr = self.server.sockets[0].getsockname()
        logger.info(f"Socket server started on {addr[0]}:{addr[1]}")
        logger.info("Waiting for connections...")
        
        async with self.server:
            await self.server.serve_forever()


class BluetoothServer:
    """Bluetooth RFCOMM server wrapper"""
    
    def __init__(self, simulator: MockWheelSimulator, device_name: str = "e-motion M25 Left"):
        self.simulator = simulator
        self.device_name = device_name
        self.server: Optional[RFCOMMServer] = None
    
    def start(self):
        """Start the Bluetooth server (blocking)"""
        if not HAS_BLUETOOTH:
            logger.error("Bluetooth RFCOMM support not available")
            logger.info("Install: pip install pybluez")
            logger.info("On Linux: sudo apt-get install libbluetooth-dev python3-dev")
            logger.info("On Windows: pip install pybluez (may need Microsoft C++ Build Tools)")
            return
        
        # Create data handler that processes packets through simulator
        def handle_bt_data(data: bytes) -> bytes:
            """Process Bluetooth data through simulator"""
            response = self.simulator.process_packet(data)
            return response if response else b''
        
        self.server = RFCOMMServer(
            device_name=self.device_name,
            data_handler=handle_bt_data,
            debug=self.simulator.debug
        )
        
        logger.info(f"Starting Bluetooth RFCOMM server: {self.device_name}")
        logger.info("Clients can discover, pair, and connect via Bluetooth")
        
        try:
            self.server.start()  # Blocking call
        except Exception as e:
            logger.error(f"Error starting Bluetooth server: {e}", exc_info=True)
            raise


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Mock M25 Wheel Simulator - Hardware emulation for testing'
    )
    parser.add_argument(
        '--key',
        type=str,
        help='Encryption key (32 hex chars = 16 bytes). Default: USB default key'
    )
    parser.add_argument(
        '--wheel-id',
        type=int,
        choices=[SRC_ID_M25_WHEEL_LEFT, SRC_ID_M25_WHEEL_RIGHT, SRC_ID_M25_WHEEL_COMMON],
        default=SRC_ID_M25_WHEEL_LEFT,
        help='Wheel ID (2=LEFT, 3=RIGHT, 1=COMMON)'
    )
    parser.add_argument(
        '--mode',
        type=str,
        choices=['socket', 'bluetooth', 'rfcomm'],
        default='socket',
        help='Communication mode (bluetooth and rfcomm are aliases for RFCOMM/SPP)'
    )
    parser.add_argument(
        '--device-name',
        type=str,
        help='Bluetooth device name (default: e-motion M25 [Left/Right/Common])'
    )
    parser.add_argument(
        '--port',
        type=int,
        default=5000,
        help='TCP port for socket mode'
    )
    parser.add_argument(
        '--debug',
        action='store_true',
        help='Enable debug logging'
    )
    
    args = parser.parse_args()
    
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Parse key
    if args.key:
        try:
            key = bytes.fromhex(args.key)
            if len(key) != 16:
                print(f"ERROR: Key must be 16 bytes (32 hex chars), got {len(key)} bytes")
                return 1
        except ValueError:
            print("ERROR: Invalid hex key")
            return 1
    else:
        key = DEFAULT_USB_KEY
        logger.info(f"Using default USB key: {key.hex()}")
    
    # Create simulator
    simulator = MockWheelSimulator(key=key, wheel_id=args.wheel_id, debug=args.debug)
    
    # Determine device name for BLE mode
    wheel_names = {
        SRC_ID_M25_WHEEL_LEFT: "Left",
        SRC_ID_M25_WHEEL_RIGHT: "Right",
        SRC_ID_M25_WHEEL_COMMON: "Common"
    }
    device_name = args.device_name or f"e-motion M25 {wheel_names.get(args.wheel_id, 'Wheel')}"
    
    # Run server
    if args.mode == 'socket':
        server = SocketServer(simulator, port=args.port)
        try:
            asyncio.run(server.start())
        except KeyboardInterrupt:
            logger.info("Server stopped by user")
            return 0
    elif args.mode in ['bluetooth', 'rfcomm']:
        if not HAS_BLUETOOTH:
            logger.error("Bluetooth RFCOMM support not available")
            logger.info("Install required library: pip install pybluez")
            logger.info("On Linux: sudo apt-get install libbluetooth-dev python3-dev")
            logger.info("On Windows: pip install pybluez (may need Microsoft C++ Build Tools)")
            logger.info("")
            logger.info("For now, use socket mode: --mode socket")
            return 1
        
        server = BluetoothServer(simulator, device_name=device_name)
        try:
            server.start()  # Blocking call
        except KeyboardInterrupt:
            logger.info("Server stopped by user")
            return 0
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
