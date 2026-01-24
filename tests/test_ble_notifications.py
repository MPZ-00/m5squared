#!/usr/bin/env python3
"""
BLE Notifications Test Suite

Comprehensive tests for notification functionality:
1. Basic callback notification
2. Queue-based notification
3. Notification with encryption
4. Notification stop/restart
5. Timeout handling
6. Error conditions
"""

import asyncio
import sys
import time
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).parent.parent))

from m25_bluetooth_ble import M25BluetoothBLE


class NotificationTester:
    """Test harness for BLE notifications"""
    
    def __init__(self, address: str, key: bytes = None):
        self.address = address
        self.key = key
        self.test_results: List[tuple] = []
        
    def log_test(self, name: str, passed: bool, details: str = ""):
        """Log test result"""
        status = "PASS" if passed else "FAIL"
        self.test_results.append((name, passed, details))
        print(f"[{status}] {name}")
        if details:
            print(f"      {details}")
    
    def print_summary(self):
        """Print test summary"""
        passed = sum(1 for _, p, _ in self.test_results if p)
        total = len(self.test_results)
        print(f"\n{'='*60}")
        print(f"Test Summary: {passed}/{total} passed")
        print(f"{'='*60}")
        
        if passed < total:
            print("\nFailed tests:")
            for name, passed, details in self.test_results:
                if not passed:
                    print(f"  - {name}: {details}")
    
    async def test_01_connection(self) -> bool:
        """Test basic BLE connection"""
        print("\n[Test 1] Basic Connection")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=True
        )
        
        try:
            # Connect
            connected = await bt.connect(timeout=10)
            self.log_test("Connection", connected, 
                         f"Address: {self.address}")
            
            if not connected:
                return False
            
            # Check characteristics discovered
            has_tx = bt._tx_char is not None
            has_rx = bt._rx_char is not None
            
            self.log_test("TX Characteristic", has_tx,
                         f"UUID: {bt._tx_char if has_tx else 'None'}")
            self.log_test("RX Characteristic", has_rx,
                         f"UUID: {bt._rx_char if has_rx else 'None'}")
            
            # Cleanup
            await bt.disconnect()
            return connected and has_tx and has_rx
            
        except Exception as e:
            self.log_test("Connection", False, str(e))
            return False
    
    async def test_02_callback_notification(self) -> bool:
        """Test notification with callback"""
        print("\n[Test 2] Callback Notifications")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=True
        )
        
        try:
            await bt.connect()
            
            # Track received data
            received_data = []
            
            def callback(data: bytes):
                received_data.append(data)
                print(f"  Callback received {len(data)} bytes: {data.hex()}")
            
            # Enable notifications
            notify_started = await bt.start_notifications(callback)
            self.log_test("Start Notifications (callback)", notify_started)
            
            if not notify_started:
                await bt.disconnect()
                return False
            
            # Wait for data (5 seconds)
            print("  Waiting 5 seconds for notifications...")
            await asyncio.sleep(5)
            
            # Check if we received any data
            data_received = len(received_data) > 0
            self.log_test("Callback Data Received", data_received,
                         f"Received {len(received_data)} packets")
            
            # Stop notifications
            notify_stopped = await bt.stop_notifications()
            self.log_test("Stop Notifications", notify_stopped)
            
            await bt.disconnect()
            return notify_started and notify_stopped
            
        except Exception as e:
            self.log_test("Callback Notifications", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def test_03_queue_notification(self) -> bool:
        """Test notification with queue"""
        print("\n[Test 3] Queue Notifications")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=True
        )
        
        try:
            await bt.connect()
            
            # Enable notifications without callback (uses queue)
            notify_started = await bt.start_notifications()
            self.log_test("Start Notifications (queue)", notify_started)
            
            if not notify_started:
                await bt.disconnect()
                return False
            
            # Wait for notifications with timeout
            print("  Waiting for notification (5 second timeout)...")
            start_time = time.time()
            data = await bt.wait_notification(timeout=5.0)
            elapsed = time.time() - start_time
            
            data_received = data is not None
            self.log_test("Queue Data Received", data_received,
                         f"Received in {elapsed:.2f}s: {data.hex() if data else 'None'}")
            
            # Test timeout behavior
            print("  Testing timeout (1 second, expecting timeout)...")
            start_time = time.time()
            timeout_data = await bt.wait_notification(timeout=1.0)
            timeout_elapsed = time.time() - start_time
            
            timeout_works = timeout_data is None and 0.9 < timeout_elapsed < 1.2
            self.log_test("Timeout Handling", timeout_works,
                         f"Timeout after {timeout_elapsed:.2f}s")
            
            await bt.stop_notifications()
            await bt.disconnect()
            return notify_started and timeout_works
            
        except Exception as e:
            self.log_test("Queue Notifications", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def test_04_notification_restart(self) -> bool:
        """Test stopping and restarting notifications"""
        print("\n[Test 4] Notification Restart")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=True
        )
        
        try:
            await bt.connect()
            
            # Start, stop, start again
            print("  First start...")
            started1 = await bt.start_notifications()
            self.log_test("First Start", started1)
            
            await asyncio.sleep(1)
            
            print("  Stop...")
            stopped = await bt.stop_notifications()
            self.log_test("Stop", stopped)
            
            await asyncio.sleep(1)
            
            print("  Second start...")
            started2 = await bt.start_notifications()
            self.log_test("Second Start", started2)
            
            # Verify still working
            print("  Checking if still receiving...")
            data = await bt.wait_notification(timeout=5.0)
            still_works = data is not None
            self.log_test("Restart Works", still_works,
                         f"Received: {data.hex() if data else 'None'}")
            
            await bt.stop_notifications()
            await bt.disconnect()
            return started1 and stopped and started2
            
        except Exception as e:
            self.log_test("Notification Restart", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def test_05_encryption_notification(self) -> bool:
        """Test notifications with encryption (if key provided)"""
        print("\n[Test 5] Encrypted Notifications")
        print("-" * 60)
        
        if not self.key:
            self.log_test("Encrypted Notifications", True, "Skipped (no key)")
            return True
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=True
        )
        
        try:
            await bt.connect()
            
            # Verify encryptor/decryptor exist
            has_crypto = bt.encryptor is not None and bt.decryptor is not None
            self.log_test("Crypto Objects", has_crypto)
            
            if not has_crypto:
                await bt.disconnect()
                return False
            
            # Enable notifications
            await bt.start_notifications()
            
            # Receive encrypted data
            print("  Waiting for encrypted notification...")
            data = await bt.wait_notification(timeout=5.0)
            
            # If we got data, it was decrypted automatically
            received_decrypted = data is not None
            self.log_test("Decrypted Data", received_decrypted,
                         f"Data: {data.hex() if data else 'None'}")
            
            await bt.stop_notifications()
            await bt.disconnect()
            return has_crypto and received_decrypted
            
        except Exception as e:
            self.log_test("Encrypted Notifications", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def test_06_error_conditions(self) -> bool:
        """Test error handling"""
        print("\n[Test 6] Error Conditions")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=False  # Reduce noise for error tests
        )
        
        try:
            # Try notifications before connection
            print("  Testing notification before connect...")
            result = await bt.start_notifications()
            before_connect = not result  # Should fail
            self.log_test("Notification Before Connect", before_connect,
                         "Correctly rejected" if before_connect else "Incorrectly allowed")
            
            # Connect normally
            await bt.connect()
            
            # Try wait_notification before start_notifications
            print("  Testing wait before start...")
            try:
                data = await bt.wait_notification(timeout=0.1)
                wait_before_start = data is None
                self.log_test("Wait Before Start", wait_before_start,
                             "Correctly returned None" if wait_before_start else "Incorrect behavior")
            except Exception as e:
                self.log_test("Wait Before Start", False, f"Exception: {e}")
                wait_before_start = False
            
            # Normal operation
            await bt.start_notifications()
            await asyncio.sleep(0.5)
            await bt.stop_notifications()
            
            # Try stopping twice
            print("  Testing double stop...")
            await bt.stop_notifications()
            double_stop = True  # Should not crash
            self.log_test("Double Stop", double_stop, "No crash")
            
            await bt.disconnect()
            return before_connect and wait_before_start and double_stop
            
        except Exception as e:
            self.log_test("Error Conditions", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def test_07_performance(self) -> bool:
        """Test notification performance"""
        print("\n[Test 7] Performance Metrics")
        print("-" * 60)
        
        bt = M25BluetoothBLE(
            address=self.address,
            key=self.key,
            name="test_wheel",
            debug=False
        )
        
        try:
            await bt.connect()
            
            # Measure notification latency
            received_count = 0
            latencies = []
            
            def callback(data: bytes):
                nonlocal received_count
                received_count += 1
            
            await bt.start_notifications(callback)
            
            print("  Collecting notifications for 10 seconds...")
            start_time = time.time()
            await asyncio.sleep(10)
            elapsed = time.time() - start_time
            
            rate = received_count / elapsed
            self.log_test("Notification Rate", True,
                         f"{received_count} packets in {elapsed:.1f}s = {rate:.2f} Hz")
            
            # Test queue throughput
            await bt.stop_notifications()
            await bt.start_notifications()  # Queue mode
            
            queue_count = 0
            print("  Testing queue throughput (5 seconds)...")
            timeout_time = time.time() + 5
            while time.time() < timeout_time:
                data = await bt.wait_notification(timeout=0.5)
                if data:
                    queue_count += 1
            
            queue_rate = queue_count / 5.0
            self.log_test("Queue Throughput", True,
                         f"{queue_count} packets in 5s = {queue_rate:.2f} Hz")
            
            await bt.stop_notifications()
            await bt.disconnect()
            return True
            
        except Exception as e:
            self.log_test("Performance", False, str(e))
            try:
                await bt.disconnect()
            except:
                pass
            return False
    
    async def run_all_tests(self):
        """Run all tests in sequence"""
        print("="*60)
        print("BLE Notification Test Suite")
        print("="*60)
        print(f"Device: {self.address}")
        print(f"Encryption: {'Enabled' if self.key else 'Disabled'}")
        print("="*60)
        
        tests = [
            self.test_01_connection,
            self.test_02_callback_notification,
            self.test_03_queue_notification,
            self.test_04_notification_restart,
            self.test_05_encryption_notification,
            self.test_06_error_conditions,
            self.test_07_performance,
        ]
        
        for test in tests:
            try:
                await test()
            except Exception as e:
                print(f"\n[EXCEPTION] {test.__name__}: {e}")
                self.log_test(test.__name__, False, str(e))
            
            # Pause between tests
            await asyncio.sleep(2)
        
        self.print_summary()


async def main():
    """Main test entry point"""
    import argparse
    import os
    from dotenv import load_dotenv
    
    parser = argparse.ArgumentParser(description="BLE Notification Test Suite")
    parser.add_argument("--address", help="Bluetooth address (or use M25_LEFT_MAC from .env)")
    parser.add_argument("--key", help="Encryption key hex (or use M25_LEFT_KEY from .env)")
    parser.add_argument("--no-encryption", action="store_true", help="Disable encryption")
    
    args = parser.parse_args()
    
    # Load from .env if not provided
    load_dotenv()
    
    address = args.address or os.getenv("M25_LEFT_MAC")
    
    if not address:
        print("ERROR: No Bluetooth address provided")
        print("  Use --address AA:BB:CC:DD:EE:FF")
        print("  Or set M25_LEFT_MAC in .env file")
        sys.exit(1)
    
    # Get encryption key
    key = None
    if not args.no_encryption:
        if args.key:
            key = bytes.fromhex(args.key)
        else:
            key_hex = os.getenv("M25_LEFT_KEY", "")
            if key_hex:
                key = bytes.fromhex(key_hex)
    
    # Run tests
    tester = NotificationTester(address, key)
    await tester.run_all_tests()


if __name__ == "__main__":
    asyncio.run(main())
