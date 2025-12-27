#!/usr/bin/env python3
"""
M25 Protocol Analyzer - Make sense of the packet soup.

Pipe decrypted packets in, get human-readable analysis out.
Finally understand what "01 05 05 01 01 50 00 5a" actually means.
(Spoiler: it's setting drive profile parameters. Riveting stuff.)
"""

import sys
import argparse
import json

from m25_utils import parse_hex, iter_input_lines, has_input_available
from m25_protocol_data import (
    POS_PROTOCOL_ID,
    POS_TELEGRAM_ID,
    POS_SOURCE_ID,
    POS_DEST_ID,
    POS_SERVICE_ID,
    POS_PARAM_ID,
    POS_PAYLOAD,
    MIN_SPP_PACKET_SIZE,
    PROTOCOL_ID_STANDARD,
    get_source_name,
    get_dest_name,
    get_service_name,
    get_param_name,
    is_valid_protocol_id
)


class M25PacketParser:
    """Parse decrypted M25 SPP packets"""

    def parse(self, spp_data):
        """
        Parse decrypted SPP packet data

        Args:
            spp_data (bytes): Decrypted SPP data from m25_decrypt.py

        Returns:
            dict: Parsed packet structure with fields and human-readable names
            None: On parsing error
        """
        # Validate minimum packet size
        if len(spp_data) < MIN_SPP_PACKET_SIZE:
            print(f"ERROR: Packet too short ({len(spp_data)} bytes, minimum {MIN_SPP_PACKET_SIZE})",
                  file=sys.stderr)
            return None

        # Extract fields
        protocol_id = spp_data[POS_PROTOCOL_ID]
        telegram_id = spp_data[POS_TELEGRAM_ID]
        source_id = spp_data[POS_SOURCE_ID]
        dest_id = spp_data[POS_DEST_ID]
        service_id = spp_data[POS_SERVICE_ID]
        param_id = spp_data[POS_PARAM_ID]
        payload = spp_data[POS_PAYLOAD:] if len(spp_data) > POS_PAYLOAD else b''

        packet = {
            # Raw field values
            'protocol_id': protocol_id,
            'telegram_id': telegram_id,
            'source_id': source_id,
            'dest_id': dest_id,
            'service_id': service_id,
            'param_id': param_id,
            'payload': payload,

            # Metadata
            'packet_length': len(spp_data),
            'payload_length': len(payload),
            'has_payload': len(payload) > 0,

            # Human-readable names
            'source_name': get_source_name(source_id),
            'dest_name': get_dest_name(dest_id),
            'service_name': get_service_name(service_id),
            'param_name': get_param_name(service_id, param_id),

            # Raw data
            'raw_hex': spp_data.hex()
        }

        return packet

    def validate(self, packet):
        """
        Validate packet structure and return warnings

        Args:
            packet (dict): Parsed packet from parse()

        Returns:
            list: Warning messages (empty if no issues)
        """
        warnings = []

        if not is_valid_protocol_id(packet['protocol_id']):
            warnings.append(
                f"Invalid protocol ID: 0x{packet['protocol_id']:02X} "
                f"(expected 0x00 or 0x01)"
            )

        if packet['source_name'].startswith("UNKNOWN_SRC"):
            warnings.append(f"Unknown source ID: 0x{packet['source_id']:02X}")

        if packet['dest_name'].startswith("UNKNOWN_DEST"):
            warnings.append(f"Unknown destination ID: 0x{packet['dest_id']:02X}")

        if packet['service_name'].startswith("UNKNOWN_SRV"):
            warnings.append(f"Unknown service ID: 0x{packet['service_id']:02X}")

        return warnings


class PacketFormatter:
    """Format parsed packets for display"""

    @staticmethod
    def format_text(packet, verbose=False):
        """
        Format packet as human-readable text

        Args:
            packet (dict): Parsed packet from M25PacketParser
            verbose (bool): Include raw hex data

        Returns:
            str: Formatted text output
        """
        lines = []

        # Header
        lines.append(f"Protocol ID:  0x{packet['protocol_id']:02X}")
        lines.append(f"Telegram ID:  {packet['telegram_id']} (0x{packet['telegram_id']:02X})")

        # Communication
        lines.append(f"Source:       {packet['source_name']} (0x{packet['source_id']:02X})")
        lines.append(f"Destination:  {packet['dest_name']} (0x{packet['dest_id']:02X})")

        # Service/Parameter
        lines.append(f"Service:      {packet['service_name']} (0x{packet['service_id']:02X})")
        lines.append(f"Parameter:    {packet['param_name']} (0x{packet['param_id']:02X})")

        # Payload
        if packet['has_payload']:
            lines.append(f"Payload:      {packet['payload'].hex()} ({packet['payload_length']} bytes)")
        else:
            lines.append("Payload:      (none)")

        # Verbose: raw hex
        if verbose:
            lines.append(f"Raw SPP:      {packet['raw_hex']}")

        return '\n'.join(lines)

    @staticmethod
    def format_json(packet):
        """
        Format packet as JSON

        Args:
            packet (dict): Parsed packet from M25PacketParser

        Returns:
            str: JSON string
        """
        json_data = {
            'protocol': {
                'id': f"0x{packet['protocol_id']:02X}",
                'telegram_id': packet['telegram_id']
            },
            'communication': {
                'source': {
                    'id': packet['source_id'],
                    'name': packet['source_name']
                },
                'destination': {
                    'id': packet['dest_id'],
                    'name': packet['dest_name']
                }
            },
            'service': {
                'id': packet['service_id'],
                'name': packet['service_name']
            },
            'parameter': {
                'id': packet['param_id'],
                'name': packet['param_name']
            },
            'payload': {
                'hex': packet['payload'].hex(),
                'length': packet['payload_length']
            }
        }

        return json.dumps(json_data, indent=2)

    @staticmethod
    def format_oneline(packet):
        """
        Format packet as single line (for grep/filtering)

        Args:
            packet (dict): Parsed packet from M25PacketParser

        Returns:
            str: Single-line compact format
        """
        payload_str = packet['payload'].hex() if packet['has_payload'] else "(none)"

        return (
            f"PID=0x{packet['protocol_id']:02X} "
            f"TID={packet['telegram_id']:02X} "
            f"{packet['source_name']}->{packet['dest_name']} "
            f"SRV={packet['service_name']} "
            f"PARAM={packet['param_name']} "
            f"PAYLOAD={payload_str}"
        )


def process_packet(parser, formatter, hex_data, output_format, verbose, show_warnings):
    """
    Process single decrypted SPP packet

    Args:
        parser (M25PacketParser): Parser instance
        formatter (PacketFormatter): Formatter instance
        hex_data (str): Hex string of decrypted SPP data
        output_format (str): Output format (text/json/oneline)
        verbose (bool): Verbose mode flag
        show_warnings (bool): Display validation warnings

    Returns:
        bool: Success status
    """
    # Parse hex string to bytes
    try:
        spp_data = parse_hex(hex_data)
    except ValueError as e:
        print(f"ERROR: Invalid hex string: {e}", file=sys.stderr)
        return False

    # Parse packet
    packet = parser.parse(spp_data)
    if packet is None:
        return False

    # Validate and show warnings
    if show_warnings:
        warnings = parser.validate(packet)
        for warning in warnings:
            print(f"WARNING: {warning}", file=sys.stderr)

    # Format and output
    if output_format == 'json':
        print(formatter.format_json(packet))
    elif output_format == 'oneline':
        print(formatter.format_oneline(packet))
    else:  # 'text'
        print(formatter.format_text(packet, verbose))
        # Add visual separator
        print("############\n")

    return True


def main():
    parser_cli = argparse.ArgumentParser(
        description='M25 Protocol Analyzer - Parse decrypted SPP packets',
        epilog='Pipe from m25_decrypt: python m25_decrypt.py -u -m packets.txt | python m25_analyzer.py'
    )

    # Input options
    parser_cli.add_argument('input', nargs='?',
                           help='Decrypted hex string or file path')
    parser_cli.add_argument('-f', '--file',
                           help='Input file (one decrypted hex per line)')

    # Output options
    parser_cli.add_argument('--format', choices=['text', 'json', 'oneline'],
                           default='text',
                           help='Output format (default: text)')
    parser_cli.add_argument('-v', '--verbose', action='store_true',
                           help='Verbose mode (show raw hex in text format)')
    parser_cli.add_argument('-w', '--warnings', action='store_true',
                           help='Show validation warnings')

    args = parser_cli.parse_args()
    parser = M25PacketParser()
    formatter = PacketFormatter()

    if not has_input_available(args.input, args.file):
        parser_cli.print_help(sys.stderr)
        sys.exit(1)

    try:
        for line in iter_input_lines(args.input, args.file):
            process_packet(parser, formatter, line, args.format,
                         args.verbose, args.warnings)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
