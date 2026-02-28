#!/usr/bin/env python3
"""
Generate C++ constants header from Python protocol definitions.

Reads m25_protocol_data.py and generates constants.h for ESP32 Arduino.
"""

import sys
import os
from pathlib import Path

# Add parent directory to path to import m25_protocol_data
sys.path.insert(0, str(Path(__file__).parent.parent))

import m25_protocol_data as proto


def generate_constants_header(output_path: str):
    """Generate constants.h from m25_protocol_data.py"""
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("""/*
 * constants.h - Protocol constants for M25 wheels
 * 
 * AUTO-GENERATED from m25_protocol_data.py
 * DO NOT EDIT MANUALLY - run tools/generate_constants.py to regenerate
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Packet Structure Positions
// ---------------------------------------------------------------------------
""")
        
        # Packet positions
        positions = [
            ('POS_PROTOCOL_ID', proto.POS_PROTOCOL_ID),
            ('POS_TELEGRAM_ID', proto.POS_TELEGRAM_ID),
            ('POS_SOURCE_ID', proto.POS_SOURCE_ID),
            ('POS_DEST_ID', proto.POS_DEST_ID),
            ('POS_SERVICE_ID', proto.POS_SERVICE_ID),
            ('POS_PARAM_ID', proto.POS_PARAM_ID),
            ('POS_PAYLOAD', proto.POS_PAYLOAD),
        ]
        
        for name, value in positions:
            f.write(f"#define {name:30} {value}\n")
        
        f.write(f"\n#define MIN_SPP_PACKET_SIZE        {proto.MIN_SPP_PACKET_SIZE}\n")
        
        # Protocol IDs
        f.write("""
// ---------------------------------------------------------------------------
// Protocol IDs
// ---------------------------------------------------------------------------
""")
        protocol_ids = [
            ('PROTOCOL_ID_NOT_USED', proto.PROTOCOL_ID_NOT_USED),
            ('PROTOCOL_ID_STANDARD', proto.PROTOCOL_ID_STANDARD),
        ]
        
        for name, value in protocol_ids:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # Source/Destination IDs
        f.write("""
// ---------------------------------------------------------------------------
// Device IDs (Source/Destination)
// ---------------------------------------------------------------------------
""")
        device_ids = [
            ('DEVICE_M25_WHEEL_COMMON', proto.SRC_ID_M25_WHEEL_COMMON),
            ('DEVICE_M25_WHEEL_LEFT', proto.SRC_ID_M25_WHEEL_LEFT),
            ('DEVICE_M25_WHEEL_RIGHT', proto.SRC_ID_M25_WHEEL_RIGHT),
            ('DEVICE_ECS', proto.SRC_ID_ECS),
            ('DEVICE_SMARTPHONE', proto.SRC_ID_SMARTPHONE),
            ('DEVICE_UNISERVICE', proto.SRC_ID_UNISERVICE),
            ('DEVICE_PROD_TEST', proto.SRC_ID_PROD_TEST),
            ('DEVICE_DEBUG_APP', proto.SRC_ID_DEBUG_APP),
        ]
        
        for name, value in device_ids:
            f.write(f"#define {name:30} {value}\n")
        
        # Service IDs
        f.write("""
// ---------------------------------------------------------------------------
// Service IDs
// ---------------------------------------------------------------------------
""")
        service_ids = [
            ('SERVICE_ARBITRATION', proto.SERVICE_ID_ARBITRATION),
            ('SERVICE_APP_MGMT', proto.SERVICE_ID_APP_MGMT),
            ('SERVICE_ACTOR_BUZZER', proto.SERVICE_ID_ACTOR_BUZZER),
            ('SERVICE_ACTOR_LEDS', proto.SERVICE_ID_ACTOR_LEDS),
            ('SERVICE_ACTOR_MOTOR', proto.SERVICE_ID_ACTOR_MOTOR),
            ('SERVICE_ECS_DISPLAY', proto.SERVICE_ID_ECS_DISPLAY),
            ('SERVICE_ACTOR_PUSH_RIM_SENSOR', proto.SERVICE_ID_ACTOR_PUSH_RIM_SENSOR),
            ('SERVICE_ACTOR_ROTOR_POS_SENSOR', proto.SERVICE_ID_ACTOR_ROTOR_POS_SENSOR),
            ('SERVICE_BATT_MGMT', proto.SERVICE_ID_BATT_MGMT),
            ('SERVICE_MEMORY_MGMT', proto.SERVICE_ID_MEMORY_MGMT),
            ('SERVICE_VERSION_MGMT', proto.SERVICE_ID_VERSION_MGMT),
            ('SERVICE_STATS', proto.SERVICE_ID_STATS),
            ('SERVICE_SPECIAL_MODE_MGMT', proto.SERVICE_ID_SPECIAL_MODE_MGMT),
            ('SERVICE_RTC', proto.SERVICE_ID_RTC),
            ('SERVICE_BT_INFO', proto.SERVICE_ID_BT_INFO),
            ('SERVICE_SYS_ERROR_MGMT', proto.SERVICE_ID_SYS_ERROR_MGMT),
            ('SERVICE_KEY_MGMT', proto.SERVICE_ID_KEY_MGMT),
            ('SERVICE_DEBUG_MANAGEMENT', proto.SERVICE_ID_DEBUG_MANAGEMENT),
            ('SERVICE_GENERAL_ERROR_MGMT', proto.SERVICE_ID_GENERAL_ERROR_MGMT),
        ]
        
        for name, value in service_ids:
            f.write(f"#define {name:30} {value}\n")
        
        # ACK/NACK Parameters
        f.write("""
// ---------------------------------------------------------------------------
// ACK/NACK Parameters
// ---------------------------------------------------------------------------
""")
        ack_nack = [
            ('PARAM_ACK', proto.PARAM_ID_ACK),
            ('PARAM_ACK_LONG', proto.PARAM_ID_ACK_LONG),
            ('NACK_GENERAL', proto.NACK_GENERAL),
            ('NACK_SID', proto.NACK_SID),
            ('NACK_PID', proto.NACK_PID),
            ('NACK_LENGTH', proto.NACK_LENGTH),
            ('NACK_CHKSUM', proto.NACK_CHKSUM),
            ('NACK_COND', proto.NACK_COND),
            ('NACK_SEC_ACC', proto.NACK_SEC_ACC),
            ('NACK_CMD_NOT_EXEC', proto.NACK_CMD_NOT_EXEC),
            ('NACK_CMD_INTERNAL_ERROR', proto.NACK_CMD_INTERNAL_ERROR),
        ]
        
        for name, value in ack_nack:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # APP_MGMT Service Parameters
        f.write("""
// ---------------------------------------------------------------------------
// APP_MGMT Service Parameters
// ---------------------------------------------------------------------------
""")
        app_mgmt_params = [
            ('PARAM_INTENDED_DISCONNECT', proto.PARAM_ID_INTENDED_DISCONNECT),
            ('PARAM_WRITE_SYSTEM_MODE', proto.PARAM_ID_WRITE_SYSTEM_MODE),
            ('PARAM_READ_SYSTEM_MODE', proto.PARAM_ID_READ_SYSTEM_MODE),
            ('PARAM_WRITE_DRIVE_MODE', proto.PARAM_ID_WRITE_DRIVE_MODE),
            ('PARAM_READ_DRIVE_MODE', proto.PARAM_ID_READ_DRIVE_MODE),
            ('PARAM_STATUS_DRIVE_MODE', proto.PARAM_ID_STATUS_DRIVE_MODE),
            ('PARAM_WRITE_REMOTE_SPEED', proto.PARAM_ID_WRITE_REMOTE_SPEED),
            ('PARAM_WRITE_ASSIST_LEVEL', proto.PARAM_ID_WRITE_ASSIST_LEVEL),
            ('PARAM_READ_ASSIST_LEVEL', proto.PARAM_ID_READ_ASSIST_LEVEL),
            ('PARAM_STATUS_ASSIST_LEVEL', proto.PARAM_ID_STATUS_ASSIST_LEVEL),
            ('PARAM_WRITE_DRIVE_PROFILE_PARAMS', proto.PARAM_ID_WRITE_DRIVE_PROFILE_PARAMS),
            ('PARAM_READ_DRIVE_PROFILE_PARAMS', proto.PARAM_ID_READ_DRIVE_PROFILE_PARAMS),
            ('PARAM_STATUS_DRIVE_PROFILE_PARAMS', proto.PARAM_ID_STATUS_DRIVE_PROFILE_PARAMS),
            ('PARAM_WRITE_DRIVE_PROFILE', proto.PARAM_ID_WRITE_DRIVE_PROFILE),
            ('PARAM_READ_DRIVE_PROFILE', proto.PARAM_ID_READ_DRIVE_PROFILE),
            ('PARAM_STATUS_DRIVE_PROFILE', proto.PARAM_ID_STATUS_DRIVE_PROFILE),
            ('PARAM_WRITE_AUTO_SHUTOFF_TIME', proto.PARAM_ID_WRITE_AUTO_SHUTOFF_TIME),
            ('PARAM_READ_AUTO_SHUTOFF_TIME', proto.PARAM_ID_READ_AUTO_SHUTOFF_TIME),
            ('PARAM_STATUS_AUTO_SHUTOFF_TIME', proto.PARAM_ID_STATUS_AUTO_SHUTOFF_TIME),
            ('PARAM_READ_CURRENT_SPEED', proto.PARAM_ID_READ_CURRENT_SPEED),
            ('PARAM_STATUS_CURRENT_SPEED', proto.PARAM_ID_STATUS_CURRENT_SPEED),
            ('PARAM_START_FACTORY_RESET', proto.PARAM_ID_START_FACTORY_RESET),
            ('PARAM_READ_FACTORY_RESET_STATE', proto.PARAM_ID_READ_FACTORY_RESET_STATE),
            ('PARAM_STATUS_FACTORY_RESET', proto.PARAM_ID_STATUS_FACTORY_RESET),
            ('PARAM_TRIGGER_SW_RESET', proto.PARAM_ID_TRIGGER_SW_RESET),
            ('PARAM_START_PAIRING_PROCESS', proto.PARAM_ID_START_PAIRING_PROCESS),
            ('PARAM_READ_PAIRING_PROCESS_STATE', proto.PARAM_ID_READ_PAIRING_PROCESS_STATE),
            ('PARAM_STATUS_PAIRING_PROCESS', proto.PARAM_ID_STATUS_PAIRING_PROCESS),
            ('PARAM_READ_CRUISE_VALUES', proto.PARAM_ID_READ_CRUISE_VALUES),
            ('PARAM_CRUISE_VALUES', proto.PARAM_ID_CRUISE_VALUES),
            ('PARAM_WRITE_DUO_DRIVE_PARAMS', proto.PARAM_ID_WRITE_DUO_DRIVE_PARAMS),
            ('PARAM_READ_DUO_DRIVE_PARAMS', proto.PARAM_ID_READ_DUO_DRIVE_PARAMS),
            ('PARAM_STATUS_DUO_DRIVE_PARAMS', proto.PARAM_ID_STATUS_DUO_DRIVE_PARAMS),
        ]
        
        for name, value in app_mgmt_params:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # Battery Management Parameters
        f.write("""
// ---------------------------------------------------------------------------
// BATT_MGMT Service Parameters
// ---------------------------------------------------------------------------
""")
        batt_params = [
            ('PARAM_READ_SOC', proto.PARAM_ID_READ_SOC),
            ('PARAM_STATUS_SOC', proto.PARAM_ID_STATUS_SOC),
        ]
        
        for name, value in batt_params:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # Version Management Parameters
        f.write("""
// ---------------------------------------------------------------------------
// VERSION_MGMT Service Parameters
// ---------------------------------------------------------------------------
""")
        version_params = [
            ('PARAM_READ_SW_VERSION', proto.PARAM_ID_READ_SW_VERSION),
            ('PARAM_STATUS_SW_VERSION', proto.PARAM_ID_STATUS_SW_VERSION),
            ('PARAM_READ_HW_VERSION', proto.PARAM_ID_READ_HW_VERSION),
            ('PARAM_STATUS_HW_VERSION', proto.PARAM_ID_STATUS_HW_VERSION),
        ]
        
        for name, value in version_params:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # System/Drive Mode Values
        f.write("""
// ---------------------------------------------------------------------------
// System Mode Values
// ---------------------------------------------------------------------------
""")
        system_modes = [
            ('SYSTEM_MODE_CONNECT', proto.SYSTEM_MODE_CONNECT),
            ('SYSTEM_MODE_STANDBY', proto.SYSTEM_MODE_STANDBY),
        ]
        
        for name, value in system_modes:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        f.write("""
// ---------------------------------------------------------------------------
// Drive Mode Bit Flags
// ---------------------------------------------------------------------------
""")
        drive_mode_bits = [
            ('DRIVE_MODE_NORMAL', 0x00),
            ('DRIVE_MODE_BIT_AUTO_HOLD', proto.DRIVE_MODE_BIT_AUTO_HOLD),
            ('DRIVE_MODE_BIT_CRUISE', proto.DRIVE_MODE_BIT_CRUISE),
            ('DRIVE_MODE_BIT_REMOTE', proto.DRIVE_MODE_BIT_REMOTE),
        ]
        
        for name, value in drive_mode_bits:
            f.write(f"#define {name:30} 0x{value:02X}\n")
        
        # Assist Levels
        f.write("""
// ---------------------------------------------------------------------------
// Assist Levels (sent to wheel)
// ---------------------------------------------------------------------------
""")
        assist_levels = [
            ('ASSIST_LEVEL_1', proto.ASSIST_LEVEL_1),
            ('ASSIST_LEVEL_2', proto.ASSIST_LEVEL_2),
            ('ASSIST_LEVEL_3', proto.ASSIST_LEVEL_3),
        ]
        
        for name, value in assist_levels:
            f.write(f"#define {name:30} {value}\n")
        
        # Profile IDs
        f.write("""
// ---------------------------------------------------------------------------
// Drive Profile IDs
// ---------------------------------------------------------------------------
""")
        profile_ids = [
            ('PROFILE_CUSTOMIZED', proto.PROFILE_ID_CUSTOMIZED),
            ('PROFILE_STANDARD', proto.PROFILE_ID_STANDARD),
            ('PROFILE_SENSITIVE', proto.PROFILE_ID_SENSITIVE),
            ('PROFILE_SOFT', proto.PROFILE_ID_SOFT),
            ('PROFILE_ACTIVE', proto.PROFILE_ID_ACTIVE),
            ('PROFILE_SENSITIVE_PLUS', proto.PROFILE_ID_SENSITIVE_PLUS),
        ]
        
        for name, value in profile_ids:
            f.write(f"#define {name:30} {value}\n")
        
        # Helper function to check ACK
        f.write("""
// ---------------------------------------------------------------------------
// Helper macros
// ---------------------------------------------------------------------------
#define IS_ACK(param_id)        ((param_id) == PARAM_ACK)
#define IS_NACK(param_id)       ((param_id) >= NACK_GENERAL && (param_id) <= NACK_CMD_INTERNAL_ERROR)

#endif // CONSTANTS_H
""")
    
    print(f"Generated: {output_path}")
    print(f"  - {len([n for n, _ in positions + protocol_ids + device_ids + service_ids + ack_nack + app_mgmt_params + batt_params + version_params + system_modes + drive_mode_bits + assist_levels + profile_ids])} constants defined")


def main():
    """Generate constants.h"""
    script_dir = Path(__file__).parent
    output_path = script_dir / '../esp32/arduino/remote_control/constants.h'
    
    print("Generating constants.h from m25_protocol_data.py...")
    generate_constants_header(str(output_path))
    print("Done!")


if __name__ == '__main__':
    main()
