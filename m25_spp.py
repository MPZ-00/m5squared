#!/usr/bin/env python3
"""
M25 SPP Layer - Bluetooth connection and packet building.

Turns out wheelchair wheels speak Bluetooth Serial Port Profile on channel 6.
The future is now.
"""

import socket
import struct
import sys
import time

from m25_protocol import HEADER_MARKER
from m25_crypto import M25Encryptor, M25Decryptor
from m25_protocol_data import (
    PROTOCOL_ID_STANDARD, SRC_ID_SMARTPHONE, DEST_ID_M25_WHEEL_COMMON,
    SERVICE_ID_APP_MGMT,
    PARAM_ID_WRITE_SYSTEM_MODE, PARAM_ID_READ_SYSTEM_MODE,
    PARAM_ID_WRITE_DRIVE_MODE, PARAM_ID_READ_DRIVE_MODE,
    PARAM_ID_WRITE_REMOTE_SPEED,
    DEFAULT_TELEGRAM_ID
)


class BluetoothConnection:
    """Bluetooth SPP connection to M25 wheel with encryption."""

    AF_BLUETOOTH = 31
    BTPROTO_RFCOMM = 3

    def __init__(self, address, key, name="wheel", debug=False):
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        self.socket = None
        self.encryptor = M25Encryptor(key)
        self.decryptor = M25Decryptor(key)

    def connect(self, channel=6):
        """Establish Bluetooth SPP connection."""
        self.socket = socket.socket(self.AF_BLUETOOTH, socket.SOCK_STREAM, self.BTPROTO_RFCOMM)
        self.socket.connect((self.address, channel))
        self.socket.settimeout(2.0)

    def disconnect(self):
        """Close Bluetooth connection."""
        if self.socket:
            self.socket.close()
            self.socket = None

    def send_packet(self, spp_data):
        """Encrypt and send SPP packet."""
        encrypted = self.encryptor.encrypt_packet(spp_data)
        if self.debug:
            print(f"  TX [{self.name}]: {encrypted.hex()}", file=sys.stderr)
            print(f"      SPP: {spp_data.hex()}", file=sys.stderr)
        if self.socket:
            self.socket.send(encrypted)
        return encrypted

    def receive(self, timeout=1.0):
        """Receive complete response (handles fragmentation)."""
        if not self.socket:
            return None

        self.socket.settimeout(timeout)
        buffer = b''
        start_time = time.time()

        try:
            while True:
                elapsed = time.time() - start_time
                if elapsed >= timeout:
                    break

                remaining = timeout - elapsed
                self.socket.settimeout(max(0.1, remaining))
                try:
                    chunk = self.socket.recv(1024)
                    if not chunk:
                        break
                    buffer += chunk
                except socket.timeout:
                    if buffer:
                        break
                    continue

                if len(buffer) >= 3 and buffer[0] == HEADER_MARKER:
                    frame_length = (buffer[1] << 8) | buffer[2]
                    if len(buffer) >= frame_length + 1:
                        break

            return buffer if buffer else None
        except (socket.timeout, Exception):
            return buffer if buffer else None

    def transact(self, spp_data, timeout=1.0):
        """Send packet and receive decrypted response."""
        self.send_packet(spp_data)
        response = self.receive(timeout)
        if response:
            decrypted = self.decryptor.decrypt_packet(response)
            if self.debug:
                print(f"  RX [{self.name}]: {response.hex()}", file=sys.stderr)
                if decrypted:
                    print(f"      SPP: {decrypted.hex()}", file=sys.stderr)
                else:
                    print(f"      SPP: <decrypt failed>", file=sys.stderr)
            return decrypted
        elif self.debug:
            print(f"  RX [{self.name}]: <no response>", file=sys.stderr)
        return None


class PacketBuilder:
    """Build M25 SPP packet payloads."""

    def __init__(self, dest_id=DEST_ID_M25_WHEEL_COMMON):
        self.telegram_id = DEFAULT_TELEGRAM_ID
        self.dest_id = dest_id

    def next_telegram_id(self):
        """Get next telegram ID and increment counter."""
        tid = self.telegram_id
        self.telegram_id = (self.telegram_id + 1) & 0xFF
        return tid

    def build_packet(self, service_id, param_id, payload=b''):
        """Build decrypted SPP packet."""
        return bytes([
            PROTOCOL_ID_STANDARD,
            self.next_telegram_id(),
            SRC_ID_SMARTPHONE,
            self.dest_id,
            service_id,
            param_id
        ]) + payload

    def build_write_system_mode(self, mode):
        """Build WRITE_SYSTEM_MODE packet (0x01=Connect, 0x02=Standby)."""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_SYSTEM_MODE, bytes([mode]))

    def build_read_system_mode(self):
        """Build READ_SYSTEM_MODE packet."""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_SYSTEM_MODE)

    def build_write_drive_mode(self, mode):
        """Build WRITE_DRIVE_MODE packet."""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_MODE, bytes([mode]))

    def build_read_drive_mode(self):
        """Build READ_DRIVE_MODE packet."""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_DRIVE_MODE)

    def build_write_remote_speed(self, speed):
        """Build WRITE_REMOTE_SPEED packet. Speed: signed 16-bit, positive=forward."""
        if speed < 0:
            speed = speed & 0xFFFF
        payload = struct.pack('>h', speed) if speed < 32768 else struct.pack('>H', speed)
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_REMOTE_SPEED, payload)
