#!/usr/bin/env python3
"""
M25 Packet Decryptor - Because sniffing Bluetooth is only half the fun.

Feed it encrypted packets, get plaintext commands. Simple as that.
Supports multiple keys for when you've got a whole fleet to analyze.
"""

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import unpad
except ImportError:
    print("ERROR: PyCryptodome not found. Run: pip install pycryptodome")
    exit(1)

import sys
import argparse

from m25_protocol import DEFAULT_USB_KEY
from m25_crypto import M25Decryptor, MultiKeyDecryptor
from m25_utils import parse_hex, parse_key, iter_input_lines, has_input_available


def extract_scope(result, scope, skip_validation=False):
    """Extract 'complete' packet or 'data' (unpadded) from result."""
    decrypted = result['decrypted']
    packet_bytes = result['packet_bytes']
    frame_length = result['frame_length']

    if scope == 'complete':
        if not skip_validation:
            try:
                _ = unpad(decrypted, AES.block_size)
            except ValueError as e:
                raise ValueError(
                    f"PKCS7 padding validation failed: {e}\n"
                    "Use --no-validation to skip validation"
                )
        return packet_bytes[:frame_length + 1]
    else:
        if not skip_validation:
            try:
                return unpad(decrypted, AES.block_size)
            except ValueError as e:
                raise ValueError(
                    f"PKCS7 padding validation failed: {e}\n"
                    "Use --no-validation to skip validation"
                )
        else:
            return decrypted


def parse_keys_from_file(filepath):
    """Parse keys from file (one hex key per line). Exits on error."""
    try:
        with open(filepath, 'r') as f:
            keys = []
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                try:
                    key = parse_key(line)
                    keys.append(key)
                except SystemExit:
                    print(f"ERROR: Invalid key at line {line_num}: {line}",
                          file=sys.stderr)
                    sys.exit(1)

            if not keys:
                print(f"ERROR: No valid keys found in {filepath}", file=sys.stderr)
                sys.exit(1)

            return keys

    except FileNotFoundError:
        print(f"ERROR: Key file not found: {filepath}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: Failed to read key file: {e}", file=sys.stderr)
        sys.exit(1)


def process_packet(decryptor, packet_hex, output_file, minimal, scope, skip_validation, show_key=False):
    """Decrypt and output single packet."""
    try:
        packet_bytes = parse_hex(packet_hex)
    except ValueError as e:
        print(f"ERROR: Invalid hex string: {e}", file=sys.stderr)
        return False

    result = decryptor.decrypt_verbose(packet_bytes)
    if result is None:
        print("ERROR: Decryption failed (invalid packet or wrong key)", file=sys.stderr)
        return False

    if not minimal:
        print(f"  CRC: 0x{result['crc_received']:04x} OK, {result['frame_length']+1}B", file=sys.stderr)
        print(f"  IV:  {result['iv_encrypted'].hex()} -> {result['iv'].hex()}", file=sys.stderr)

    try:
        output_data = extract_scope(result, scope, skip_validation)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return False

    output_hex = output_data.hex()
    if show_key and 'successful_key_index' in result:
        output_hex += f" [key:{result['successful_key_index']}]"

    print(output_hex, file=output_file)
    if not minimal:
        suffix = ""
        if 'successful_key_index' in result:
            suffix = f", key {result['successful_key_index']}"
        print(f"  ({len(output_data)}B{suffix})", file=sys.stderr)

    return True


def main():
    parser = argparse.ArgumentParser(
        description='M25 Protocol Decryption Tool',
        epilog='USB key: 416c6265725f4d32355f656d6f74696f (use -u to apply)'
    )
    parser.add_argument('input', nargs='?', help='Hex packet string or file path')
    parser.add_argument('-f', '--file', help='Input file (one hex packet per line)')
    key_group = parser.add_mutually_exclusive_group(required=True)
    key_group.add_argument('-k', '--key', help='AES key in hex (16 bytes)')
    key_group.add_argument('-u', '--usb-key', action='store_true', help='Use hardcoded USB key (Alber_M25_emotio)')
    key_group.add_argument('--multi-key', nargs='+', metavar='KEY',
                          help='Try multiple AES keys (hex, space-separated) - exclusive mode, no USB key fallback')
    key_group.add_argument('--keys-file', metavar='FILE',
                          help='File with one hex key per line (try all) - exclusive mode')

    parser.add_argument('--no-validation', action='store_true', help='Skip padding validation (output raw decrypted data even if invalid)')
    parser.add_argument('-m', '--minimal', action='store_true', help='Minimal output mode (hex only)')
    parser.add_argument('-o', '--out', help='Output file (default: stdout)')
    parser.add_argument('--scope', choices=['complete', 'data'], default='data',
                        help='Output scope: complete (full packet), data (decrypted without padding)')
    parser.add_argument('--show-key', action='store_true',
                        help='Show which key succeeded in output (multi-key mode only)')
    args = parser.parse_args()

    if args.multi_key or args.keys_file:
        if args.multi_key:
            keys = [parse_key(k) for k in args.multi_key]
        else:  # args.keys_file
            keys = parse_keys_from_file(args.keys_file)

        decryptor = MultiKeyDecryptor(keys)
        if not args.minimal:
            print(f"Keys: {len(keys)} loaded", file=sys.stderr)
    else:
        if args.usb_key:
            key = DEFAULT_USB_KEY
        else:
            key = parse_key(args.key)

        decryptor = M25Decryptor(key)
        if not args.minimal:
            print(f"Key: {key.hex()}", file=sys.stderr)

    if not has_input_available(args.input, args.file):
        parser.print_help(sys.stderr)
        sys.exit(1)

    output_file = open(args.out, 'w') if args.out else sys.stdout

    try:
        for line in iter_input_lines(args.input, args.file):
            process_packet(decryptor, line, output_file, args.minimal, args.scope, args.no_validation, args.show_key)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        if args.out:
            output_file.close()


if __name__ == "__main__":
    main()
