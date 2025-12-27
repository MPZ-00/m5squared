#!/usr/bin/env python3
"""
M25 Packet Encryptor - The reverse of decryption. Obviously.

Craft your SPP payload, encrypt it, send it.
"""

import sys
import argparse

from m25_protocol import DEFAULT_USB_KEY
from m25_crypto import M25Encryptor
from m25_utils import parse_hex, parse_key, iter_input_lines, has_input_available


def process_packet(encryptor, hex_data, minimal, verbose):
    """Process single SPP packet and encrypt it"""
    try:
        spp_data = parse_hex(hex_data)
    except ValueError as e:
        print(f"ERROR: Invalid hex string: {e}", file=sys.stderr)
        return False

    try:
        result = encryptor.encrypt_verbose(spp_data)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return False

    if verbose:
        print(f"  SPP:  {spp_data.hex()} ({len(spp_data)}B)", file=sys.stderr)
        print(f"  IV:   {result['iv'].hex()} -> {result['iv_encrypted'].hex()}", file=sys.stderr)
        print(f"  Data: {result['encrypted_data'].hex()} ({len(result['encrypted_data'])}B)", file=sys.stderr)
        print(f"  CRC:  0x{result['crc']:04X}, frame={result['frame_length']}B", file=sys.stderr)

    # Output encrypted packet
    output_hex = result['encrypted_packet'].hex()

    print(output_hex)
    if not minimal:
        print(f"  ({len(result['encrypted_packet'])} bytes)", file=sys.stderr)

    return True


def main():
    parser = argparse.ArgumentParser(
        description='M25 Protocol Encryption Tool - Create ready-to-send USB packets',
        epilog='Default USB key: 416c6265725f4d32355f656d6f74696f (Alber_M25_emotio)'
    )
    parser.add_argument('input', nargs='?', help='Hex SPP data or file path')
    parser.add_argument('-f', '--file', help='Input file (one hex SPP per line)')

    # Key selection (mutually exclusive)
    key_group = parser.add_mutually_exclusive_group(required=True)
    key_group.add_argument('-k', '--key', help='AES key in hex (16 bytes)')
    key_group.add_argument('-u', '--usb-key', action='store_true',
                          help='Use hardcoded USB key (Alber_M25_emotio)')

    parser.add_argument('-m', '--minimal', action='store_true',
                       help='Minimal output mode (hex only)')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Verbose mode (show encryption details)')

    args = parser.parse_args()

    # Parse key
    if args.usb_key:
        key = DEFAULT_USB_KEY
    elif args.key:
        key = parse_key(args.key)
    else:
        print("ERROR: Must specify encryption key", file=sys.stderr)
        sys.exit(1)

    encryptor = M25Encryptor(key)
    if not args.minimal:
        print(f"Key: {key.hex()}", file=sys.stderr)

    if not has_input_available(args.input, args.file):
        parser.print_help(sys.stderr)
        sys.exit(1)

    try:
        for line in iter_input_lines(args.input, args.file):
            process_packet(encryptor, line, args.minimal, args.verbose)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
