/**
 * config.h - Compile-time configuration for the M25 wheel simulator.
 *
 * This is the ONLY file that needs to change between LEFT / RIGHT wheel builds,
 * and between different hardware setups.  Every #define that affects behavior
 * lives here; nothing is scattered across source files.
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// WHEEL IDENTITY
// Uncomment exactly ONE of the two lines below.
// =============================================================================
#define WHEEL_SIDE_LEFT
// #define WHEEL_SIDE_RIGHT

#if defined(WHEEL_SIDE_LEFT)
    #define DEVICE_NAME "M25_FAKE_LEFT"
#elif defined(WHEEL_SIDE_RIGHT)
    #define DEVICE_NAME "M25_FAKE_RIGHT"
#else
    #error "Define either WHEEL_SIDE_LEFT or WHEEL_SIDE_RIGHT"
#endif

// =============================================================================
// TRANSPORT SELECTION
// Enable one or both transports.
// RFCOMM = Bluetooth Classic SPP (matches real wheel + m25_spp.py).
// BLE    = BLE GATT (original fake_m25_wheel style; useful for BLE-only hosts).
// =============================================================================
#define TRANSPORT_RFCOMM_ENABLED  1   // Bluetooth Classic SPP
#define TRANSPORT_BLE_ENABLED     1   // BLE GATT (disable when using RFCOMM only)

// RFCOMM channel the real wheel advertises on.
// m25_spp.py hard-codes channel 6; our SPP server will target this channel via
// the ESP-IDF SDP registration.  If BluetoothSerial assigns a different channel
// at runtime, check Serial Monitor for the actual channel and update m25_spp.py.
#define RFCOMM_CHANNEL 6

// =============================================================================
// ENCRYPTION KEY
// Replace with your actual M25_LEFT_KEY / M25_RIGHT_KEY from .env.
// Convert the 32-char hex string to 0x-prefixed bytes:
//   "A1B2C3D4..." -> 0xA1, 0xB2, 0xC3, 0xD4, ...
// =============================================================================
#ifndef ENCRYPTION_KEY
#define ENCRYPTION_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
#endif

// =============================================================================
// PIN ASSIGNMENTS
// Override here to match your specific ESP32 board wiring.
// All pins below are the defaults for the original reference wiring.
// =============================================================================

// Status LEDs
#define PIN_LED_RED     25   // Low battery
#define PIN_LED_YELLOW  26   // Medium battery
#define PIN_LED_GREEN   27   // High battery
#define PIN_LED_WHITE   32   // Connection status
#define PIN_LED_BLUE    14   // Speed indicator

// Audio
#define PIN_BUZZER_ACTIVE   22   // Active buzzer (on/off)
#define PIN_BUZZER_PASSIVE  23   // Passive buzzer (PWM / tone capable)

// Control
#define PIN_BUTTON  33   // Force-advertising button (active LOW with pullup)

// =============================================================================
// TIMING CONSTANTS
// =============================================================================

// Speed command watchdog: auto-stop if no REMOTE_SPEED received within this period
#define CMD_TIMEOUT_MS          500

// Battery simulation intervals (milliseconds)
#define BATTERY_DRAIN_ACTIVE_MS 15000   // Drain faster while moving
#define BATTERY_DRAIN_IDLE_MS   30000   // Drain slower while idle

// Button debounce period
#define BUTTON_DEBOUNCE_MS  50

// Stale-packet timeout after connection: discard bad packets for up to this long
#define STALE_TIMEOUT_MS    15000

// BLE-specific UUIDs (only used when TRANSPORT_BLE_ENABLED)
#if defined(WHEEL_SIDE_LEFT)
    #define BLE_SERVICE_UUID  "00001101-0000-1000-8000-00805F9B34FB"
    #define BLE_CHAR_UUID_TX  "00001101-0000-1000-8000-00805F9B34FB"
    #define BLE_CHAR_UUID_RX  "00001102-0000-1000-8000-00805F9B34FB"
#else
    #define BLE_SERVICE_UUID  "00001101-0000-1000-8000-00805F9B34FB"
    #define BLE_CHAR_UUID_TX  "00001103-0000-1000-8000-00805F9B34FB"
    #define BLE_CHAR_UUID_RX  "00001104-0000-1000-8000-00805F9B34FB"
#endif

// =============================================================================
// FIRMWARE / HARDWARE VERSION
// Returned in VERSION_MGMT responses.
// =============================================================================
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define HW_VERSION        1

// =============================================================================
// CRUISE CONTROL DEFAULTS
// cruiseSpeed: raw speed units (same scale as REMOTE_SPEED).
// autoShutoffMin: idle-shutoff timeout in minutes.
// =============================================================================
#define CRUISE_SPEED_DEFAULT      100
#define AUTO_SHUTOFF_MIN_DEFAULT   10

// =============================================================================
// PROTOCOL CONSTANTS (do not change unless the wheel firmware changes)
// =============================================================================
#define M25_HEADER_MARKER   0xEF
#define M25_HEADER_SIZE     3
#define M25_CRC_SIZE        2

#endif // CONFIG_H
