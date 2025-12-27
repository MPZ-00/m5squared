#!/usr/bin/env python3
"""
M25 Utilities - The unglamorous helper functions that make everything work.

Hex parsing, key validation, input handling. Not exciting, but essential.
"""

from typing import Optional, Iterator, List
import os
import sys


def parse_hex(hex_str: str) -> bytes:
    """Parse hex string to bytes, tolerating spaces and colons."""
    return bytes.fromhex(hex_str.replace(' ', '').replace(':', ''))


def parse_hex_safe(hex_str: str) -> Optional[bytes]:
    """Parse hex string, returning None on error."""
    try:
        return parse_hex(hex_str)
    except ValueError:
        return None


def parse_hex_key(key_hex: str, key_length: int = 16) -> bytes:
    """Parse and validate AES key from hex string. Raises ValueError on error."""
    try:
        key = parse_hex(key_hex)
    except ValueError as e:
        raise ValueError(f"Invalid key hex: {e}") from e
    if len(key) != key_length:
        raise ValueError(f"Key must be {key_length} bytes, got {len(key)}")
    return key


def parse_key(key_hex: str, name: str = "key") -> bytes:
    """Parse 16-byte AES key from hex string. Prints error and exits on failure."""
    try:
        return parse_hex_key(key_hex, 16)
    except ValueError as e:
        print(f"ERROR: Invalid {name}: {e}", file=sys.stderr)
        sys.exit(1)


def iter_input_lines(input_arg: Optional[str] = None,
                     file_arg: Optional[str] = None,
                     stdin: bool = True,
                     skip_empty: bool = True,
                     skip_comments: bool = False) -> Iterator[str]:
    """
    Unified input iterator: stdin, file, or direct string.

    Priority: file_arg > input_arg (file or value) > stdin
    """
    def should_yield(line: str) -> bool:
        if skip_empty and not line:
            return False
        if skip_comments and line.startswith('#'):
            return False
        return True

    def iter_file(filepath: str) -> Iterator[str]:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if should_yield(line):
                    yield line

    if file_arg:
        if not os.path.isfile(file_arg):
            raise FileNotFoundError(f"File not found: {file_arg}")
        yield from iter_file(file_arg)
        return

    if input_arg:
        if os.path.isfile(input_arg):
            yield from iter_file(input_arg)
        else:
            line = input_arg.strip()
            if should_yield(line):
                yield line
        return

    if stdin and not sys.stdin.isatty():
        for line in sys.stdin:
            line = line.strip()
            if should_yield(line):
                yield line
        return

    raise ValueError("No input provided")


def read_all_input(input_arg: Optional[str] = None,
                   file_arg: Optional[str] = None,
                   stdin: bool = True) -> List[str]:
    """Read all input lines into a list."""
    return list(iter_input_lines(input_arg, file_arg, stdin))


def has_input_available(input_arg: Optional[str] = None,
                        file_arg: Optional[str] = None) -> bool:
    """Check if any input source is available."""
    return bool(file_arg or input_arg or not sys.stdin.isatty())


def format_hex(data: bytes, separator: str = '') -> str:
    """Format bytes as hex string with optional separator."""
    if separator:
        return separator.join(f'{b:02x}' for b in data)
    return data.hex()


def format_hex_upper(data: bytes, separator: str = '') -> str:
    """Format bytes as uppercase hex with optional separator."""
    if separator:
        return separator.join(f'{b:02X}' for b in data)
    return data.hex().upper()
