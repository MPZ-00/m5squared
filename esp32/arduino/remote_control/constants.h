/*
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
#define POS_PROTOCOL_ID                0
#define POS_TELEGRAM_ID                1
#define POS_SOURCE_ID                  2
#define POS_DEST_ID                    3
#define POS_SERVICE_ID                 4
#define POS_PARAM_ID                   5
#define POS_PAYLOAD                    6

#define MIN_SPP_PACKET_SIZE        6

// ---------------------------------------------------------------------------
// Protocol IDs
// ---------------------------------------------------------------------------
#define PROTOCOL_ID_NOT_USED           0x00
#define PROTOCOL_ID_STANDARD           0x01

// ---------------------------------------------------------------------------
// Device IDs (Source/Destination)
// ---------------------------------------------------------------------------
#define DEVICE_M25_WHEEL_COMMON        1
#define DEVICE_M25_WHEEL_LEFT          2
#define DEVICE_M25_WHEEL_RIGHT         3
#define DEVICE_ECS                     4
#define DEVICE_SMARTPHONE              5
#define DEVICE_UNISERVICE              6
#define DEVICE_PROD_TEST               7
#define DEVICE_DEBUG_APP               15

// ---------------------------------------------------------------------------
// Service IDs
// ---------------------------------------------------------------------------
#define SERVICE_ARBITRATION            0
#define SERVICE_APP_MGMT               1
#define SERVICE_ACTOR_BUZZER           2
#define SERVICE_ACTOR_LEDS             3
#define SERVICE_ACTOR_MOTOR            4
#define SERVICE_ECS_DISPLAY            5
#define SERVICE_ACTOR_PUSH_RIM_SENSOR  6
#define SERVICE_ACTOR_ROTOR_POS_SENSOR 7
#define SERVICE_BATT_MGMT              8
#define SERVICE_MEMORY_MGMT            9
#define SERVICE_VERSION_MGMT           10
#define SERVICE_STATS                  11
#define SERVICE_SPECIAL_MODE_MGMT      12
#define SERVICE_RTC                    14
#define SERVICE_BT_INFO                16
#define SERVICE_SYS_ERROR_MGMT         20
#define SERVICE_KEY_MGMT               24
#define SERVICE_DEBUG_MANAGEMENT       125
#define SERVICE_GENERAL_ERROR_MGMT     127

// ---------------------------------------------------------------------------
// ACK/NACK Parameters
// ---------------------------------------------------------------------------
#define PARAM_ACK                      0xFF
#define PARAM_ACK_LONG                 0x01
#define NACK_GENERAL                   0x80
#define NACK_SID                       0x81
#define NACK_PID                       0x82
#define NACK_LENGTH                    0x83
#define NACK_CHKSUM                    0x84
#define NACK_COND                      0x85
#define NACK_SEC_ACC                   0x86
#define NACK_CMD_NOT_EXEC              0x87
#define NACK_CMD_INTERNAL_ERROR        0x88

// ---------------------------------------------------------------------------
// APP_MGMT Service Parameters
// ---------------------------------------------------------------------------
#define PARAM_INTENDED_DISCONNECT      0x10
#define PARAM_WRITE_SYSTEM_MODE        0x10
#define PARAM_READ_SYSTEM_MODE         0x11
#define PARAM_WRITE_DRIVE_MODE         0x20
#define PARAM_READ_DRIVE_MODE          0x21
#define PARAM_STATUS_DRIVE_MODE        0x22
#define PARAM_WRITE_REMOTE_SPEED       0x30
#define PARAM_WRITE_ASSIST_LEVEL       0x40
#define PARAM_READ_ASSIST_LEVEL        0x41
#define PARAM_STATUS_ASSIST_LEVEL      0x42
#define PARAM_WRITE_DRIVE_PROFILE_PARAMS 0x50
#define PARAM_READ_DRIVE_PROFILE_PARAMS 0x51
#define PARAM_STATUS_DRIVE_PROFILE_PARAMS 0x52
#define PARAM_WRITE_DRIVE_PROFILE      0x60
#define PARAM_READ_DRIVE_PROFILE       0x61
#define PARAM_STATUS_DRIVE_PROFILE     0x62
#define PARAM_WRITE_AUTO_SHUTOFF_TIME  0x80
#define PARAM_READ_AUTO_SHUTOFF_TIME   0x81
#define PARAM_STATUS_AUTO_SHUTOFF_TIME 0x82
#define PARAM_READ_CURRENT_SPEED       0x91
#define PARAM_STATUS_CURRENT_SPEED     0x92
#define PARAM_START_FACTORY_RESET      0xA0
#define PARAM_READ_FACTORY_RESET_STATE 0xA1
#define PARAM_STATUS_FACTORY_RESET     0xA2
#define PARAM_TRIGGER_SW_RESET         0xB0
#define PARAM_START_PAIRING_PROCESS    0xC0
#define PARAM_READ_PAIRING_PROCESS_STATE 0xC1
#define PARAM_STATUS_PAIRING_PROCESS   0xC2
#define PARAM_READ_CRUISE_VALUES       0xD1
#define PARAM_CRUISE_VALUES            0xD2
#define PARAM_WRITE_DUO_DRIVE_PARAMS   0xF0
#define PARAM_READ_DUO_DRIVE_PARAMS    0xF1
#define PARAM_STATUS_DUO_DRIVE_PARAMS  0xF2

// ---------------------------------------------------------------------------
// BATT_MGMT Service Parameters
// ---------------------------------------------------------------------------
#define PARAM_READ_SOC                 0x11
#define PARAM_STATUS_SOC               0x12

// ---------------------------------------------------------------------------
// VERSION_MGMT Service Parameters
// ---------------------------------------------------------------------------
#define PARAM_READ_SW_VERSION          0x21
#define PARAM_STATUS_SW_VERSION        0x22
#define PARAM_READ_HW_VERSION          0x41
#define PARAM_STATUS_HW_VERSION        0x42

// ---------------------------------------------------------------------------
// System Mode Values
// ---------------------------------------------------------------------------
#define SYSTEM_MODE_CONNECT            0x01
#define SYSTEM_MODE_STANDBY            0x02

// ---------------------------------------------------------------------------
// Drive Mode Bit Flags
// ---------------------------------------------------------------------------
#define DRIVE_MODE_NORMAL              0x00
#define DRIVE_MODE_BIT_AUTO_HOLD       0x01
#define DRIVE_MODE_BIT_CRUISE          0x02
#define DRIVE_MODE_BIT_REMOTE          0x04

// ---------------------------------------------------------------------------
// Assist Levels (sent to wheel)
// ---------------------------------------------------------------------------
#define ASSIST_LEVEL_1                 0
#define ASSIST_LEVEL_2                 1
#define ASSIST_LEVEL_3                 2

// ---------------------------------------------------------------------------
// Drive Profile IDs
// ---------------------------------------------------------------------------
#define PROFILE_CUSTOMIZED             0
#define PROFILE_STANDARD               1
#define PROFILE_SENSITIVE              2
#define PROFILE_SOFT                   3
#define PROFILE_ACTIVE                 4
#define PROFILE_SENSITIVE_PLUS         5

// ---------------------------------------------------------------------------
// Helper macros
// ---------------------------------------------------------------------------
#define IS_ACK(param_id)        ((param_id) == PARAM_ACK)
#define IS_NACK(param_id)       ((param_id) >= NACK_GENERAL && (param_id) <= NACK_CMD_INTERNAL_ERROR)

#endif // CONSTANTS_H
