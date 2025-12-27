#!/usr/bin/env python3
"""
M25 Parking Mode - Remote movement control demo.

SAFETY: This WILL move the wheelchair. Use responsibly.
        We are not liable for any yeet-related incidents.
"""

import argparse
import sys
import time
import struct

# Import M25 protocol utilities
from m25_utils import parse_key
from m25_spp import BluetoothConnection, PacketBuilder
from m25_protocol_data import (
    SYSTEM_MODE_CONNECT,
    DRIVE_MODE_NORMAL, DRIVE_MODE_REMOTE
)

# Speed settings (signed 16-bit, positive = forward)
SPEED_STOP = 0
SPEED_SLOW = 80       # Gentle forward
SPEED_MEDIUM = 150    # Medium forward
SPEED_FAST = 250      # Faster (use with caution!)

# Timing
COMMAND_INTERVAL_MS = 100  # Send speed commands every 100ms
FORWARD_DURATION_MS = 500  # Move forward for 500ms (half second)


def run_remote_test(left_conn, right_conn, speed=SPEED_SLOW, left_speed=None, right_speed=None, duration_ms=FORWARD_DURATION_MS, dry_run=False):
    """
    Execute remote control test sequence

    Args:
        left_conn: BluetoothConnection for left wheel
        right_conn: BluetoothConnection for right wheel
        speed: Forward speed value
        duration_ms: How long to move forward
        dry_run: If True, just print packets without sending
    """
    builder_left = PacketBuilder()
    builder_right = PacketBuilder()

    print("\nM25 Remote Control Test" + (" [DRY RUN]" if dry_run else ""))

    def send_to_both(packet_left, packet_right, description):
        """Send packet to both wheels"""
        print(f"\n>> {description}")
        print(f"   Left SPP:  {packet_left.hex()}")
        print(f"   Right SPP: {packet_right.hex()}")

        if not dry_run:
            enc_left = left_conn.send_packet(packet_left)
            enc_right = right_conn.send_packet(packet_right)
            print(f"   Left encrypted:  {enc_left.hex()[:40]}...")
            print(f"   Right encrypted: {enc_right.hex()[:40]}...")
        else:
            # Show what encrypted packet would look like
            enc_left = left_conn.encryptor.encrypt_packet(packet_left)
            enc_right = right_conn.encryptor.encrypt_packet(packet_right)
            print(f"   Left would send:  {enc_left.hex()}")
            print(f"   Right would send: {enc_right.hex()}")

    try:
        # Step 0: Initialize connection (WRITE_SYSTEM_MODE = 0x01)
        print("\n[0/5] INITIALIZING CONNECTION")
        pkt_left = builder_left.build_write_system_mode(SYSTEM_MODE_CONNECT)
        pkt_right = builder_right.build_write_system_mode(SYSTEM_MODE_CONNECT)
        send_to_both(pkt_left, pkt_right, "WRITE_SYSTEM_MODE = 0x01 (connect)")

        if not dry_run:
            time.sleep(0.3)  # Wait for ACK
            # Try to receive ACK
            resp_left = left_conn.receive(timeout=0.5)
            resp_right = right_conn.receive(timeout=0.5)
            if resp_left:
                print(f"   Left ACK: {resp_left.hex()[:40]}...")
            if resp_right:
                print(f"   Right ACK: {resp_right.hex()[:40]}...")

        # Step 1: Enable remote control mode
        print("\n[1/5] ENABLING REMOTE CONTROL MODE")
        pkt_left = builder_left.build_write_drive_mode(DRIVE_MODE_REMOTE)
        pkt_right = builder_right.build_write_drive_mode(DRIVE_MODE_REMOTE)
        send_to_both(pkt_left, pkt_right, "WRITE_DRIVE_MODE = 0x04 (remote)")

        if not dry_run:
            time.sleep(0.3)  # Wait for mode change
            resp_left = left_conn.receive(timeout=0.5)
            resp_right = right_conn.receive(timeout=0.5)
            if resp_left:
                print(f"   Left ACK: {resp_left.hex()[:40]}...")
            if resp_right:
                print(f"   Right ACK: {resp_right.hex()[:40]}...")

        # Step 2: Send forward speed commands
        # Use individual wheel speeds if specified, otherwise use common speed
        actual_left_speed = left_speed if left_speed is not None else speed
        actual_right_speed = right_speed if right_speed is not None else speed

        # Left wheel needs negative speed for forward movement (wheels on opposite sides)
        left_wheel_speed = -actual_left_speed
        right_wheel_speed = actual_right_speed

        print(f"\n[2/5] MOVING FORWARD (left={left_wheel_speed}, right={right_wheel_speed}, duration={duration_ms}ms)")

        num_commands = duration_ms // COMMAND_INTERVAL_MS

        for i in range(num_commands):
            pkt_left = builder_left.build_write_remote_speed(left_wheel_speed)
            pkt_right = builder_right.build_write_remote_speed(right_wheel_speed)

            if i == 0:  # Only show first packet details
                send_to_both(pkt_left, pkt_right, f"WRITE_REMOTE_SPEED left={left_wheel_speed}, right={right_wheel_speed}")
            else:
                if not dry_run:
                    left_conn.send_packet(pkt_left)
                    right_conn.send_packet(pkt_right)
                print(f"   Sending speed command {i+1}/{num_commands}...", end='\r')

            if not dry_run:
                time.sleep(COMMAND_INTERVAL_MS / 1000.0)

        print(f"   Sent {num_commands} speed commands                    ")

        # Step 3: Stop
        print("\n[3/5] STOPPING")
        pkt_left = builder_left.build_write_remote_speed(SPEED_STOP)
        pkt_right = builder_right.build_write_remote_speed(SPEED_STOP)
        send_to_both(pkt_left, pkt_right, "WRITE_REMOTE_SPEED = 0 (stop)")

        if not dry_run:
            time.sleep(0.1)

        # Step 4: Disable remote mode
        print("\n[4/5] DISABLING REMOTE CONTROL MODE")
        pkt_left = builder_left.build_write_drive_mode(DRIVE_MODE_NORMAL)
        pkt_right = builder_right.build_write_drive_mode(DRIVE_MODE_NORMAL)
        send_to_both(pkt_left, pkt_right, "WRITE_DRIVE_MODE = 0x00 (normal)")

        print("\n" + "=" * 60)
        print("TEST COMPLETE")
        print("=" * 60)

    except Exception as e:
        print(f"\nERROR: {e}")

        # Emergency stop
        print("\nSending emergency stop...")
        try:
            pkt_left = builder_left.build_write_remote_speed(SPEED_STOP)
            pkt_right = builder_right.build_write_remote_speed(SPEED_STOP)
            if not dry_run:
                left_conn.send_packet(pkt_left)
                right_conn.send_packet(pkt_right)

            pkt_left = builder_left.build_write_drive_mode(DRIVE_MODE_NORMAL)
            pkt_right = builder_right.build_write_drive_mode(DRIVE_MODE_NORMAL)
            if not dry_run:
                left_conn.send_packet(pkt_left)
                right_conn.send_packet(pkt_right)
        except Exception:
            pass  # Best-effort cleanup, connection may already be broken

        raise


def main():
    parser = argparse.ArgumentParser(
        description='M25 Remote Control Test - Move wheelchair forward briefly',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
SAFETY WARNING:
  This script WILL move the wheelchair! Ensure:
  - Wheelchair is in a safe, open area
  - Nothing/no one is in the path
  - You can reach the power switch
  - Wheels are on the ground

Examples:
  # Dry run (show packets without sending)
  python m25_parking.py --left-key HEXKEY --right-key HEXKEY --dry-run

  # Move forward for 1 second
  python m25_parking.py --left-addr AA:BB:CC:DD:EE:FF --right-addr 11:22:33:44:55:66 \\
                        --left-key HEXKEY --right-key HEXKEY --duration 1000 -f

  # Adjust speed
  python m25_parking.py --left-key HEXKEY --right-key HEXKEY --speed 100 --dry-run

  # Get keys from QR codes
  python m25_qr_to_key.py "QR_CODE_STRING"
"""
    )

    parser.add_argument('--left-addr', metavar='ADDR',
                        help='Left wheel Bluetooth address (XX:XX:XX:XX:XX:XX)')
    parser.add_argument('--right-addr', metavar='ADDR',
                        help='Right wheel Bluetooth address (XX:XX:XX:XX:XX:XX)')
    parser.add_argument('--left-key', metavar='HEX', required=True,
                        help='Left wheel AES key (hex, from m25_qr_to_key.py)')
    parser.add_argument('--right-key', metavar='HEX', required=True,
                        help='Right wheel AES key (hex, from m25_qr_to_key.py)')
    parser.add_argument('--speed', type=int, default=SPEED_SLOW,
                        help=f'Forward speed (default: {SPEED_SLOW}, range: 0-350)')
    parser.add_argument('--left-speed', type=int, default=None,
                        help='Override speed for left wheel (for steering correction)')
    parser.add_argument('--right-speed', type=int, default=None,
                        help='Override speed for right wheel (for steering correction)')
    parser.add_argument('--duration', type=int, default=FORWARD_DURATION_MS,
                        help=f'Duration in ms (default: {FORWARD_DURATION_MS})')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show packets without sending (no Bluetooth required)')
    parser.add_argument('-f', '--force', action='store_true',
                        help='Skip interactive confirmation')

    args = parser.parse_args()

    left_key = parse_key(args.left_key, "left-key")
    right_key = parse_key(args.right_key, "right-key")

    if args.speed < 0 or args.speed > 350:
        print(f"WARNING: Speed {args.speed} is outside typical range (0-350)")

    left_conn = BluetoothConnection(
        args.left_addr or "XX:XX:XX:XX:XX:XX",
        left_key,
        "Left Wheel"
    )
    right_conn = BluetoothConnection(
        args.right_addr or "YY:YY:YY:YY:YY:YY",
        right_key,
        "Right Wheel"
    )

    if args.dry_run:
        print("\n*** DRY RUN MODE ***")
        print("Showing packets that would be sent.\n")
        print(f"Left wheel key:  {left_key.hex()}")
        print(f"Right wheel key: {right_key.hex()}")

        run_remote_test(left_conn, right_conn,
                       speed=args.speed,
                       left_speed=args.left_speed,
                       right_speed=args.right_speed,
                       duration_ms=args.duration,
                       dry_run=True)
    else:
        # Require addresses for real run
        if not args.left_addr or not args.right_addr:
            print("ERROR: Must specify --left-addr and --right-addr for real test")
            print("       Use --dry-run to see packets without sending")
            sys.exit(1)

        # Safety confirmation
        print("\n" + "!" * 60)
        print("WARNING: This will MOVE the wheelchair!")
        print("!" * 60)
        print(f"\nSettings:")
        print(f"  Speed:      {args.speed}")
        if args.left_speed is not None:
            print(f"  Left speed: {args.left_speed}")
        if args.right_speed is not None:
            print(f"  Right speed: {args.right_speed}")
        print(f"  Duration:   {args.duration}ms")
        print(f"  Left addr:  {args.left_addr}")
        print(f"  Right addr: {args.right_addr}")

        if not args.force:
            confirm = input("\nType 'YES' to proceed: ")
            if confirm != 'YES':
                print("Aborted.")
                sys.exit(0)
        else:
            print("\n--force specified, skipping confirmation")

        try:
            # Connect to both wheels
            left_conn.connect()
            right_conn.connect()

            # Run test
            run_remote_test(left_conn, right_conn,
                           speed=args.speed,
                           left_speed=args.left_speed,
                           right_speed=args.right_speed,
                           duration_ms=args.duration,
                           dry_run=False)

        finally:
            # Always disconnect
            left_conn.disconnect()
            right_conn.disconnect()


if __name__ == '__main__':
    main()
