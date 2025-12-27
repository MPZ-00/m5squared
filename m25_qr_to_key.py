#!/usr/bin/env python3
"""
M25 QR Code to AES Key Converter

Each wheel has a QR code sticker. Scan it, feed it here, get the AES key.
The "UPK" algorithm they invented is... creative. Base-62-ish with extra
characters because why use standard encodings when you can roll your own?
"""

import sys
import argparse

from m25_utils import iter_input_lines, has_input_available


class M25QRConverter:
    UPK_CHARSET = [
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '#', '@', '&', '?', '/'
    ]

    def convert_qr_to_key(self, qr_code):
        """Convert QR code to 16-byte AES key. Returns None on failure."""
        if len(qr_code) != 22:
            print(f"ERROR: QR code must be 22 characters (got {len(qr_code)})", file=sys.stderr)
            return None

        for char in qr_code:
            if char not in self.UPK_CHARSET:
                print(f"ERROR: Invalid character '{char}' in QR code", file=sys.stderr)
                return None

        binary_string = ""
        for char in qr_code:
            index = self.UPK_CHARSET.index(char)
            binary_string += format(index, '06b')

        binary_key = binary_string[4:132]
        key_bytes = bytearray()
        for i in range(0, 128, 8):
            byte_val = int(binary_key[i:i+8], 2)
            key_bytes.append(byte_val)

        return bytes(key_bytes)


def process_qr(converter, qr_code, output_file):
    """Process single QR code."""
    qr_code = qr_code.strip()
    if not qr_code:
        return False

    key = converter.convert_qr_to_key(qr_code)
    if key is None:
        return False

    print(key.hex(), file=output_file)
    return True


def main():
    parser = argparse.ArgumentParser(
        description='M25 QR Code to AES Key Converter',
        epilog='Uses the proprietary UPK encoding algorithm'
    )
    parser.add_argument('input', nargs='?', help='QR code string or file path')
    parser.add_argument('-f', '--file', help='Input file (one QR code per line)')
    parser.add_argument('-o', '--out', help='Output file (default: stdout)')
    args = parser.parse_args()

    converter = M25QRConverter()

    if not has_input_available(args.input, args.file):
        parser.print_help(sys.stderr)
        sys.exit(1)

    output_file = open(args.out, 'w') if args.out else sys.stdout

    try:
        for line in iter_input_lines(args.input, args.file):
            process_qr(converter, line, output_file)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        if args.out:
            output_file.close()


if __name__ == "__main__":
    main()
