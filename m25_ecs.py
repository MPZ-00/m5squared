#!/usr/bin/env python3
"""
M25 ECS Remote - Talk to your wheelchair like it's 2024.

Because the official app doesn't let you read raw sensor data or do
anything fun. We fixed that.

Features the manufacturer didn't want you to have:
- Read ALL the parameters (not just the "safe" ones)
- Actually understand what your wheels are thinking

39C3: Liberating mobility devices, one byte at a time.
"""

import argparse
import sys
import socket
import json
import time

# Import M25 protocol utilities
from m25_protocol import HEADER_MARKER
from m25_crypto import M25Encryptor, M25Decryptor
from m25_utils import parse_key
from m25_spp import BluetoothConnection, PacketBuilder as PacketBuilderBase
from m25_protocol_data import (
    # Protocol constants
    PROTOCOL_ID_STANDARD, POS_PROTOCOL_ID, POS_TELEGRAM_ID, POS_SOURCE_ID,
    POS_DEST_ID, POS_SERVICE_ID, POS_PARAM_ID, POS_PAYLOAD, MIN_SPP_PACKET_SIZE,
    # Device IDs
    SRC_ID_SMARTPHONE, DEST_ID_M25_WHEEL_COMMON,
    # Service IDs
    SERVICE_ID_APP_MGMT, SERVICE_ID_BATT_MGMT, SERVICE_ID_VERSION_MGMT,
    # Parameter IDs - Battery
    PARAM_ID_READ_SOC, PARAM_ID_STATUS_SOC,
    # Parameter IDs - App Management
    PARAM_ID_READ_ASSIST_LEVEL, PARAM_ID_STATUS_ASSIST_LEVEL,
    PARAM_ID_READ_DRIVE_MODE, PARAM_ID_STATUS_DRIVE_MODE,
    PARAM_ID_READ_DRIVE_PROFILE, PARAM_ID_STATUS_DRIVE_PROFILE,
    PARAM_ID_READ_DRIVE_PROFILE_PARAMS, PARAM_ID_STATUS_DRIVE_PROFILE_PARAMS,
    PARAM_ID_READ_CRUISE_VALUES, PARAM_ID_CRUISE_VALUES,
    # Parameter IDs - Version
    PARAM_ID_READ_SW_VERSION, PARAM_ID_STATUS_SW_VERSION,
    # Parameter IDs - Write commands
    PARAM_ID_WRITE_ASSIST_LEVEL, PARAM_ID_WRITE_DRIVE_MODE,
    PARAM_ID_WRITE_DRIVE_PROFILE, PARAM_ID_WRITE_DRIVE_PROFILE_PARAMS,
    # Drive mode flags
    DRIVE_MODE_BIT_AUTO_HOLD, DRIVE_MODE_BIT_CRUISE, DRIVE_MODE_BIT_REMOTE,
    # Profile IDs and names
    PROFILE_ID_CUSTOMIZED, PROFILE_ID_STANDARD, PROFILE_ID_SENSITIVE,
    PROFILE_ID_SOFT, PROFILE_ID_ACTIVE, PROFILE_ID_SENSITIVE_PLUS, PROFILE_NAMES,
    # Assist levels
    ASSIST_LEVEL_1, ASSIST_LEVEL_2, ASSIST_LEVEL_3, ASSIST_LEVEL_NAMES,
    # ACK/NACK
    PARAM_ID_ACK, NACK_GENERAL,
    # System/Drive modes
    SYSTEM_MODE_CONNECT
)


class ECSPacketBuilder(PacketBuilderBase):
    """Extended PacketBuilder with ECS-specific read/write commands."""

    def build_read_soc(self):
        """Build READ_SOC packet (battery %)."""
        return self.build_packet(SERVICE_ID_BATT_MGMT, PARAM_ID_READ_SOC)

    def build_read_assist_level(self):
        """Build READ_ASSIST_LEVEL packet."""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_ASSIST_LEVEL)

    def build_read_drive_profile(self):
        """Build READ_DRIVE_PROFILE packet (profile selection)"""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_DRIVE_PROFILE)

    def build_read_drive_profile_params(self, assist_level=0):
        """Build READ_DRIVE_PROFILE_PARAMS packet

        Args:
            assist_level: 0 = Normal/Level1 params, 1 = Outdoor/Level2 params
        """
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_DRIVE_PROFILE_PARAMS,
                                 bytes([assist_level]))

    def build_read_cruise_values(self):
        """Build READ_CRUISE_VALUES packet (contains distance, speed, etc)"""
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_READ_CRUISE_VALUES)

    def build_read_sw_version(self):
        """Build READ_SW_VERSION packet (firmware version)"""
        return self.build_packet(SERVICE_ID_VERSION_MGMT, PARAM_ID_READ_SW_VERSION)

    # Write commands
    def build_write_assist_level(self, level):
        """Build WRITE_ASSIST_LEVEL packet

        Args:
            level: 0 = Normal/Level1, 1 = Outdoor/Level2, 2 = Learning/Level3
        """
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_ASSIST_LEVEL,
                                 bytes([level]))

    def build_write_drive_mode(self, auto_hold):
        """Build WRITE_DRIVE_MODE packet (auto hold / hill hold)

        Args:
            auto_hold: True/1 = enable, False/0 = disable
        """
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_MODE,
                                 bytes([1 if auto_hold else 0]))

    def build_write_drive_profile(self, profile_id):
        """Build WRITE_DRIVE_PROFILE packet (select profile)

        Args:
            profile_id: 0=Customized, 1=Standard, 2=Sensitive, 3=Soft, 4=Active, 5=SensitivePlus
        """
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_PROFILE,
                                 bytes([profile_id]))

    def build_write_drive_profile_params(self, assist_level, max_torque, max_speed,
                                          p_factor, speed_bias, speed_factor,
                                          rotation_threshold, slope_inc, slope_dec):
        """Build WRITE_DRIVE_PROFILE_PARAMS packet (10 bytes)

        Args:
            assist_level: 0 = Level1 params, 1 = Level2 params
            max_torque: 10-100 (%)
            max_speed: raw value in mm/s (e.g., 2361 = 8.5 km/h)
            p_factor: 1-999
            speed_bias: 10-50 (sensor sensitivity)
            speed_factor: 1-100
            rotation_threshold: 1-150
            slope_inc: 20-70 (startup time)
            slope_dec: 20-70 (coasting time)
        """
        payload = bytes([
            assist_level,
            max_torque,
            (max_speed >> 8) & 0xFF,  # max_speed high byte
            max_speed & 0xFF,          # max_speed low byte
            (p_factor >> 8) & 0xFF,    # p_factor high byte
            p_factor & 0xFF,           # p_factor low byte
            speed_bias,
            speed_factor,
            rotation_threshold,
            slope_inc,
            slope_dec
        ])
        return self.build_packet(SERVICE_ID_APP_MGMT, PARAM_ID_WRITE_DRIVE_PROFILE_PARAMS,
                                 payload)

class ResponseParser:
    """Parse M25 SPP response packets"""

    @staticmethod
    def parse_header(data):
        """Parse SPP packet header"""
        if len(data) < MIN_SPP_PACKET_SIZE:
            return None
        return {
            'protocol_id': data[POS_PROTOCOL_ID],
            'telegram_id': data[POS_TELEGRAM_ID],
            'source_id': data[POS_SOURCE_ID],
            'dest_id': data[POS_DEST_ID],
            'service_id': data[POS_SERVICE_ID],
            'param_id': data[POS_PARAM_ID],
            'payload': data[POS_PAYLOAD:] if len(data) > POS_PAYLOAD else b''
        }

    @staticmethod
    def is_nack(header):
        """Check if response is a NACK (0x80-0x88 range only)"""
        param_id = header['param_id']
        return 0x80 <= param_id <= 0x88

    @staticmethod
    def is_ack(header):
        """Check if response is an ACK"""
        return header['param_id'] == PARAM_ID_ACK

    @staticmethod
    def parse_soc(payload):
        """Parse STATUS_SOC response - returns battery %"""
        if len(payload) >= 1:
            return payload[0]
        return None

    @staticmethod
    def parse_assist_level(payload):
        """Parse STATUS_ASSIST_LEVEL response"""
        if len(payload) >= 1:
            level = payload[0]
            return {
                'value': level,
                'name': ASSIST_LEVEL_NAMES.get(level, f"Unknown({level})")
            }
        return None

    @staticmethod
    def parse_drive_mode(payload):
        """Parse STATUS_DRIVE_MODE response"""
        if len(payload) >= 1:
            mode = payload[0]
            return {
                'value': mode,
                'auto_hold': bool(mode & DRIVE_MODE_BIT_AUTO_HOLD),
                'cruise': bool(mode & DRIVE_MODE_BIT_CRUISE),
                'remote': bool(mode & DRIVE_MODE_BIT_REMOTE)
            }
        return None

    @staticmethod
    def parse_drive_profile(payload):
        """Parse STATUS_DRIVE_PROFILE response"""
        if len(payload) >= 1:
            profile_id = payload[0]
            return {
                'value': profile_id,
                'name': PROFILE_NAMES.get(profile_id, f"Unknown({profile_id})")
            }
        return None

    @staticmethod
    def parse_drive_profile_params(payload):
        """Parse STATUS_DRIVE_PROFILE_PARAMS response (10 bytes)"""
        if len(payload) >= 10:
            max_speed_raw = (payload[1] << 8) | payload[2]
            p_factor = (payload[3] << 8) | payload[4]
            return {
                'max_torque': payload[0],
                'max_speed': max_speed_raw * 0.0036,  # mm/s to km/h
                'max_speed_raw': max_speed_raw,
                'p_factor': p_factor,
                'speed_bias': payload[5],
                'speed_factor': payload[6],
                'rotation_threshold': payload[7],
                'slope_inc': payload[8],
                'slope_dec': payload[9]
            }
        return None

    @staticmethod
    def parse_cruise_values(payload):
        """Parse CRUISE_VALUES response (12+ bytes)

        Contains: drive_mode, push_rim, speed, soc, distance, push_counter, error
        """
        if len(payload) >= 12:
            # Bytes 5-8: overall_distance (uint32_be, 0.01m units)
            distance_raw = (payload[5] << 24) | (payload[6] << 16) | (payload[7] << 8) | payload[8]
            distance_km = distance_raw * 0.01 / 1000  # 0.01m -> km
            return {
                'distance_km': distance_km,
                'distance_raw': distance_raw
            }
        return None

    @staticmethod
    def parse_sw_version(payload):
        """Parse STATUS_SW_VERSION response (4 bytes)

        Format: dev_state(char), major, minor, patch
        Example: 0x56030500 = "V03.005.000"
        """
        if len(payload) >= 4:
            dev_state = chr(payload[0]) if 0x20 <= payload[0] <= 0x7E else '?'
            major = payload[1]
            minor = payload[2]
            patch = payload[3]
            version_str = f"{dev_state}{major:02d}.{minor:03d}.{patch:03d}"
            return {
                'version_str': version_str,
                'major': major,
                'minor': minor,
                'patch': patch
            }
        return None

class ECSRemote:
    """ECS Remote status reader"""

    # Delay between commands (ms)
    COMMAND_DELAY = 0.15  # 150ms

    def __init__(self, left_conn, right_conn, verbose=False, retries=2):
        self.left_conn = left_conn
        self.right_conn = right_conn
        self.verbose = verbose
        self.retries = retries

    def init_connection(self, conn, builder):
        """Send connection init (WRITE_SYSTEM_MODE = 0x01)"""
        packet = builder.build_write_system_mode(SYSTEM_MODE_CONNECT)
        response = conn.transact(packet)
        if response:
            header = ResponseParser.parse_header(response)
            if header and ResponseParser.is_ack(header):
                return True
            if self.verbose:
                print(f"  Init: unexpected response", file=sys.stderr)
        elif self.verbose:
            print(f"  Init: no response", file=sys.stderr)
        return False

    def read_value(self, conn, build_method, expected_param_id, parse_method):
        """Read a single value from a wheel (with retry)"""
        last_error = None

        for attempt in range(self.retries + 1):
            packet = build_method()
            response = conn.transact(packet)

            if response is None:
                last_error = "no response"
                if attempt < self.retries:
                    time.sleep(0.1)  # Brief delay before retry
                continue

            header = ResponseParser.parse_header(response)
            if header is None:
                last_error = "invalid response"
                continue

            if ResponseParser.is_nack(header):
                last_error = f"NACK 0x{header['param_id']:02X}"
                break  # Don't retry NACKs

            if header['param_id'] != expected_param_id:
                last_error = f"unexpected param_id 0x{header['param_id']:02X}"
                continue

            return parse_method(header['payload'])

        if self.verbose and last_error:
            print(f"  {build_method.__name__}: {last_error}", file=sys.stderr)
        return None

    def write_value(self, conn, packet, command_name):
        """Send a write command and expect ACK (with retry)

        Returns:
            True if ACK received, False otherwise
        """
        last_error = None

        for attempt in range(self.retries + 1):
            response = conn.transact(packet)

            if response is None:
                last_error = "no response"
                if attempt < self.retries:
                    time.sleep(0.1)
                continue

            header = ResponseParser.parse_header(response)
            if header is None:
                last_error = "invalid response"
                continue

            if ResponseParser.is_nack(header):
                last_error = f"NACK 0x{header['param_id']:02X}"
                break  # Don't retry NACKs

            if ResponseParser.is_ack(header):
                return True

            last_error = f"unexpected param_id 0x{header['param_id']:02X}"

        if self.verbose and last_error:
            print(f"  {command_name}: {last_error}", file=sys.stderr)
        return False

    def write_assist_level(self, conn, builder, level):
        """Set assist level on a wheel

        Args:
            level: 0 = Normal/Level1, 1 = Outdoor/Level2, 2 = Learning/Level3

        Returns:
            True if successful
        """
        self.init_connection(conn, builder)
        time.sleep(self.COMMAND_DELAY)
        packet = builder.build_write_assist_level(level)
        return self.write_value(conn, packet, "write_assist_level")

    def write_auto_hold(self, conn, builder, enabled):
        """Set auto hold (hill hold) on a wheel

        Args:
            enabled: True to enable, False to disable

        Returns:
            True if successful
        """
        self.init_connection(conn, builder)
        time.sleep(self.COMMAND_DELAY)
        packet = builder.build_write_drive_mode(enabled)
        return self.write_value(conn, packet, "write_auto_hold")

    def read_profile_params(self, conn, builder, assist_level):
        """Read drive profile params for a specific assist level

        Args:
            assist_level: 0 = Level1/Normal, 1 = Level2/Outdoor

        Returns:
            dict with profile params or None
        """
        self.init_connection(conn, builder)
        time.sleep(self.COMMAND_DELAY)
        return self.read_value(
            conn,
            lambda: builder.build_read_drive_profile_params(assist_level),
            PARAM_ID_STATUS_DRIVE_PROFILE_PARAMS,
            ResponseParser.parse_drive_profile_params
        )

    def write_max_speed(self, conn, builder, assist_level, max_speed_kmh):
        """Set max speed for a specific assist level

        Reads current params, modifies max_speed, writes back all params.

        Args:
            assist_level: 0 = Level1/Normal, 1 = Level2/Outdoor
            max_speed_kmh: Speed in km/h (2.0-8.5 in 0.5 steps)

        Returns:
            True if successful
        """
        # Convert km/h to mm/s
        max_speed_raw = int(max_speed_kmh / 0.0036)

        # Read current params
        params = self.read_profile_params(conn, builder, assist_level)
        if params is None:
            if self.verbose:
                print(f"  Failed to read current profile params", file=sys.stderr)
            return False

        time.sleep(self.COMMAND_DELAY)

        packet = builder.build_write_drive_profile_params(
            assist_level=assist_level,
            max_torque=params['max_torque'],
            max_speed=max_speed_raw,
            p_factor=params['p_factor'],
            speed_bias=params['speed_bias'],
            speed_factor=params['speed_factor'],
            rotation_threshold=params['rotation_threshold'],
            slope_inc=params['slope_inc'],
            slope_dec=params['slope_dec']
        )
        return self.write_value(conn, packet, "write_max_speed")

    def read_wheel_status(self, conn, builder):
        """Read all status values from a wheel"""
        status = {}

        # Init connection first
        self.init_connection(conn, builder)
        time.sleep(self.COMMAND_DELAY)

        # Battery SOC
        soc = self.read_value(conn, builder.build_read_soc,
                              PARAM_ID_STATUS_SOC, ResponseParser.parse_soc)
        status['battery'] = soc
        time.sleep(self.COMMAND_DELAY)

        # Assist level
        assist = self.read_value(conn, builder.build_read_assist_level,
                                 PARAM_ID_STATUS_ASSIST_LEVEL, ResponseParser.parse_assist_level)
        status['assist_level'] = assist
        time.sleep(self.COMMAND_DELAY)

        # Drive mode (auto hold, cruise, remote)
        drive_mode = self.read_value(conn, builder.build_read_drive_mode,
                                     PARAM_ID_STATUS_DRIVE_MODE, ResponseParser.parse_drive_mode)
        status['drive_mode'] = drive_mode
        time.sleep(self.COMMAND_DELAY)

        # Drive profile selection
        profile = self.read_value(conn, builder.build_read_drive_profile,
                                  PARAM_ID_STATUS_DRIVE_PROFILE, ResponseParser.parse_drive_profile)
        status['profile'] = profile
        time.sleep(self.COMMAND_DELAY)

        # Drive profile parameters for current assist level
        current_level = status['assist_level']['value'] if status.get('assist_level') else 0
        params = self.read_value(conn, lambda: builder.build_read_drive_profile_params(current_level),
                                 PARAM_ID_STATUS_DRIVE_PROFILE_PARAMS, ResponseParser.parse_drive_profile_params)
        status['profile_params'] = params
        time.sleep(self.COMMAND_DELAY)

        # Cruise values (contains distance)
        cruise = self.read_value(conn, builder.build_read_cruise_values,
                                 PARAM_ID_CRUISE_VALUES, ResponseParser.parse_cruise_values)
        status['cruise_values'] = cruise
        time.sleep(self.COMMAND_DELAY)

        # Firmware version
        version = self.read_value(conn, builder.build_read_sw_version,
                                  PARAM_ID_STATUS_SW_VERSION, ResponseParser.parse_sw_version)
        status['sw_version'] = version

        return status

    def read_all(self):
        """Read status from both wheels"""
        # Single builder works for both - wheel distinction is at Bluetooth level
        builder = ECSPacketBuilder()

        left_status = self.read_wheel_status(self.left_conn, builder)
        right_status = self.read_wheel_status(self.right_conn, builder)

        return {
            'left': left_status,
            'right': right_status
        }

    @staticmethod
    def format_wheel_status(status, wheel_name, address):
        """Format status for display"""
        lines = [f"{wheel_name} Wheel ({address})"]

        # Battery
        if status.get('battery') is not None:
            lines.append(f"  Battery:           {status['battery']}%")
        else:
            lines.append(f"  Battery:           --")

        # Assist level (0=Level1, 1=Level2, 2=Learning -> display as 1, 2, 3)
        if status.get('assist_level'):
            level_val = status['assist_level']['value'] + 1  # 0-indexed to 1-indexed
            level_name = status['assist_level']['name']
            if level_val == 3:
                lines.append(f"  Assist Level:      {level_val} (Learning Mode)")
            else:
                lines.append(f"  Assist Level:      {level_val}")
        else:
            lines.append(f"  Assist Level:      --")

        # Auto Hold
        if status.get('drive_mode'):
            auto_hold = "On" if status['drive_mode']['auto_hold'] else "Off"
            lines.append(f"  Auto Hold:         {auto_hold}")
        else:
            lines.append(f"  Auto Hold:         --")

        # Profile
        if status.get('profile'):
            lines.append(f"  Profile:           {status['profile']['name']}")
        else:
            lines.append(f"  Profile:           --")

        # Profile parameters
        params = status.get('profile_params')
        if params:
            lines.append(f"  Max Torque:        {params['max_torque']}%")
            lines.append(f"  Max Speed:         {params['max_speed']:.2f} km/h")
            lines.append(f"  Sensor Sens.:      {params['speed_bias']} (speedBias)")
            lines.append(f"  Startup Time:      {params['slope_inc']} (slopeInc)")
            lines.append(f"  Coasting Time:     {params['slope_dec']} (slopeDec)")
            lines.append(f"  P-Factor:          {params['p_factor']}")
            lines.append(f"  Speed Factor:      {params['speed_factor']}")
            lines.append(f"  Rot. Threshold:    {params['rotation_threshold']}")

        # Distance (from cruise values)
        cruise = status.get('cruise_values')
        if cruise:
            lines.append(f"  Distance:          {cruise['distance_km']:.2f} km")
        else:
            lines.append(f"  Distance:          --")

        # Firmware version
        version = status.get('sw_version')
        if version:
            lines.append(f"  Firmware:          {version['version_str']}")
        else:
            lines.append(f"  Firmware:          --")

        return '\n'.join(lines)

    def display_status(self, result, left_addr, right_addr):
        """Display formatted status"""
        print(self.format_wheel_status(result['left'], "Left", left_addr))
        print()
        print(self.format_wheel_status(result['right'], "Right", right_addr))

def dry_run_demo(left_key, right_key):
    """Show what packets would be sent without actually connecting"""
    print("Dry Run Mode - Showing packets that would be sent\n")
    print("Note: SPP payload is identical for both wheels (dest_id=M25_WHEEL_COMMON)")
    print("      Only the encryption differs (each wheel has its own key)\n")

    left_encryptor = M25Encryptor(left_key)
    right_encryptor = M25Encryptor(right_key)

    # Single builder - same packets for both wheels
    builder = ECSPacketBuilder()

    commands = [
        ("WRITE_SYSTEM_MODE (init)", lambda: builder.build_write_system_mode(SYSTEM_MODE_CONNECT)),
        ("READ_SOC", builder.build_read_soc),
        ("READ_ASSIST_LEVEL", builder.build_read_assist_level),
        ("READ_DRIVE_MODE", builder.build_read_drive_mode),
        ("READ_DRIVE_PROFILE", builder.build_read_drive_profile),
        ("READ_DRIVE_PROFILE_PARAMS (level 0)", lambda: builder.build_read_drive_profile_params(0)),
    ]

    for name, cmd in commands:
        spp = cmd()

        left_enc = left_encryptor.encrypt_packet(spp)
        right_enc = right_encryptor.encrypt_packet(spp)

        print(f"{name}:")
        print(f"  SPP:       {spp.hex()}")
        print(f"  Left Enc:  {left_enc.hex()[:60]}...")
        print(f"  Right Enc: {right_enc.hex()[:60]}...")
        print()

def main():
    parser = argparse.ArgumentParser(
        description='M25 ECS Remote Status Reader',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Read status from both wheels
  python m25_ecs.py --left-addr AA:BB:CC:DD:EE:FF --right-addr 11:22:33:44:55:66 \\
                    --left-key HEXKEY --right-key HEXKEY

  # Left wheel only
  python m25_ecs.py --left-addr AA:BB:CC:DD:EE:FF --left-key HEXKEY --left-only

  # Dry run (show packets without sending)
  python m25_ecs.py --left-key HEXKEY --right-key HEXKEY --dry-run

  # Set assist level (1=Normal, 2=Outdoor, 3=Learning)
  python m25_ecs.py --left-addr ... --right-addr ... --left-key ... --right-key ... --set-level 2

  # Toggle auto hold (hill hold)
  python m25_ecs.py ... --set-auto-hold on

  # Set max speed for Normal level (level 1) to 6.0 km/h
  python m25_ecs.py ... --set-max-speed 6.0 --for-level 1

  # Get keys from QR codes
  python m25_qr_to_key.py "QR_CODE_STRING"
"""
    )

    parser.add_argument('--left-addr', metavar='ADDR',
                        help='Left wheel Bluetooth address (XX:XX:XX:XX:XX:XX)')
    parser.add_argument('--right-addr', metavar='ADDR',
                        help='Right wheel Bluetooth address (XX:XX:XX:XX:XX:XX)')
    parser.add_argument('--left-key', metavar='HEX',
                        help='Left wheel AES key (hex, from m25_qr_to_key.py)')
    parser.add_argument('--right-key', metavar='HEX',
                        help='Right wheel AES key (hex, from m25_qr_to_key.py)')
    parser.add_argument('--left-only', action='store_true',
                        help='Only connect to left wheel')
    parser.add_argument('--right-only', action='store_true',
                        help='Only connect to right wheel')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show packets without sending (no Bluetooth required)')
    parser.add_argument('--json', action='store_true',
                        help='Output in JSON format')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output (show errors)')
    parser.add_argument('--debug', action='store_true',
                        help='Show raw TX/RX packets (hex)')
    parser.add_argument('--ping', action='store_true',
                        help='Test connectivity only (send init, check for ACK)')

    # Write commands
    parser.add_argument('--set-level', type=int, choices=[1, 2, 3], metavar='N',
                        help='Set assist level: 1=Normal, 2=Outdoor, 3=Learning')
    parser.add_argument('--set-auto-hold', choices=['on', 'off'],
                        help='Set auto hold (hill hold): on or off')
    parser.add_argument('--set-max-speed', type=float, metavar='KMH',
                        help='Set max speed in km/h (2.0-8.5, requires --for-level)')
    parser.add_argument('--for-level', type=int, choices=[1, 2], metavar='N',
                        help='Target assist level for --set-max-speed: 1=Normal, 2=Outdoor')

    args = parser.parse_args()

    # Validate --set-max-speed requires --for-level
    if args.set_max_speed is not None and args.for_level is None:
        print("ERROR: --set-max-speed requires --for-level (1 or 2)")
        sys.exit(1)

    # Validate speed range
    if args.set_max_speed is not None:
        if args.set_max_speed < 2.0 or args.set_max_speed > 8.5:
            print(f"ERROR: --set-max-speed must be between 2.0 and 8.5 km/h")
            sys.exit(1)

    # Dry run mode (needs both keys) --> this should probably fixed someday, but is fine for now.
    if args.dry_run:
        left_key = parse_key(args.left_key, "left-key")
        right_key = parse_key(args.right_key, "right-key")
        dry_run_demo(left_key, right_key)
        return

    # Determine which wheels to connect
    if args.left_only and args.right_only:
        print("ERROR: Cannot specify both --left-only and --right-only")
        sys.exit(1)

    connect_left = not args.right_only
    connect_right = not args.left_only

    # Validate required addresses
    if connect_left and not args.left_addr:
        print("ERROR: Must specify --left-addr (or use --right-only)")
        sys.exit(1)
    if connect_right and not args.right_addr:
        print("ERROR: Must specify --right-addr (or use --left-only)")
        sys.exit(1)

    # Validate keys
    if connect_left and not args.left_key:
        print("ERROR: Must specify --left-key (or use --right-only)")
        sys.exit(1)
    if connect_right and not args.right_key:
        print("ERROR: Must specify --right-key (or use --left-only)")
        sys.exit(1)

    # Parse keys (only the ones we need / specified)
    left_key = parse_key(args.left_key, "left-key") if connect_left else None
    right_key = parse_key(args.right_key, "right-key") if connect_right else None

    left_conn = None
    right_conn = None
    result = {}

    try:
        if connect_left:
            print(f"Connecting to Left ({args.left_addr})...", end=' ', flush=True)
            left_conn = BluetoothConnection(args.left_addr, left_key, "Left", debug=args.debug)
            left_conn.connect()
            print("OK")

        if connect_right:
            print(f"Connecting to Right ({args.right_addr})...", end=' ', flush=True)
            right_conn = BluetoothConnection(args.right_addr, right_key, "Right", debug=args.debug)
            right_conn.connect()
            print("OK")

        print()

        # dest_id is always M25_WHEEL_COMMON - wheel distinction is at Bluetooth level
        builder = ECSPacketBuilder()
        ecs = ECSRemote(left_conn, right_conn, verbose=args.verbose)

        # Ping mode - just test connectivity
        if args.ping:
            print("Ping test (sending init command)...")
            success = True

            if connect_left:
                ok = ecs.init_connection(left_conn, builder)
                status = "OK" if ok else "FAILED (no ACK)"
                print(f"  Left:  {status}")
                success = success and ok

            if connect_right:
                ok = ecs.init_connection(right_conn, builder)
                status = "OK" if ok else "FAILED (no ACK)"
                print(f"  Right: {status}")
                success = success and ok

            sys.exit(0 if success else 1)

        # Handle write commands
        write_performed = False

        if args.set_level is not None:
            level = args.set_level - 1  # Convert 1-3 to 0-2
            level_names = {0: 'Normal (1)', 1: 'Outdoor (2)', 2: 'Learning (3)'}
            print(f"Setting assist level to {level_names[level]}...")

            if connect_left:
                ok = ecs.write_assist_level(left_conn, builder, level)
                print(f"  Left:  {'OK' if ok else 'FAILED'}")
            if connect_right:
                ok = ecs.write_assist_level(right_conn, builder, level)
                print(f"  Right: {'OK' if ok else 'FAILED'}")
            write_performed = True
            print()

        if args.set_auto_hold is not None:
            enabled = args.set_auto_hold == 'on'
            state_name = 'ON' if enabled else 'OFF'
            print(f"Setting auto hold to {state_name}...")

            if connect_left:
                ok = ecs.write_auto_hold(left_conn, builder, enabled)
                print(f"  Left:  {'OK' if ok else 'FAILED'}")
            if connect_right:
                ok = ecs.write_auto_hold(right_conn, builder, enabled)
                print(f"  Right: {'OK' if ok else 'FAILED'}")
            write_performed = True
            print()

        if args.set_max_speed is not None:
            assist_level = args.for_level - 1  # Convert 1-2 to 0-1
            level_name = 'Normal' if assist_level == 0 else 'Outdoor'
            print(f"Setting max speed to {args.set_max_speed:.1f} km/h for {level_name} (level {args.for_level})...")

            if connect_left:
                ok = ecs.write_max_speed(left_conn, builder, assist_level, args.set_max_speed)
                print(f"  Left:  {'OK' if ok else 'FAILED'}")
            if connect_right:
                ok = ecs.write_max_speed(right_conn, builder, assist_level, args.set_max_speed)
                print(f"  Right: {'OK' if ok else 'FAILED'}")
            write_performed = True
            print()

        # Read status (always, to show current state after writes)
        if connect_left:
            print("Reading Left wheel...")
            result['left'] = ecs.read_wheel_status(left_conn, builder)

        if connect_right:
            print("Reading Right wheel...")
            result['right'] = ecs.read_wheel_status(right_conn, builder)

        print()

        # Output
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            if 'left' in result:
                print(ECSRemote.format_wheel_status(result['left'], "Left", args.left_addr))
                if 'right' in result:
                    print()
            if 'right' in result:
                print(ECSRemote.format_wheel_status(result['right'], "Right", args.right_addr))

    except socket.error as e:
        print(f"FAILED")
        print(f"ERROR: Bluetooth connection failed: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"FAILED")
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        if left_conn:
            left_conn.disconnect()
        if right_conn:
            right_conn.disconnect()


if __name__ == '__main__':
    main()
