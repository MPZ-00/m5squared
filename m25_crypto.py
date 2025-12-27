#!/usr/bin/env python3
"""
M25 Cryptographic Operations - AES-128-CBC with a twist of chaos.

The encryption scheme: AES-128-CBC, but the IV is encrypted with ECB first.
Why? Nobody knows. Security through obscurity? Job security? Boredom?

At least they didn't roll their own crypto... entirely.

39C3: We break wheelchairs (with consent and good intentions).
"""

from typing import Optional, Dict, Any, List
import sys

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad
    from Crypto.Random import get_random_bytes
except ImportError:
    print("ERROR: pycryptodome not installed. Run: pip install pycryptodome", file=sys.stderr)
    sys.exit(1)

from m25_protocol import (
    HEADER_MARKER, HEADER_SIZE, IV_SIZE, MIN_PACKET_LENGTH, MAX_FRAME_LENGTH,
    calculate_crc, add_delimiters, remove_delimiters
)


class M25Encryptor:
    """Encrypt M25 SPP packets for transmission."""

    def __init__(self, key: bytes):
        if len(key) != 16:
            raise ValueError(f"Key must be 16 bytes, got {len(key)}")
        self.key = key

    def encrypt(self, spp_data: bytes) -> bytes:
        """Encrypt SPP data into complete M25 packet ready to send."""
        return self.encrypt_verbose(spp_data)['encrypted_packet']

    encrypt_packet = encrypt  # Backward compatibility

    def encrypt_verbose(self, spp_data: bytes) -> Dict[str, Any]:
        """Encrypt with full details. Returns dict with encrypted_packet, iv, crc, etc."""
        padded_data = pad(spp_data, AES.block_size)
        iv = get_random_bytes(IV_SIZE)

        cipher_ecb = AES.new(self.key, AES.MODE_ECB)
        iv_encrypted = cipher_ecb.encrypt(iv)

        cipher_cbc = AES.new(self.key, AES.MODE_CBC, iv)
        encrypted_data = cipher_cbc.encrypt(padded_data)

        payload = iv_encrypted + encrypted_data
        frame_length = HEADER_SIZE + len(payload) + 2 - 1

        if frame_length > MAX_FRAME_LENGTH:
            raise ValueError(f"Packet too large ({frame_length} bytes, max {MAX_FRAME_LENGTH})")

        frame = bytearray([HEADER_MARKER, (frame_length >> 8) & 0xFF, frame_length & 0xFF])
        frame.extend(payload)

        crc = calculate_crc(bytes(frame), len(frame))
        frame.append((crc >> 8) & 0xFF)
        frame.append(crc & 0xFF)

        packet_raw = bytes(frame)
        packet_stuffed = add_delimiters(packet_raw)

        return {
            'encrypted_packet': packet_stuffed,
            'encrypted_packet_raw': packet_raw,
            'iv': iv,
            'iv_encrypted': iv_encrypted,
            'encrypted_data': encrypted_data,
            'frame_length': frame_length,
            'crc': crc
        }


class M25Decryptor:
    """Decrypt M25 SPP packets received from wheels."""

    def __init__(self, key: bytes):
        if len(key) != 16:
            raise ValueError(f"Key must be 16 bytes, got {len(key)}")
        self.key = key

    def decrypt(self, packet_bytes: bytes, validate_padding: bool = True) -> Optional[bytes]:
        """Decrypt M25 packet. Returns SPP data or None on failure."""
        result = self.decrypt_verbose(packet_bytes)
        if result is None:
            return None

        decrypted = result['decrypted']
        if validate_padding:
            try:
                return unpad(decrypted, AES.block_size)
            except ValueError:
                return None
        return decrypted

    decrypt_packet = decrypt  # Backward compatibility

    def decrypt_verbose(self, packet_bytes: bytes) -> Optional[Dict[str, Any]]:
        """Decrypt with full details. Returns dict or None on failure."""
        destuffed = remove_delimiters(packet_bytes)
        if destuffed is None:
            return None

        if len(destuffed) < MIN_PACKET_LENGTH:
            return None
        if destuffed[0] != HEADER_MARKER:
            return None

        frame_length = (destuffed[1] << 8) | destuffed[2]
        if frame_length > MAX_FRAME_LENGTH or len(destuffed) <= frame_length:
            return None

        crc_received = (destuffed[frame_length - 1] << 8) | destuffed[frame_length]
        crc_calculated = calculate_crc(destuffed, frame_length - 1)
        if crc_calculated != crc_received:
            return None

        iv_encrypted = destuffed[HEADER_SIZE:HEADER_SIZE + IV_SIZE]
        cipher_ecb = AES.new(self.key, AES.MODE_ECB)
        iv = cipher_ecb.decrypt(iv_encrypted)

        data_start = HEADER_SIZE + IV_SIZE
        encrypted_data_length = frame_length - data_start - 2 + 1
        encrypted_data = destuffed[data_start:data_start + encrypted_data_length]

        cipher_cbc = AES.new(self.key, AES.MODE_CBC, iv)
        decrypted_data = cipher_cbc.decrypt(encrypted_data)

        return {
            'decrypted': decrypted_data,
            'packet_bytes': destuffed,
            'frame_length': frame_length,
            'iv_encrypted': iv_encrypted,
            'iv': iv,
            'encrypted_data': encrypted_data,
            'crc_received': crc_received,
            'crc_calculated': crc_calculated
        }


class MultiKeyDecryptor:
    """Try multiple AES keys until one succeeds (validates via PKCS7 padding)."""

    def __init__(self, keys: List[bytes]):
        if not keys:
            raise ValueError("At least one key required")
        self.keys = keys
        self.decryptors = [M25Decryptor(key) for key in keys]

    def decrypt(self, packet_bytes: bytes) -> Optional[bytes]:
        """Try all keys. Returns decrypted data or None."""
        result = self.decrypt_verbose(packet_bytes)
        if result is None:
            return None
        try:
            return unpad(result['decrypted'], AES.block_size)
        except ValueError:
            return None

    def decrypt_verbose(self, packet_bytes: bytes) -> Optional[Dict[str, Any]]:
        """Try all keys with full details. Adds successful_key* fields to result."""
        for idx, decryptor in enumerate(self.decryptors):
            result = decryptor.decrypt_verbose(packet_bytes)
            if result is None:
                continue
            try:
                unpad(result['decrypted'], AES.block_size)
                result['successful_key'] = self.keys[idx]
                result['successful_key_index'] = idx
                result['key_attempts'] = idx + 1
                return result
            except ValueError:
                continue
        return None


def extract_decrypted_data(result: Dict[str, Any], include_padding: bool = False) -> bytes:
    """Extract decrypted data from verbose result."""
    decrypted = result['decrypted']
    if include_padding:
        return decrypted
    return unpad(decrypted, AES.block_size)


def extract_complete_packet(result: Dict[str, Any]) -> bytes:
    """Extract complete destuffed packet from verbose result."""
    return result['packet_bytes'][:result['frame_length'] + 1]
