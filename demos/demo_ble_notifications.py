#!/usr/bin/env python3
"""
BLE Notifications Demo - Power-Efficient Communication

This demo shows how to use BLE notifications instead of polling for
battery-efficient operation. The wheelchair wheels only wake up to send
data when they have something to report, rather than staying awake to
respond to constant read requests.

Power Efficiency:
- Polling (receive_packet): Device must wake for every read request
- Notifications: Device only wakes when it has data to send

This extends wheelchair battery life by minimizing BLE radio usage,
preserving power for the drive motor (280W) and wheelchair operation.

M25 Battery: Lithium-ion 10ICR19/66-2, 36.5V
Range: 25 km per charge (ISO 7176-4)
"""

import asyncio
import os
import sys
from pathlib import Path
from dotenv import load_dotenv

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from m25_bluetooth_ble import M25BluetoothBLE


async def demo_with_callback():
    """Demo: Using notification callback (push model)"""
    print("=== BLE Notifications Demo - Callback Mode ===\n")
    
    # Load config
    load_dotenv()
    left_addr = os.getenv("M25_LEFT_MAC")
    left_key = bytes.fromhex(os.getenv("M25_LEFT_KEY", ""))
    
    if not left_addr:
        print("ERROR: M25_LEFT_MAC not set in .env")
        return
    
    # Create connection
    bt = M25BluetoothBLE(
        address=left_addr,
        key=left_key if left_key else None,
        name="left_wheel",
        debug=True
    )
    
    # Connect
    print(f"Connecting to {left_addr}...")
    if not await bt.connect():
        print("Connection failed")
        return
    
    # Callback function for incoming data
    def handle_wheel_data(data: bytes):
        print(f"WHEEL DATA: {data.hex()} ({len(data)} bytes)")
        # Parse wheel status here
    
    # Enable notifications (power-efficient!)
    print("\nEnabling notifications (power-efficient mode)...")
    if not await bt.start_notifications(handle_wheel_data):
        print("Failed to enable notifications")
        await bt.disconnect()
        return
    
    print("Listening for wheel events (device will push data when available)...")
    print("Power consumption: LOW (device sleeps between events)")
    print("Press Ctrl+C to stop\n")
    
    try:
        # Just wait - callback handles incoming data
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        print("\n\nStopping...")
    
    # Cleanup
    await bt.stop_notifications()
    await bt.disconnect()
    print("Disconnected")


async def demo_with_queue():
    """Demo: Using notification queue (pull model)"""
    print("=== BLE Notifications Demo - Queue Mode ===\n")
    
    # Load config
    load_dotenv()
    left_addr = os.getenv("M25_LEFT_MAC")
    left_key = bytes.fromhex(os.getenv("M25_LEFT_KEY", ""))
    
    if not left_addr:
        print("ERROR: M25_LEFT_MAC not set in .env")
        return
    
    # Create connection
    bt = M25BluetoothBLE(
        address=left_addr,
        key=left_key if left_key else None,
        name="left_wheel",
        debug=True
    )
    
    # Connect
    print(f"Connecting to {left_addr}...")
    if not await bt.connect():
        print("Connection failed")
        return
    
    # Enable notifications without callback (uses queue)
    print("\nEnabling notifications (power-efficient mode)...")
    if not await bt.start_notifications():
        print("Failed to enable notifications")
        await bt.disconnect()
        return
    
    print("Waiting for wheel events (device will push data when available)...")
    print("Power consumption: LOW (device sleeps between events)")
    print("Press Ctrl+C to stop\n")
    
    try:
        # Wait for notifications with timeout
        while True:
            data = await bt.wait_notification(timeout=5.0)
            if data:
                print(f"WHEEL DATA: {data.hex()} ({len(data)} bytes)")
            else:
                print("No data in last 5 seconds (device sleeping)")
    except KeyboardInterrupt:
        print("\n\nStopping...")
    
    # Cleanup
    await bt.stop_notifications()
    await bt.disconnect()
    print("Disconnected")


async def demo_polling_comparison():
    """Demo: Show power consumption difference (POLLING - BAD)"""
    print("=== BLE Polling Demo - High Power Consumption ===\n")
    print("WARNING: This demo uses polling which drains battery faster!")
    print("Use for testing only - prefer notifications in production.\n")
    
    # Load config
    load_dotenv()
    left_addr = os.getenv("M25_LEFT_MAC")
    left_key = bytes.fromhex(os.getenv("M25_LEFT_KEY", ""))
    
    if not left_addr:
        print("ERROR: M25_LEFT_MAC not set in .env")
        return
    
    # Create connection
    bt = M25BluetoothBLE(
        address=left_addr,
        key=left_key if left_key else None,
        name="left_wheel",
        debug=True
    )
    
    # Connect
    print(f"Connecting to {left_addr}...")
    if not await bt.connect():
        print("Connection failed")
        return
    
    print("\nPolling for data (HIGH POWER - keeps device awake)...")
    print("Power consumption: HIGH (device must stay awake to respond)")
    print("Press Ctrl+C to stop\n")
    
    try:
        count = 0
        while True:
            # Polling - device must wake up for EVERY read attempt
            data = await bt.receive_packet(timeout=5)
            count += 1
            
            if data:
                print(f"[Poll {count}] Data: {data.hex()}")
            else:
                print(f"[Poll {count}] No data")
            
            await asyncio.sleep(0.1)  # Still polling 10x per second!
            
    except KeyboardInterrupt:
        print(f"\n\nPolled {count} times (each poll wakes the device)")
        print("With notifications, device would only wake when sending data!")
    
    # Cleanup
    await bt.disconnect()
    print("Disconnected")


async def main():
    """Main demo selector"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="BLE Notifications Demo - Power-Efficient Communication"
    )
    parser.add_argument(
        "mode",
        choices=["callback", "queue", "polling"],
        help="Demo mode: callback (push), queue (pull), or polling (comparison)"
    )
    
    args = parser.parse_args()
    
    if args.mode == "callback":
        await demo_with_callback()
    elif args.mode == "queue":
        await demo_with_queue()
    elif args.mode == "polling":
        await demo_polling_comparison()


if __name__ == "__main__":
    asyncio.run(main())
