/*
 * Device-specific configuration
 * 
 * IMPORTANT: Each M25 wheel (LEFT and RIGHT) has its own:
 *   - Bluetooth MAC address
 *   - AES-128 encryption key (derived from QR code)
 * 
 * Configure this file based on which wheel you want to simulate:
 * 
 * FOR LEFT WHEEL:
 *   - Set DEVICE_NAME to "M25_FAKE_LEFT"
 *   - Set ENCRYPTION_KEY to your M25_LEFT_KEY (from .env)
 * 
 * FOR RIGHT WHEEL:
 *   - Set DEVICE_NAME to "M25_FAKE_RIGHT"
 *   - Set ENCRYPTION_KEY to your M25_RIGHT_KEY (from .env)
 * 
 * HOW TO GET THE KEY:
 *   1. Find M25_LEFT_KEY or M25_RIGHT_KEY in your .env file
 *   2. It's a 32-character hex string (e.g., "A1B2C3D4E5F6...")
 *   3. Convert each pair of hex digits to 0x format below
 *   
 *   Example: If your key is "A1B2C3D4E5F6G7H8I9J0K1L2M3N4O5P6"
 *   Convert to: 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, ...
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// =================================================================
// WHEEL SELECTION: Choose which wheel to simulate
// =================================================================

// Change to "M25_FAKE_RIGHT" for right wheel simulation
#define DEVICE_NAME "M25_FAKE_LEFT"

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
