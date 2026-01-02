#!/usr/bin/env python3
"""
Test WinRT RFCOMM Bluetooth connection

Simple test to verify WinRT Bluetooth works and can connect to M25 wheels.
"""

import sys
import time

try:
    from m25_bluetooth_winrt import WinRTBluetoothConnection
    from m25_utils import parse_key
    from m25_ecs import ECSPacketBuilder, ResponseParser
except ImportError as e:
    print(f"Error: Missing dependencies: {e}")
    print("\nMake sure you have installed:")
    print("  pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Bluetooth.Rfcomm")
    print("  pip install winrt-Windows.Networking winrt-Windows.Storage.Streams")
    sys.exit(1)


def test_connection():
    """Test connecting to a wheel and reading battery"""
    
    # Get test parameters
    print("WinRT RFCOMM Bluetooth Test")
    print("-" * 40)
    
    mac = input("Enter wheel MAC address (e.g., AA:BB:CC:DD:EE:FF): ").strip()
    key_str = input("Enter encryption key (32 hex chars): ").strip()
    
    try:
        key_bytes = parse_key(key_str)
    except Exception as e:
        print(f"Error: Invalid encryption key: {e}")
        return False
    
    print("\nConnecting...")
    conn = WinRTBluetoothConnection(mac, key_bytes, name="Test", debug=True)
    
    if not conn.connect():
        print("Failed to connect")
        return False
    
    print("Connected successfully!\n")
    
    # Try to read battery
    print("Reading battery status...")
    builder = ECSPacketBuilder()
    packet = builder.build_read_soc()
    
    response = conn.transact(packet, timeout=2.0)
    
    if response:
        header = ResponseParser.parse_header(response)
        if header:
            soc = ResponseParser.parse_soc(header['payload'])
            if soc is not None:
                print(f"Battery: {soc}%")
            else:
                print("Could not parse battery response")
        else:
            print("Invalid response header")
    else:
        print("No response received")
    
    # Disconnect
    print("\nDisconnecting...")
    conn.disconnect()
    print("Test complete")
    
    return True


if __name__ == "__main__":
    try:
        success = test_connection()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\nTest interrupted")
        sys.exit(1)
    except Exception as e:
        print(f"\nTest failed with error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
