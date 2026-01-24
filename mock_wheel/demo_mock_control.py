#!/usr/bin/env python3
"""
Demo: Mock Wheel Simulator with Motor Control

Demonstrates the mock wheel simulator by simulating a real control session:
1. Connect to wheel
2. Set system mode to Connected
3. Send increasing/decreasing speed commands
4. Read back current speed
5. Simulate realistic control patterns
"""

import socket
import struct
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from m25_crypto import M25Encryptor, M25Decryptor
from m25_protocol import DEFAULT_USB_KEY
from m25_protocol_data import (
    PROTOCOL_ID_STANDARD,
    SRC_ID_SMARTPHONE,
    DEST_ID_M25_WHEEL_LEFT,
    SERVICE_ID_APP_MGMT,
    PARAM_ID_WRITE_SYSTEM_MODE,
    PARAM_ID_WRITE_REMOTE_SPEED,
    PARAM_ID_READ_CURRENT_SPEED,
)


class WheelController:
    """Simple wheel controller for demo"""
    
    def __init__(self, host='127.0.0.1', port=5000):
        self.host = host
        self.port = port
        self.key = DEFAULT_USB_KEY
        self.encryptor = M25Encryptor(self.key)
        self.decryptor = M25Decryptor(self.key)
        self.telegram_id = 0
        self.sock = None
    
    def connect(self):
        """Connect to simulator"""
        print(f"Connecting to wheel simulator at {self.host}:{self.port}...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.sock.settimeout(2.0)
        print("Connected!\n")
    
    def disconnect(self):
        """Disconnect"""
        if self.sock:
            self.sock.close()
            print("\nDisconnected.")
    
    def next_telegram_id(self):
        tid = self.telegram_id
        self.telegram_id = (self.telegram_id + 1) & 0xFF
        return tid
    
    def build_packet(self, service_id, param_id, payload=b''):
        return bytes([
            PROTOCOL_ID_STANDARD,
            self.next_telegram_id(),
            SRC_ID_SMARTPHONE,
            DEST_ID_M25_WHEEL_LEFT,
            service_id,
            param_id
        ]) + payload
    
    def send_receive(self, spp_packet):
        """Send and receive"""
        encrypted = self.encryptor.encrypt(spp_packet)
        self.sock.send(encrypted)
        
        response_data = b''
        while True:
            chunk = self.sock.recv(1024)
            if not chunk:
                break
            response_data += chunk
            if len(response_data) >= 3:
                frame_length = (response_data[1] << 8) | response_data[2]
                if len(response_data) >= frame_length + 1:
                    break
        
        return self.decryptor.decrypt(response_data) if response_data else None
    
    def set_system_mode(self, mode):
        """Set system mode"""
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_SYSTEM_MODE, bytes([mode]))
        response = self.send_receive(packet)
        return response is not None
    
    def set_speed(self, speed):
        """Set motor speed"""
        speed_bytes = struct.pack('>h', speed)
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_REMOTE_SPEED, speed_bytes)
        response = self.send_receive(packet)
        return response is not None
    
    def get_speed(self):
        """Get current motor speed"""
        packet = self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_CURRENT_SPEED)
        response = self.send_receive(packet)
        if response and len(response) >= 8:
            return struct.unpack('>h', response[6:8])[0]
        return None


def demo_control_sequence():
    """Demonstrate realistic control sequence"""
    controller = WheelController()
    
    try:
        controller.connect()
        
        # Initialize
        print("Initializing wheel...")
        controller.set_system_mode(0x01)  # Connect mode
        time.sleep(0.5)
        
        print("\n" + "=" * 60)
        print("Motor Control Demo")
        print("=" * 60 + "\n")
        
        # Demo 1: Gradual acceleration
        print("Demo 1: Gradual Acceleration (0 -> 100)")
        print("-" * 40)
        for speed in range(0, 101, 10):
            controller.set_speed(speed)
            time.sleep(0.1)
            current_speed = controller.get_speed()
            bar = '#' * (speed // 5)
            print(f"  Target: {speed:3d}  Current: {current_speed:3d}  [{bar:<20}]")
            time.sleep(0.2)
        
        time.sleep(1)
        
        # Demo 2: Gradual deceleration
        print("\nDemo 2: Gradual Deceleration (100 -> 0)")
        print("-" * 40)
        for speed in range(100, -1, -10):
            controller.set_speed(speed)
            time.sleep(0.1)
            current_speed = controller.get_speed()
            bar = '#' * (speed // 5)
            print(f"  Target: {speed:3d}  Current: {current_speed:3d}  [{bar:<20}]")
            time.sleep(0.2)
        
        time.sleep(1)
        
        # Demo 3: Pulse pattern
        print("\nDemo 3: Pulse Pattern")
        print("-" * 40)
        for cycle in range(3):
            print(f"  Cycle {cycle + 1}/3:")
            controller.set_speed(80)
            time.sleep(0.3)
            current = controller.get_speed()
            print(f"    Speed UP to {current}")
            
            controller.set_speed(20)
            time.sleep(0.3)
            current = controller.get_speed()
            print(f"    Speed DOWN to {current}")
        
        time.sleep(0.5)
        
        # Demo 4: Stop
        print("\nDemo 4: Emergency Stop")
        print("-" * 40)
        controller.set_speed(0)
        time.sleep(0.1)
        current_speed = controller.get_speed()
        print(f"  Motor stopped. Speed: {current_speed}")
        
        print("\n" + "=" * 60)
        print("Demo Complete!")
        print("=" * 60)
        
    except KeyboardInterrupt:
        print("\n\nDemo interrupted by user")
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
    finally:
        controller.disconnect()


if __name__ == '__main__':
    print("=" * 60)
    print("Mock Wheel Simulator - Control Demo")
    print("=" * 60)
    print("\nMake sure the simulator is running:")
    print("  python mock_wheel_simulator.py")
    print("\nPress Ctrl+C to stop\n")
    
    time.sleep(2)
    demo_control_sequence()
