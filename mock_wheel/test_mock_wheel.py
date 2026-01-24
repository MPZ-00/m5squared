#!/usr/bin/env python3
"""
Test client for Mock Wheel Simulator

Connects to the mock wheel simulator and sends test commands
to verify the simulator is working correctly.
"""

import asyncio
import socket
import sys
import struct
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from m25_crypto import M25Encryptor, M25Decryptor
from m25_protocol import DEFAULT_USB_KEY
from m25_protocol_data import (
    PROTOCOL_ID_STANDARD,
    SRC_ID_SMARTPHONE,
    DEST_ID_M25_WHEEL_LEFT,
    SERVICE_ID_APP_MGMT,
    PARAM_ID_WRITE_SYSTEM_MODE,
    PARAM_ID_READ_SYSTEM_MODE,
    PARAM_ID_WRITE_DRIVE_MODE,
    PARAM_ID_READ_DRIVE_MODE,
    PARAM_ID_WRITE_REMOTE_SPEED,
    PARAM_ID_READ_CURRENT_SPEED,
    PARAM_ID_ACK,
)


class SimpleTestClient:
    """Simple test client for the mock wheel simulator"""
    
    def __init__(self, host: str = '127.0.0.1', port: int = 5000, key: bytes = DEFAULT_USB_KEY):
        self.host = host
        self.port = port
        self.key = key
        self.encryptor = M25Encryptor(key)
        self.decryptor = M25Decryptor(key)
        self.telegram_id = 0
        self.sock = None
    
    def connect(self):
        """Connect to the simulator"""
        print(f"Connecting to {self.host}:{self.port}...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.sock.settimeout(2.0)
        print("Connected!")
    
    def disconnect(self):
        """Disconnect from the simulator"""
        if self.sock:
            self.sock.close()
            self.sock = None
    
    def next_telegram_id(self) -> int:
        """Get next telegram ID"""
        tid = self.telegram_id
        self.telegram_id = (self.telegram_id + 1) & 0xFF
        return tid
    
    def build_packet(self, service_id: int, param_id: int, payload: bytes = b'') -> bytes:
        """Build SPP packet"""
        return bytes([
            PROTOCOL_ID_STANDARD,
            self.next_telegram_id(),
            SRC_ID_SMARTPHONE,
            DEST_ID_M25_WHEEL_LEFT,
            service_id,
            param_id
        ]) + payload
    
    def send_receive(self, spp_packet: bytes, timeout: float = 2.0) -> bytes:
        """Send packet and receive response"""
        # Encrypt and send
        encrypted = self.encryptor.encrypt(spp_packet)
        print(f"  TX: {spp_packet.hex()} -> {len(encrypted)} bytes encrypted")
        self.sock.send(encrypted)
        
        # Small delay to allow server to process
        import time
        time.sleep(0.1)
        
        # Receive response
        self.sock.settimeout(timeout)
        response_data = b''
        
        # Read until we have a complete packet
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                chunk = self.sock.recv(1024)
                if not chunk:
                    break
                response_data += chunk
                
                # Check if we have enough data
                if len(response_data) >= 3:
                    frame_length = (response_data[1] << 8) | response_data[2]
                    if len(response_data) >= frame_length + 1:
                        break
            except socket.timeout:
                if response_data:
                    break
                continue
        
        if not response_data:
            print("  RX: No response")
            return None
        
        # Decrypt response
        decrypted = self.decryptor.decrypt(response_data)
        if decrypted:
            print(f"  RX: {len(response_data)} bytes encrypted -> {decrypted.hex()}")
            return decrypted
        else:
            print("  RX: Decryption failed")
            return None
    
    def test_write_system_mode(self):
        """Test WRITE_SYSTEM_MODE command"""
        print("\n[TEST] Write System Mode (Connect)")
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_SYSTEM_MODE, bytes([0x01]))
        response = self.send_receive(packet)
        
        if response and len(response) >= 6:
            param_id = response[5]
            if param_id == PARAM_ID_ACK:
                print("  Result: ACK received - SUCCESS")
                return True
            else:
                print(f"  Result: Unexpected response param_id={param_id:02X}")
        else:
            print("  Result: FAILED")
        return False
    
    def test_read_system_mode(self):
        """Test READ_SYSTEM_MODE command"""
        print("\n[TEST] Read System Mode")
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_SYSTEM_MODE)
        response = self.send_receive(packet)
        
        if response and len(response) >= 7:
            mode = response[6]
            print(f"  Result: System Mode = 0x{mode:02X} - SUCCESS")
            return True
        else:
            print("  Result: FAILED")
        return False
    
    def test_write_drive_mode(self):
        """Test WRITE_DRIVE_MODE command"""
        print("\n[TEST] Write Drive Mode (Normal)")
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_MODE, bytes([0x01]))
        response = self.send_receive(packet)
        
        if response and len(response) >= 6:
            param_id = response[5]
            if param_id == PARAM_ID_ACK:
                print("  Result: ACK received - SUCCESS")
                return True
        print("  Result: FAILED")
        return False
    
    def test_write_remote_speed(self, speed: int = 50):
        """Test WRITE_REMOTE_SPEED command"""
        print(f"\n[TEST] Write Remote Speed ({speed})")
        speed_bytes = struct.pack('>h', speed)  # Big-endian signed short
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_REMOTE_SPEED, speed_bytes)
        response = self.send_receive(packet)
        
        if response and len(response) >= 6:
            param_id = response[5]
            if param_id == PARAM_ID_ACK:
                print("  Result: ACK received - SUCCESS")
                return True
        print("  Result: FAILED")
        return False
    
    def test_read_current_speed(self):
        """Test READ_CURRENT_SPEED command"""
        print("\n[TEST] Read Current Speed")
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_CURRENT_SPEED)
        response = self.send_receive(packet)
        
        if response and len(response) >= 8:
            speed = struct.unpack('>h', response[6:8])[0]
            print(f"  Result: Current Speed = {speed} - SUCCESS")
            return True
        print("  Result: FAILED")
        return False
    
    def run_all_tests(self):
        """Run all tests"""
        print("=" * 60)
        print("Mock Wheel Simulator - Test Client")
        print("=" * 60)
        
        tests = [
            ("Write System Mode", self.test_write_system_mode),
            ("Read System Mode", self.test_read_system_mode),
            ("Write Drive Mode", self.test_write_drive_mode),
            ("Write Remote Speed (50)", lambda: self.test_write_remote_speed(50)),
            ("Read Current Speed", self.test_read_current_speed),
            ("Write Remote Speed (100)", lambda: self.test_write_remote_speed(100)),
            ("Read Current Speed", self.test_read_current_speed),
            ("Write Remote Speed (0)", lambda: self.test_write_remote_speed(0)),
            ("Read Current Speed", self.test_read_current_speed),
        ]
        
        results = []
        for test_name, test_func in tests:
            try:
                result = test_func()
                results.append((test_name, result))
            except Exception as e:
                print(f"  ERROR: {e}")
                results.append((test_name, False))
        
        print("\n" + "=" * 60)
        print("Test Results Summary")
        print("=" * 60)
        
        for test_name, result in results:
            status = "PASS" if result else "FAIL"
            print(f"  [{status}] {test_name}")
        
        passed = sum(1 for _, result in results if result)
        total = len(results)
        print(f"\n  Total: {passed}/{total} tests passed")
        print("=" * 60)
        
        return passed == total


def main():
    """Main entry point"""
    client = SimpleTestClient()
    
    try:
        client.connect()
        success = client.run_all_tests()
        return 0 if success else 1
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        client.disconnect()


if __name__ == '__main__':
    sys.exit(main())
