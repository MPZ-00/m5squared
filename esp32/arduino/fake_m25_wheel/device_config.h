/*
 * Device-specific configuration for the fake M25 wheel simulator.
 *
 * IMPORTANT: Each M25 wheel (LEFT and RIGHT) has its own:
 *   - Bluetooth MAC address (set by the physical ESP32, no config needed)
 *   - AES-128 encryption key (derived from QR code)
 *   - BLE characteristic UUIDs (derived from WHEEL_SIDE_LEFT / WHEEL_SIDE_RIGHT below)
 *
 * Steps to configure:
 *   1. Uncomment the correct wheel side in the WHEEL SELECTION section below.
 *      DEVICE_NAME and characteristic UUIDs are set automatically.
 *   2. Set ENCRYPTION_KEY to your M25_LEFT_KEY or M25_RIGHT_KEY from .env.
 *
 * HOW TO GET THE KEY:
 *   1. Find M25_LEFT_KEY or M25_RIGHT_KEY in your .env file
 *   2. It's a 32-character hex string (e.g., "A1B2C3D4E5F60102...")
 *   3. Convert each pair of hex digits to 0x format:
 *      "A1B2C3D4..." -> 0xA1, 0xB2, 0xC3, 0xD4, ...
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// =================================================================
// WHEEL SELECTION: Uncomment exactly ONE of the two lines below.
// This controls the device name AND the BLE characteristic UUIDs,
// so the remote can tell the two fake wheels apart without relying
// solely on MAC address.
// =================================================================
#define WHEEL_SIDE_LEFT
// #define WHEEL_SIDE_RIGHT

#if defined(WHEEL_SIDE_LEFT)
  #define DEVICE_NAME   "M25_FAKE_LEFT"
  // Left wheel uses the same char UUIDs as real M25 wheels (0x1101 / 0x1102).
  #define CHAR_UUID_TX  "00001101-0000-1000-8000-00805F9B34FB"
  #define CHAR_UUID_RX  "00001102-0000-1000-8000-00805F9B34FB"
#elif defined(WHEEL_SIDE_RIGHT)
  #define DEVICE_NAME   "M25_FAKE_RIGHT"
  // Right wheel uses distinct char UUIDs (0x1103 / 0x1104) to differentiate
  // from the left wheel.  Keep SERVICE_UUID the same (0x1101) on both sides.
  #define CHAR_UUID_TX  "00001103-0000-1000-8000-00805F9B34FB"
  #define CHAR_UUID_RX  "00001104-0000-1000-8000-00805F9B34FB"
#else
  #error "Define either WHEEL_SIDE_LEFT or WHEEL_SIDE_RIGHT in device_config.h"
#endif

// =================================================================
// ENCRYPTION KEY: Must match the wheel you're simulating
// =================================================================

// OPTION 1: Default test key (zeros - for testing only)
// OPTION 2: Replace with your actual M25_LEFT_KEY or M25_RIGHT_KEY

#ifndef ENCRYPTION_KEY
#define ENCRYPTION_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
#endif

#endif // DEVICE_CONFIG_H
