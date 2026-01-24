#!/usr/bin/env python3
"""
Quick test to verify Bluetooth RFCOMM server functionality.
Tests basic server startup and shutdown.
"""

import sys
import time
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from bluetooth_server import RFCOMMServer, HAS_PYBLUEZ


def test_server_creation():
    """Test that we can create an RFCOMM server instance."""
    if not HAS_PYBLUEZ:
        print("SKIP: pybluez not installed")
        return False
    
    try:
        server = RFCOMMServer(
            device_name="Test Wheel",
            data_handler=lambda data: b"ACK",
            debug=True
        )
        print("✓ Server instance created successfully")
        return True
    except Exception as e:
        print(f"✗ Failed to create server: {e}")
        return False


def test_server_start_stop():
    """Test server start and stop (non-blocking)."""
    if not HAS_PYBLUEZ:
        print("SKIP: pybluez not installed")
        return False
    
    try:
        server = RFCOMMServer(
            device_name="Test Wheel",
            data_handler=lambda data: b"ACK",
            debug=True
        )
        
        # Start server in background thread
        import threading
        server_thread = threading.Thread(target=server.start, daemon=True)
        server_thread.start()
        
        print("✓ Server started in background")
        
        # Let it run for a moment
        time.sleep(2)
        
        # Stop server
        server.stop()
        print("✓ Server stopped successfully")
        
        return True
        
    except Exception as e:
        print(f"✗ Server test failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    """Run all tests."""
    print("=" * 60)
    print("Bluetooth RFCOMM Server Tests")
    print("=" * 60)
    
    if not HAS_PYBLUEZ:
        print("\nERROR: PyBluez not installed!")
        print("Install with: pip install pybluez")
        print("\nOn Linux, you may also need:")
        print("  sudo apt-get install libbluetooth-dev python3-dev")
        return 1
    
    tests = [
        ("Server Creation", test_server_creation),
        ("Server Start/Stop", test_server_start_stop),
    ]
    
    passed = 0
    failed = 0
    
    for name, test_func in tests:
        print(f"\n{name}:")
        if test_func():
            passed += 1
        else:
            failed += 1
    
    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)
    
    if failed == 0:
        print("\n✓ All tests passed! Bluetooth server is working.")
        print("\nYou can now start the simulator with:")
        print("  python mock_wheel_simulator.py --mode bluetooth --debug")
    else:
        print("\n✗ Some tests failed. Check errors above.")
    
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
