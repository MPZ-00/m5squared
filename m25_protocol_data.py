#!/usr/bin/env python3
"""
M25 Protocol Data Layer Constants

All the magic numbers that make wheelchairs go brrr.
Lovingly documented so you don't have to guess what 0x50 means at 3am.

39C3: "Your Wheelchair Can Do WHAT?!"
"""

# Decrypted SPP packet structure (output from m25_decrypt.py --scope data)
POS_PROTOCOL_ID = 0    # Protocol version/type (0x01 = STANDARD)
POS_TELEGRAM_ID = 1    # Sequence number / packet counter
POS_SOURCE_ID = 2      # Source device ID (who sent)
POS_DEST_ID = 3        # Destination device ID (who receives)
POS_SERVICE_ID = 4     # Service/subsystem ID
POS_PARAM_ID = 5       # Parameter/command ID
POS_PAYLOAD = 6        # Start of payload data (variable length)

MIN_SPP_PACKET_SIZE = 6  # Minimum: PROT + TEL + SRC + DEST + SRV + PARAM

# Protocol IDs
PROTOCOL_ID_NOT_USED = 0x00
PROTOCOL_ID_STANDARD = 0x01  # Standard M25 protocol mode

# Device IDs
SRC_ID_M25_WHEEL_COMMON = 1
SRC_ID_M25_WHEEL_LEFT = 2
SRC_ID_M25_WHEEL_RIGHT = 3
SRC_ID_ECS = 4
SRC_ID_SMARTPHONE = 5
SRC_ID_UNISERVICE = 6
SRC_ID_PROD_TEST = 7
SRC_ID_DEBUG_APP = 15

# Human-readable source name mapping
SOURCE_NAMES = {
    1: "M25_WHEEL_COMMON",
    2: "M25_WHEEL_LEFT",
    3: "M25_WHEEL_RIGHT",
    4: "ECS",
    5: "SMARTPHONE",
    6: "UNISERVICE",
    7: "PROD_TEST",
    15: "DEBUG_APP"
}

# Device IDs
DEST_ID_M25_WHEEL_COMMON = 1
DEST_ID_M25_WHEEL_LEFT = 2
DEST_ID_M25_WHEEL_RIGHT = 3
DEST_ID_ECS = 4
DEST_ID_SMARTPHONE = 5
DEST_ID_UNISERVICE = 6
DEST_ID_PROD_TEST = 7
DEST_ID_BROADCAST = 15
DEST_ID_DEBUG_APP = 15  # Same as BROADCAST

# Human-readable destination name mapping
DEST_NAMES = {
    1: "M25_WHEEL_COMMON",
    2: "M25_WHEEL_LEFT",
    3: "M25_WHEEL_RIGHT",
    4: "ECS",
    5: "SMARTPHONE",
    6: "UNISERVICE",
    7: "PROD_TEST",
    15: "BROADCAST/DEBUG_APP"
}

# Service IDs
SERVICE_ID_ARBITRATION = 0
SERVICE_ID_APP_MGMT = 1
SERVICE_ID_ACTOR_BUZZER = 2
SERVICE_ID_ACTOR_LEDS = 3
SERVICE_ID_ACTOR_MOTOR = 4
SERVICE_ID_ECS_DISPLAY = 5
SERVICE_ID_ACTOR_PUSH_RIM_SENSOR = 6
SERVICE_ID_ACTOR_ROTOR_POS_SENSOR = 7
SERVICE_ID_BATT_MGMT = 8
SERVICE_ID_MEMORY_MGMT = 9
SERVICE_ID_VERSION_MGMT = 10
SERVICE_ID_STATS = 11
SERVICE_ID_SPECIAL_MODE_MGMT = 12
SERVICE_ID_RTC = 14
SERVICE_ID_BT_INFO = 16
SERVICE_ID_SYS_ERROR_MGMT = 20
SERVICE_ID_KEY_MGMT = 24
SERVICE_ID_DEBUG_MANAGEMENT = 125
SERVICE_ID_GENERAL_ERROR_MGMT = 127

# Human-readable service name mapping
SERVICE_NAMES = {
    0: "ARBITRATION",
    1: "APP_MGMT",
    2: "ACTOR_BUZZER",
    3: "ACTOR_LEDS",
    4: "ACTOR_MOTOR",
    5: "ECS_DISPLAY",
    6: "ACTOR_PUSH_RIM_SENSOR",
    7: "ACTOR_ROTOR_POS_SENSOR",
    8: "BATT_MGMT",
    9: "MEMORY_MGMT",
    10: "VERSION_MGMT",
    11: "STATS",
    12: "SPECIAL_MODE_MGMT",
    14: "RTC",
    16: "BT_INFO",
    20: "SYS_ERROR_MGMT",
    24: "KEY_MGMT",
    125: "DEBUG_MANAGEMENT",
    127: "GENERAL_ERROR_MGMT"
}

# Parameter IDs - ACK/NACK
PARAM_ID_ACK = 0xFF
PARAM_ID_ACK_LONG = 0x01  # Not directly in constants, inferred

# NACK error codes (lines 47-55)
NACK_GENERAL = 0x80        # -128 in signed byte
NACK_SID = 0x81            # -127
NACK_PID = 0x82            # -126
NACK_LENGTH = 0x83         # -125
NACK_CHKSUM = 0x84         # -124
NACK_COND = 0x85           # -123
NACK_SEC_ACC = 0x86        # -122
NACK_CMD_NOT_EXEC = 0x87   # -121
NACK_CMD_INTERNAL_ERROR = 0x88  # -120

# Parameter IDs - APP_MGMT (Service ID 1)
PARAM_ID_INTENDED_DISCONNECT = 0x10
PARAM_ID_WRITE_SYSTEM_MODE = 0x10  # Same as INTENDED_DISCONNECT (context-dependent)
PARAM_ID_READ_SYSTEM_MODE = 0x11
# Note: 0x12 would be STATUS_SYSTEM_MODE

# Drive Mode Management
PARAM_ID_WRITE_DRIVE_MODE = 0x20
PARAM_ID_READ_DRIVE_MODE = 0x21
PARAM_ID_STATUS_DRIVE_MODE = 0x22

# Remote Speed Control
PARAM_ID_WRITE_REMOTE_SPEED = 0x30

# Assist Level Management
PARAM_ID_WRITE_ASSIST_LEVEL = 0x40
PARAM_ID_READ_ASSIST_LEVEL = 0x41
PARAM_ID_STATUS_ASSIST_LEVEL = 0x42

# Drive Profile Parameters (ASSIST_LEVEL_PARAMETERS)
PARAM_ID_WRITE_DRIVE_PROFILE_PARAMS = 0x50
PARAM_ID_READ_DRIVE_PROFILE_PARAMS = 0x51
PARAM_ID_STATUS_DRIVE_PROFILE_PARAMS = 0x52

# Drive Profile Selection
PARAM_ID_WRITE_DRIVE_PROFILE = 0x60
PARAM_ID_READ_DRIVE_PROFILE = 0x61
PARAM_ID_STATUS_DRIVE_PROFILE = 0x62

# Auto Shutoff Time
PARAM_ID_WRITE_AUTO_SHUTOFF_TIME = 0x80    # -128
PARAM_ID_READ_AUTO_SHUTOFF_TIME = 0x81     # -127
PARAM_ID_STATUS_AUTO_SHUTOFF_TIME = 0x82   # -126

# Current Speed Reading
PARAM_ID_READ_CURRENT_SPEED = 0x91         # -111
PARAM_ID_STATUS_CURRENT_SPEED = 0x92       # -110

# Factory Reset
PARAM_ID_START_FACTORY_RESET = 0xA0        # -96
PARAM_ID_READ_FACTORY_RESET_STATE = 0xA1   # -95
PARAM_ID_STATUS_FACTORY_RESET = 0xA2       # -94

# Software Reset
PARAM_ID_TRIGGER_SW_RESET = 0xB0           # -80

# Pairing Process
PARAM_ID_START_PAIRING_PROCESS = 0xC0      # -64
PARAM_ID_READ_PAIRING_PROCESS_STATE = 0xC1 # -63
PARAM_ID_STATUS_PAIRING_PROCESS = 0xC2     # -62

# Cruise Control Values
PARAM_ID_READ_CRUISE_VALUES = 0xD1         # -47
PARAM_ID_CRUISE_VALUES = 0xD2              # -46

# DuoDrive Parameters
PARAM_ID_WRITE_DUO_DRIVE_PARAMS = 0xF0     # -16
PARAM_ID_READ_DUO_DRIVE_PARAMS = 0xF1      # -15
PARAM_ID_STATUS_DUO_DRIVE_PARAMS = 0xF2    # -14

# Parameter IDs - ACTOR_BUZZER (Service ID 2)
PARAM_ID_WRITE_BUZZER_VOL = 0x10
PARAM_ID_READ_BUZZER_VOL = 0x11
PARAM_ID_STATUS_BUZZER_VOL = 0x12
PARAM_ID_WRITE_BUZZER_ON_OFF = 0x20
PARAM_ID_READ_BUZZER_ON_OFF = 0x21
PARAM_ID_STATUS_BUZZER_ON_OFF = 0x22

# Parameter IDs - ACTOR_LEDS (Service ID 3)
PARAM_ID_WRITE_LED_STATE = 0x10
PARAM_ID_READ_LED_STATE = 0x11
PARAM_ID_STATUS_LED_STATE = 0x12

# Note: ACTOR_MOTOR (Service ID 4) is diagnostic-only, not used by smartphone app

# Parameter IDs - BATT_MGMT (Service ID 8)
PARAM_ID_START_DISCHARGE = 0x20
PARAM_ID_READ_DISCHARGE_STATE = 0x21
PARAM_ID_STATUS_DISCHARGE = 0x22
PARAM_ID_STOP_DISCHARGE = 0x23
PARAM_ID_READ_SOC = 0x11
PARAM_ID_STATUS_SOC = 0x12
PARAM_ID_READ_BMS_STATE = 0x71
PARAM_ID_STATUS_BMS_STATE = 0x72

# Parameter IDs - MEMORY_MGMT (Service ID 9)
PARAM_ID_READ_MEMORY_PREPARATION = 0x10
PARAM_ID_READ_MEMORY_BLOCK = 0x11
PARAM_ID_READ_MEMORY_DATA = 0x12
PARAM_ID_WRITE_MEMORY_PREPARATION = 0x20
PARAM_ID_WRITE_MEMORY_BLOCK = 0x24
PARAM_ID_ERASE_MEMORY = 0x30
PARAM_ID_WRITE_MOUNTING_SIDE = 0x70
PARAM_ID_READ_MOUNTING_SIDE = 0x71
PARAM_ID_STATUS_MOUNTING_SIDE = 0x72

# Parameter IDs - VERSION_MGMT (Service ID 10)
PARAM_ID_WRITE_SW_VERSION = 0x20
PARAM_ID_READ_SW_VERSION = 0x21
PARAM_ID_STATUS_SW_VERSION = 0x22
PARAM_ID_READ_HW_VERSION = 0x41
PARAM_ID_STATUS_HW_VERSION = 0x42

# Parameter IDs - SPECIAL_MODE_MGMT (Service ID 12)
PARAM_ID_WRITE_SYS_MODE = 0x10
PARAM_ID_READ_SYS_MODE = 0x11
PARAM_ID_STATUS_SYS_MODE = 0x12
PARAM_ID_WRITE_TEST_PERIOD_STATE = 0x20
PARAM_ID_READ_TEST_PERIOD_STATE = 0x21
PARAM_ID_STATUS_TEST_PERIOD_STATE = 0x22

# Parameter IDs - RTC (Service ID 14)
PARAM_ID_WRITE_RTC_TIME = 0x10
PARAM_ID_READ_RTC_TIME = 0x11
PARAM_ID_STATUS_RTC_TIME = 0x12

# Parameter IDs - BT_INFO (Service ID 16)
PARAM_ID_READ_BT_MAC = 0x11

# Parameter IDs - SYS_ERROR_MGMT (Service ID 20)
PARAM_ID_RESET_ERROR = 0x10
PARAM_ID_READ_GENERAL_ERROR = 0x11
PARAM_ID_STATUS_GENERAL_ERROR = 0x12

# Parameter IDs - KEY_MGMT (Service ID 24)
PARAM_ID_SET_ACCESSORY_KEY = 0x10

# Parameter names organized by service for context-aware lookups
PARAM_NAMES_BY_SERVICE = {
    1: {  # APP_MGMT - Application Management (ALL smartphone app functions)
        0x10: "INTENDED_DISCONNECT / WRITE_SYSTEM_MODE",  # Context-dependent
        0x11: "READ_SYSTEM_MODE",
        0x12: "STATUS_SYSTEM_MODE",  # Response to READ_SYSTEM_MODE
        0x20: "WRITE_DRIVE_MODE",
        0x21: "READ_DRIVE_MODE",
        0x22: "STATUS_DRIVE_MODE",
        0x30: "WRITE_REMOTE_SPEED",
        0x40: "WRITE_ASSIST_LEVEL",
        0x41: "READ_ASSIST_LEVEL",
        0x42: "STATUS_ASSIST_LEVEL",
        0x50: "WRITE_DRIVE_PROFILE_PARAMS",  # ASSIST_LEVEL_PARAMETERS
        0x51: "READ_DRIVE_PROFILE_PARAMS",
        0x52: "STATUS_DRIVE_PROFILE_PARAMS",
        0x60: "WRITE_DRIVE_PROFILE",
        0x61: "READ_DRIVE_PROFILE",
        0x62: "STATUS_DRIVE_PROFILE",
        0x70: "WRITE_MOUNTING_SIDE",  # Also in MEMORY_MGMT but used via APP_MGMT
        0x71: "READ_MOUNTING_SIDE",
        0x72: "STATUS_MOUNTING_SIDE",
        0x80: "WRITE_AUTO_SHUTOFF_TIME",
        0x81: "READ_AUTO_SHUTOFF_TIME",
        0x82: "STATUS_AUTO_SHUTOFF_TIME",
        0x91: "READ_CURRENT_SPEED",
        0x92: "STATUS_CURRENT_SPEED",
        0xA0: "START_FACTORY_RESET",
        0xA1: "READ_FACTORY_RESET_STATE",
        0xA2: "STATUS_FACTORY_RESET",
        0xB0: "TRIGGER_SW_RESET",
        0xC0: "START_PAIRING_PROCESS",
        0xC1: "READ_PAIRING_PROCESS_STATE",
        0xC2: "STATUS_PAIRING_PROCESS",
        0xD1: "READ_CRUISE_VALUES",
        0xD2: "CRUISE_VALUES",
        0xF0: "WRITE_DUO_DRIVE_PARAMS",
        0xF1: "READ_DUO_DRIVE_PARAMS",
        0xF2: "STATUS_DUO_DRIVE_PARAMS"
    },
    2: {  # ACTOR_BUZZER
        0x10: "WRITE_BUZZER_VOL",
        0x11: "READ_BUZZER_VOL",
        0x12: "STATUS_BUZZER_VOL",
        0x20: "WRITE_BUZZER_ON_OFF",
        0x21: "READ_BUZZER_ON_OFF",
        0x22: "STATUS_BUZZER_ON_OFF"
    },
    3: {  # ACTOR_LEDS
        0x10: "WRITE_LED_STATE",
        0x11: "READ_LED_STATE",
        0x12: "STATUS_LED_STATE"
    },
    # Service ID 4: ACTOR_MOTOR - Not used by M25 smartphone app
    # (Diagnostic-only service for Intellion maintenance tool)
    7: {  # ACTOR_ROTOR_POS_SENSOR (Intellion maintenance)
        0x31: "READ_RESOLVER_CALIBRATION",
        0x32: "STATUS_RESOLVER_CALIBRATION",
        0x41: "READ_SENSOR_TEMPERATURE",
        0x42: "STATUS_SENSOR_TEMPERATURE"
    },
    8: {  # BATT_MGMT
        0x11: "READ_SOC",
        0x12: "STATUS_SOC",
        0x20: "START_DISCHARGE",
        0x21: "READ_DISCHARGE_STATE",
        0x22: "STATUS_DISCHARGE",
        0x23: "STOP_DISCHARGE",
        # Intellion additional params
        0x31: "READ_BATTERY_CELL_BALANCED",
        0x32: "STATUS_BATTERY_CELL_BALANCED",
        0x41: "READ_BATTERY_DISCHARGE_COMPLETELY",
        0x42: "STATUS_BATTERY_DISCHARGE_COMPLETELY",
        0x51: "READ_BATTERY_DETERMINED_CAPACITY",
        0x52: "STATUS_BATTERY_DETERMINED_CAPACITY",
        0x61: "READ_BATTERY_NOMINAL_CAPACITY",
        0x62: "STATUS_BATTERY_NOMINAL_CAPACITY",
        0x71: "READ_BMS_STATE",
        0x72: "STATUS_BMS_STATE"
    },
    9: {  # MEMORY_MGMT
        0x10: "READ_MEMORY_PREPARATION",
        0x11: "READ_MEMORY_BLOCK",
        0x12: "READ_MEMORY_DATA",
        0x20: "WRITE_MEMORY_PREPARATION",
        0x24: "WRITE_MEMORY_BLOCK",
        0x30: "ERASE_MEMORY",
        0x70: "WRITE_MOUNTING_SIDE",
        0x71: "READ_MOUNTING_SIDE",
        0x72: "STATUS_MOUNTING_SIDE"
    },
    10: {  # VERSION_MGMT
        0x20: "WRITE_SW_VERSION",
        0x21: "READ_SW_VERSION",
        0x22: "STATUS_SW_VERSION",
        0x31: "READ_BOOTLOADER_VERSION",
        0x32: "STATUS_BOOTLOADER_VERSION",
        0x41: "READ_HW_VERSION",
        0x42: "STATUS_HW_VERSION"
    },
    11: {  # STATS - Statistics service (Intellion)
        0x11: "READ_STATS_MOMENTARY",
        0x12: "STATUS_STATS_MOMENTARY",
        0x21: "READ_STATS_SYSTEM",
        0x22: "STATUS_STATS_SYSTEM",
        0x31: "READ_STATS_BATTERY",
        0x32: "STATUS_STATS_BATTERY"
    },
    12: {  # SPECIAL_MODE_MGMT
        0x10: "WRITE_SYS_MODE",
        0x11: "READ_SYS_MODE",
        0x12: "STATUS_SYS_MODE",
        0x20: "WRITE_TEST_PERIOD_STATE",
        0x21: "READ_TEST_PERIOD_STATE",
        0x22: "STATUS_TEST_PERIOD_STATE"
    },
    14: {  # RTC
        0x10: "WRITE_RTC_TIME",
        0x11: "READ_RTC_TIME",
        0x12: "STATUS_RTC_TIME"
    },
    16: {  # BT_INFO
        0x11: "READ_BT_MAC"
    },
    20: {  # SYS_ERROR_MGMT
        0x10: "RESET_ERROR",
        0x11: "READ_GENERAL_ERROR",
        0x12: "STATUS_GENERAL_ERROR",
        # Intellion additional params
        0x21: "READ_ERROR_COUNTER_FRAM",
        0x22: "STATUS_ERROR_COUNTER_FRAM",
        0x30: "WRITE_ERASE_ERROR_COUNTER",
        0x50: "WRITE_ERASE_ERROR_MEMORY"
    },
    24: {  # KEY_MGMT
        0x10: "SET_ACCESSORY_KEY"
    },
    127: {  # GENERAL_ERROR_MGMT
        0x10: "RESET_GENERAL_ERROR",
        0x11: "READ_GENERAL_ERROR",
        0x12: "STATUS_GENERAL_ERROR"
    },
    # Service ID 125: DEBUG_MANAGEMENT - Parameters moved to APP_MGMT (Service ID 1)
    # (AUTO_SHUTOFF_TIME and TRIGGER_SW_RESET belong to APP_MGMT per Android source)
}


def get_source_name(source_id):
    """
    Get human-readable source device name

    Args:
        source_id (int): Source device ID byte

    Returns:
        str: Device name or "UNKNOWN_SRC_0xXX"
    """
    return SOURCE_NAMES.get(source_id, f"UNKNOWN_SRC_0x{source_id:02X}")


def get_dest_name(dest_id):
    """
    Get human-readable destination device name

    Args:
        dest_id (int): Destination device ID byte

    Returns:
        str: Device name or "UNKNOWN_DEST_0xXX"
    """
    return DEST_NAMES.get(dest_id, f"UNKNOWN_DEST_0x{dest_id:02X}")


def get_service_name(service_id):
    """
    Get human-readable service name

    Args:
        service_id (int): Service ID byte

    Returns:
        str: Service name or "UNKNOWN_SRV_0xXX"
    """
    return SERVICE_NAMES.get(service_id, f"UNKNOWN_SRV_0x{service_id:02X}")


def get_param_name(service_id, param_id):
    """
    Get human-readable parameter name (context-aware by service)

    Args:
        service_id (int): Service ID byte
        param_id (int): Parameter ID byte

    Returns:
        str: Parameter name or "PARAM_0xXX"
    """
    if param_id == PARAM_ID_ACK:
        return "ACK"

    if service_id in PARAM_NAMES_BY_SERVICE:
        return PARAM_NAMES_BY_SERVICE[service_id].get(
            param_id,
            f"PARAM_0x{param_id:02X}"
        )

    return f"PARAM_0x{param_id:02X}"


def is_valid_protocol_id(protocol_id):
    """
    Validate protocol ID

    Args:
        protocol_id (int): Protocol ID byte

    Returns:
        bool: True if valid M25 protocol ID
    """
    return protocol_id in [PROTOCOL_ID_NOT_USED, PROTOCOL_ID_STANDARD]


# Payload Structures

# STATUS_DRIVE_PROFILE_PARAMS (0x52) - 10 bytes
# Service: APP_MGMT (1), Response to READ_DRIVE_PROFILE_PARAMS (0x51)
#
# Byte layout (0-indexed from payload start, i.e. after header):
#   Byte 0:    max_torque        - Motor support level % (10-100, clamped)
#   Bytes 1-2: max_speed         - Big-endian short (mm/s, *0.0036 for km/h)
#                                  Valid: 556-2361 (2.0-8.5 km/h in 0.5 km/h steps)
#   Bytes 3-4: p_factor          - Big-endian short (1-999)
#   Byte 5:    speed_bias        - Valid values: 10,20,30,40,50
#   Byte 6:    speed_factor      - Range 1-100
#   Byte 7:    rotation_thres    - Rotation threshold (1-150)
#   Byte 8:    slope_inc         - Slope increase (valid: 70,56,42,28,20)
#   Byte 9:    slope_dec         - Slope decrease (valid: 70,56,42,28,20)
#
# Note: Assist level is determined by state machine context, not in payload
PAYLOAD_DRIVE_PROFILE_PARAMS = {
    'size': 10,
    'fields': [
        (0, 1, 'max_torque', 'uint8', '% motor support (10-100)'),
        (1, 2, 'max_speed', 'uint16_be', 'mm/s (*0.0036 for km/h)'),
        (3, 2, 'p_factor', 'uint16_be', 'P-factor (1-999)'),
        (5, 1, 'speed_bias', 'uint8', 'Speed bias (10,20,30,40,50)'),
        (6, 1, 'speed_factor', 'uint8', 'Speed factor (1-100)'),
        (7, 1, 'rotation_thres', 'uint8', 'Rotation threshold (1-150)'),
        (8, 1, 'slope_inc', 'uint8', 'Slope increase (70,56,42,28,20)'),
        (9, 1, 'slope_dec', 'uint8', 'Slope decrease (70,56,42,28,20)'),
    ]
}

# CRUISE_VALUES (0xD2) - 13 bytes (optionally 14 with error byte)
# Service: APP_MGMT (1), Response to READ_CRUISE_VALUES (0xD1)
# Byte layout:
#   Byte 0:    drive_mode        - DriveMode flags (bit0=auto_hold, bit1=cruise, bit2=remote)
#   Byte 1:    push_rim_value    - Push rim sensor deflection (signed byte)
#   Bytes 2-3: current_speed     - Big-endian short (0.001 km/h units, multiply by 0.001)
#   Byte 4:    soc               - State of charge % (0-100)
#   Bytes 5-8: overall_distance  - Big-endian int (0.01 m units, multiply by 0.01)
#   Bytes 9-10: push_counter     - Big-endian short (rim push count since startup)
#   Byte 11:   wheel_error       - Error code (0 = no error)
#   Byte 12:   (optional) extended data if packet length == 22
PAYLOAD_CRUISE_VALUES = {
    'size': 12,  # minimum, can be 13 with extended data
    'fields': [
        (0, 1, 'drive_mode', 'uint8', 'Flags: bit0=auto_hold, bit1=cruise, bit2=remote'),
        (1, 1, 'push_rim_value', 'int8', 'Push rim sensor deflection'),
        (2, 2, 'current_speed', 'uint16_be', '0.001 km/h units'),
        (4, 1, 'soc', 'uint8', 'State of charge %'),
        (5, 4, 'overall_distance', 'uint32_be', '0.01 meter units'),
        (9, 2, 'push_counter', 'uint16_be', 'Push rim deflection count'),
        (11, 1, 'wheel_error', 'uint8', 'Error code (0=none)'),
    ]
}

# STATUS_RTC_TIME (0x12) - 6 bytes
# Service: RTC (14), Response to READ_RTC_TIME (0x11)
# Byte layout:
#   Bytes 0-1: year    - Big-endian short (e.g., 0x07E9 = 2025)
#   Byte 2:    month   - 1-12
#   Byte 3:    day     - 1-31
#   Byte 4:    hour    - 0-23
#   Byte 5:    minute  - 0-59
PAYLOAD_RTC_TIME = {
    'size': 6,
    'fields': [
        (0, 2, 'year', 'uint16_be', 'Year (e.g., 2025)'),
        (2, 1, 'month', 'uint8', 'Month 1-12'),
        (3, 1, 'day', 'uint8', 'Day 1-31'),
        (4, 1, 'hour', 'uint8', 'Hour 0-23'),
        (5, 1, 'minute', 'uint8', 'Minute 0-59'),
    ]
}

# STATUS_SW_VERSION (0x22) - 4 bytes
# Service: VERSION_MGMT (10), Response to READ_SW_VERSION (0x21)
# Byte layout:
#   Byte 0: dev_state     - Development state char (e.g., 'V'=0x56 for release)
#   Byte 1: version       - Major version (2 digits, e.g., 03)
#   Byte 2: revision      - Minor version (3 digits, e.g., 005)
#   Byte 3: test_version  - Test/patch version (3 digits)
#
# Example: 0x56030500 = "V03.005.000"
PAYLOAD_SW_VERSION = {
    'size': 4,
    'fields': [
        (0, 1, 'dev_state', 'char', 'Development state (V=release)'),
        (1, 1, 'version', 'uint8', 'Major version'),
        (2, 1, 'revision', 'uint8', 'Minor/revision'),
        (3, 1, 'test_version', 'uint8', 'Test/patch version'),
    ]
}

# STATUS_HW_VERSION (0x42) - 1 byte
# Service: VERSION_MGMT (10), Response to READ_HW_VERSION (0x41)
PAYLOAD_HW_VERSION = {
    'size': 1,
    'fields': [
        (0, 1, 'hw_version', 'uint8', 'Hardware version number'),
    ]
}

# STATUS_DISCHARGE (0x22) - 4 bytes
# Service: BATT_MGMT (8), Response to READ_DISCHARGE_STATE (0x21)
#
# Byte layout:
#   Byte 0:    current_soc       - Current state of charge %
#   Byte 1:    target_soc        - Target discharge SOC %
#   Bytes 2-3: remaining_seconds - Big-endian short (remaining discharge time)
PAYLOAD_DISCHARGE_STATE = {
    'size': 4,
    'fields': [
        (0, 1, 'current_soc', 'uint8', 'Current SOC %'),
        (1, 1, 'target_soc', 'uint8', 'Target discharge SOC %'),
        (2, 2, 'remaining_seconds', 'uint16_be', 'Remaining discharge time (seconds)'),
    ]
}

# STATUS_BMS_STATE (0x72) - 2+ bytes
# Service: BATT_MGMT (8), Response to READ_BMS_STATE (0x71)
#
# Byte layout:
#   Byte 0: unknown (possibly flags)
#   Byte 1: is_charging - 0=not charging, non-zero=charging
PAYLOAD_BMS_STATE = {
    'size': 2,
    'fields': [
        (0, 1, 'flags', 'uint8', 'BMS status flags'),
        (1, 1, 'is_charging', 'uint8', '0=not charging, 1=charging'),
    ]
}

# STATUS_DUO_DRIVE_PARAMS (0xF2) - 3 bytes
# Service: APP_MGMT (1), Response to READ_DUO_DRIVE_PARAMS (0xF1)
#
# Byte layout:
#   Byte 0: mounting_side    - 0=UNKNOWN, 1=RIGHT, 2=LEFT
#   Byte 1: speed_sensibility - Speed sensitivity setting
#   Byte 2: steering_dynamic - Steering dynamic enum
PAYLOAD_DUO_DRIVE_PARAMS = {
    'size': 3,
    'fields': [
        (0, 1, 'mounting_side', 'uint8', '0=UNKNOWN, 1=RIGHT, 2=LEFT'),
        (1, 1, 'speed_sensibility', 'uint8', 'Speed sensitivity'),
        (2, 1, 'steering_dynamic', 'uint8', 'Steering dynamic enum'),
    ]
}

# STATUS_AUTO_SHUTOFF_TIME (0x82) - 2 bytes
# Service: APP_MGMT (1), Response to READ_AUTO_SHUTOFF_TIME (0x81)
#
# Byte layout:
#   Bytes 0-1: shutoff_time - Big-endian short (seconds, default 3600)
PAYLOAD_AUTO_SHUTOFF_TIME = {
    'size': 2,
    'fields': [
        (0, 2, 'shutoff_time', 'uint16_be', 'Auto shutoff time in seconds'),
    ]
}

# STATUS_LED_STATE (0x12) - 1 byte
# Service: ACTOR_LEDS (3), Response to READ_LED_STATE (0x11)
#
# Byte layout:
#   Byte 0: led_state - bit0=LEDs while charging, bit1=LEDs normal operation
PAYLOAD_LED_STATE = {
    'size': 1,
    'fields': [
        (0, 1, 'led_state', 'uint8', 'bit0=charging LEDs, bit1=normal LEDs'),
    ]
}

# Intellion-only Service IDs (maintenance tool, not in smartphone app)
SERVICE_ID_SPECIAL_MODE_PASSWORD = 12  # Same as SPECIAL_MODE_MGMT, password variant
SERVICE_ID_SPECIAL_MODE = 13           # Different from 12 in Intellion
SERVICE_ID_SYSTEM_SIGNALS = 17         # Digital signals
SERVICE_ID_SYSTEM_PL900 = 18           # PL900 battery chip
SERVICE_ID_SYSTEM_CONTROLLER = 19      # Controller diagnostics
SERVICE_ID_SYSTEM_MOTOR = 23           # Motor coil diagnostics
SERVICE_ID_SYSTEM_ENCRYPTION_KEY = 24  # Same as KEY_MGMT

# Intellion-only parameter IDs for ACTOR_MOTOR (Service 4) - diagnostic only
PARAM_ID_MOTOR_SET_ROTATION_SPEED = 0x10  # Direct motor RPM control
PARAM_ID_MOTOR_SET_TORQUE = 0x20          # Direct torque control

# Intellion-only parameter IDs for sensors
PARAM_ID_PUSH_RIM_GET_VALUE = 0x10        # Service 6: read push rim sensor
PARAM_ID_RESOLVER_CALIBRATION = 0x30      # Service 7: resolver calibration
PARAM_ID_SENSOR_TEMPERATURE = 0x40        # Service 7: temperature sensors

# Intellion statistics service (Service 11) parameters
PARAM_ID_STATISTICS_MOMENTARY = 0x10      # Current session stats
PARAM_ID_STATISTICS_SYSTEM = 0x20         # Overall system stats
PARAM_ID_STATISTICS_BATTERY = 0x30        # Battery lifetime stats

# Drive mode flags (used in CRUISE_VALUES payload)
DRIVE_MODE_BIT_AUTO_HOLD = 0x01  # Bit 0: Auto-hold active
DRIVE_MODE_BIT_CRUISE = 0x02     # Bit 1: Cruise mode active
DRIVE_MODE_BIT_REMOTE = 0x04     # Bit 2: Remote control active

# System mode values
SYS_MODE_OFF = 0
SYS_MODE_ON = 1
SYS_MODE_STANDBY = 2
SYS_MODE_FLIGHT = 3

# Mounting side values
MOUNTING_SIDE_UNKNOWN = 0
MOUNTING_SIDE_RIGHT = 1
MOUNTING_SIDE_LEFT = 2

# Test period state values
TEST_PERIOD_NOT_STARTED = 0
TEST_PERIOD_ACTIVE = 1
TEST_PERIOD_ELAPSED = 2
TEST_PERIOD_ERROR = 3

# Factory reset state values
FACTORY_RESET_NOT_STARTED = 0
FACTORY_RESET_IN_PROGRESS = 1
FACTORY_RESET_FINISHED = 2
FACTORY_RESET_ERROR = 3

# Pairing state values
PAIRING_NOT_STARTED = 0
PAIRING_IN_PROGRESS = 1
PAIRING_CONFIRMED = 2
PAIRING_TIMEOUT = 3

# Drive profile presets
PROFILE_ID_CUSTOMIZED = 0
PROFILE_ID_STANDARD = 1
PROFILE_ID_SENSITIVE = 2
PROFILE_ID_SOFT = 3
PROFILE_ID_ACTIVE = 4
PROFILE_ID_SENSITIVE_PLUS = 5

PROFILE_NAMES = {
    PROFILE_ID_CUSTOMIZED: "Customized",
    PROFILE_ID_STANDARD: "Standard",
    PROFILE_ID_SENSITIVE: "Sensitive",
    PROFILE_ID_SOFT: "Soft",
    PROFILE_ID_ACTIVE: "Active",
    PROFILE_ID_SENSITIVE_PLUS: "SensitivePlus"
}

# Valid max_speed values (mm/s units, multiply by 0.0036 to get km/h)
# Values correspond to 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5 km/h
MAX_SPEED_VALUES = [556, 694, 833, 972, 1111, 1250, 1389, 1528, 1667, 1806, 1944, 2083, 2222, 2361]
MAX_SPEED_VALUES_NO_MPP = [556, 694, 833, 972, 1111, 1250, 1389, 1528, 1667]  # Without M++ (up to 6.0 km/h)

# Speed limits (mm/s)
MAX_SUPPORT_SPEED = 2361     # 8.5 km/h (with M++ license)
LO_SUPPORT_SPEED = 1667      # 6.0 km/h (without M++ license)
MAX_SPEED_LEARNING = 833     # 3.0 km/h (learning mode)

# Valid slope_inc/slope_dec values
SLOPE_VALUES = [70, 56, 42, 28, 20]

# Valid speed_bias values
SPEED_BIAS_VALUES = [10, 20, 30, 40, 50]

# Assist level values
ASSIST_LEVEL_1 = 0  # Normal / Standard
ASSIST_LEVEL_2 = 1  # Outdoor
ASSIST_LEVEL_3 = 2  # Learning mode (Intellion maintenance only)

ASSIST_LEVEL_NAMES = {
    ASSIST_LEVEL_1: "Normal",
    ASSIST_LEVEL_2: "Outdoor",
    ASSIST_LEVEL_3: "Learning"
}

# Buzzer state and volume values
BUZZER_OFF = 0
BUZZER_ON = 1
BUZZER_VOL_MIN = 0
BUZZER_VOL_MAX = 1

# LED settings bit flags (used in PAYLOAD_LED_STATE, Service 3)
LED_SETTINGS_BIT_ON_CHARGING = 0x01   # bit0: Enable LEDs while charging
LED_SETTINGS_BIT_ON_NORMAL = 0x02     # bit1: Enable LEDs during normal operation
LED_SETTINGS_BIT_OFF_CHARGING = 0xFE  # ~bit0: Disable charging LEDs (AND mask)
LED_SETTINGS_BIT_OFF_NORMAL = 0xFD    # ~bit1: Disable normal LEDs (AND mask)
LED_SETTINGS_DEFAULT = 0x03           # Both enabled (charging | normal)

# System mode constants (for WRITE_SYSTEM_MODE, PARAM_ID 0x10)
SYSTEM_MODE_CONNECT = 0x01    # Connect/initialize communication
SYSTEM_MODE_STANDBY = 0x02    # Enter standby mode

# Drive mode constants
DRIVE_MODE_NORMAL = 0x00      # Normal drive mode (no special flags)
DRIVE_MODE_REMOTE = 0x04      # Remote control mode (alias for DRIVE_MODE_BIT_REMOTE)
DRIVE_MODE_MAX_VALUE = 0x07   # All bits set (auto_hold | cruise | remote)

# OFF masks (AND with current value to clear bit)
DRIVE_MODE_BIT_AUTO_HOLD_OFF = 0xFE  # ~0x01
DRIVE_MODE_BIT_CRUISE_OFF = 0xFD    # ~0x02
DRIVE_MODE_BIT_REMOTE_OFF = 0xFB    # ~0x04

# Error codes
ERR_CHARGER_REMOTEMODE = 62           # Charger connected while in remote mode
ERR_REMOTEMODE_PUSHRIMSENS = 63       # Push rim sensor error in remote mode
RESET_ERR_REMOTEMODE_PUSHRIMSENS = 1  # Reset command value for push rim error

# ACK payload values (not param IDs - PARAM_ID_ACK = 0xFF)
ACK_VALUE = 0        # Standard acknowledgment payload
ACK_LONG_VALUE = 1   # Extended acknowledgment payload

# Default values
DEFAULT_AUTO_SHUTOFF_TIME = 3600  # 1 hour in seconds
DEFAULT_TELEGRAM_ID = 0x80        # Starting telegram ID (signed: -128)
CRUISE_PACKET_LENGTH = 22         # Expected cruise values packet length

# Stats service parameters (Service ID 11)
PARAM_ID_READ_STATS_MOMENTARY = 0x11
PARAM_ID_STATUS_STATS_MOMENTARY = 0x12
PARAM_ID_READ_STATS_SYSTEM = 0x21
PARAM_ID_STATUS_STATS_SYSTEM = 0x22
PARAM_ID_READ_STATS_BATTERY = 0x31
PARAM_ID_STATUS_STATS_BATTERY = 0x32

# Additional BATT_MGMT parameters (Intellion)
PARAM_ID_READ_BATTERY_SHIPPING = 0x21
PARAM_ID_STATUS_BATTERY_SHIPPING = 0x22
PARAM_ID_READ_BATTERY_CELL_BALANCED = 0x31
PARAM_ID_STATUS_BATTERY_CELL_BALANCED = 0x32
PARAM_ID_READ_BATTERY_DISCHARGE_COMPLETELY = 0x41
PARAM_ID_STATUS_BATTERY_DISCHARGE_COMPLETELY = 0x42
PARAM_ID_READ_BATTERY_DETERMINED_CAPACITY = 0x51
PARAM_ID_STATUS_BATTERY_DETERMINED_CAPACITY = 0x52
PARAM_ID_READ_BATTERY_NOMINAL_CAPACITY = 0x61
PARAM_ID_STATUS_BATTERY_NOMINAL_CAPACITY = 0x62

# VERSION_MGMT additional parameters
PARAM_ID_READ_BOOTLOADER_VERSION = 0x31
PARAM_ID_STATUS_BOOTLOADER_VERSION = 0x32

# Sensor service parameters (Service ID 7)
PARAM_ID_READ_RESOLVER_CALIBRATION = 0x31
PARAM_ID_STATUS_RESOLVER_CALIBRATION = 0x32
PARAM_ID_READ_SENSOR_TEMPERATURE = 0x41
PARAM_ID_STATUS_SENSOR_TEMPERATURE = 0x42

# SYS_ERROR_MGMT additional parameters (Service ID 20)
PARAM_ID_READ_SYS_ERROR_COUNTER_FRAM = 0x21
PARAM_ID_STATUS_SYS_ERROR_COUNTER_FRAM = 0x22
PARAM_ID_WRITE_SYS_ERROR_ERASE_COUNTER = 0x30
PARAM_ID_WRITE_SYS_ERROR_ERASE_MEMORY = 0x50

# Memory block IDs (VirtBlockID - for MEMORY_MGMT operations)
MEMORY_BLOCK_PRODUCTION_DATE_PCB = 3
MEMORY_BLOCK_SERIAL_NUMBER_PCB = 4
MEMORY_BLOCK_PRODUCTION_DATE_WHEEL = 5
MEMORY_BLOCK_SERIAL_NUMBER_WHEEL = 6
MEMORY_BLOCK_ALLOWED_CELL_TYPE = 8
MEMORY_BLOCK_BRAKE_CHOPPER_RESISTANCE = 9
MEMORY_BLOCK_CHARGER_VOLTAGE_OFF = 10
MEMORY_BLOCK_CHARGER_VOLTAGE_ON = 11
MEMORY_BLOCK_CHARGER_CURRENT_MAX = 12
MEMORY_BLOCK_CHARGER_CURRENT_MIN = 13
MEMORY_BLOCK_WHEEL_CIRCUMFERENCE = 14
MEMORY_BLOCK_MOTOR_TORQUE = 15
MEMORY_BLOCK_MOTOR_PHASE_CURRENT_MAX = 16
MEMORY_BLOCK_MOTOR_COIL_RESISTANCE = 17
MEMORY_BLOCK_TEMP_AT_COIL_RES_ADJUST = 18
MEMORY_BLOCK_PUSH_RIM_TOLERANCE = 19
MEMORY_BLOCK_CAL_PUSH_RIM = 20
MEMORY_BLOCK_CAL_RSLV_ANG_OFFS = 21
MEMORY_BLOCK_CAL_BATT_VOLTAGE = 22
MEMORY_BLOCK_CAL_MOTOR_VOLTAGE = 23
MEMORY_BLOCK_CAL_CHARGER_VOLTAGE = 24
MEMORY_BLOCK_CAL_PL900 = 25
MEMORY_BLOCK_BRAKE_WARN_TEMP = 26
MEMORY_BLOCK_BRAKE_ERROR_TEMP = 27
MEMORY_BLOCK_COIL_TEMP_WARN = 28
MEMORY_BLOCK_COIL_TEMP_ERROR = 29
MEMORY_BLOCK_BOARD_TEMP_WARN = 30
MEMORY_BLOCK_BOARD_TEMP_ERROR = 31
MEMORY_BLOCK_SYS_TEMP_HYSTERESIS = 32
MEMORY_BLOCK_UPDATE_ALLOWED = 48
MEMORY_BLOCK_DEFAULT_DRIVING_PROFILE = 50
MEMORY_BLOCK_DEFAULT_ASSIST_LEVEL = 51
MEMORY_BLOCK_DEFAULT_ASSIST_LEVEL_PARAMS_1 = 52
MEMORY_BLOCK_DEFAULT_ASSIST_LEVEL_PARAMS_2 = 53
MEMORY_BLOCK_DEFAULT_ASSIST_LEVEL_PARAMS_3 = 54
MEMORY_BLOCK_DEFAULT_AUTO_SHUTOFF_TIME = 55
MEMORY_BLOCK_DEFAULT_BUZZER_VOLUME = 56
MEMORY_BLOCK_DEFAULT_BUZZER_STATE = 57
MEMORY_BLOCK_DEFAULT_SOC_LED_STATE = 58
MEMORY_BLOCK_MAX_VELOCITY_SINCE_INIT = 62
MEMORY_BLOCK_CONNECTIONS_USB = 68
MEMORY_BLOCK_CONNECTIONS_BTCL = 69
MEMORY_BLOCK_CONNECTIONS_BLE = 70
MEMORY_BLOCK_BLUETOOTH_ABORTS = 71
MEMORY_BLOCK_UPDATE_HISTORY = 72
MEMORY_BLOCK_PCB_CHANGES = 75
MEMORY_BLOCK_BATTERY_CHANGES = 76
MEMORY_BLOCK_ERROR_MEMORY_POINTER = 77
MEMORY_BLOCK_APP_INFO = 80
MEMORY_BLOCK_ERROR_DATA_MEMORY = 139
MEMORY_BLOCK_AES_128_KEY = 144
MEMORY_BLOCK_DUODRIVE_FLAG = 145

# Firmware sectors (for firmware update)
MEMORY_BLOCK_FIRMWARE_SECTOR_H = 132
MEMORY_BLOCK_FIRMWARE_SECTOR_G = 133
MEMORY_BLOCK_FIRMWARE_SECTOR_F = 134
MEMORY_BLOCK_FIRMWARE_SECTOR_E = 135
MEMORY_BLOCK_FIRMWARE_SECTOR_D = 136
MEMORY_BLOCK_FIRMWARE_SECTOR_C = 137
MEMORY_BLOCK_FIRMWARE_SECTOR_B = 138
