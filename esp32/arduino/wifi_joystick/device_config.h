/*
 * Device configuration for WiFi Joystick Controller
 * 
 * Configure encryption key and wheel MAC addresses here
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// Encryption key (16 bytes for AES-128)
// Replace with your actual key derived from QR code
// Example: Use m25_qr_to_key.py to generate from QR code
#ifndef ENCRYPTION_KEY
#define ENCRYPTION_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
#endif

// M25 Wheel MAC Addresses
// Find using BLE scanner or m5squared Python scripts
// Format: "AA:BB:CC:DD:EE:FF"
#define LEFT_WHEEL_MAC  "00:00:00:00:00:00"   // Replace with left wheel MAC
#define RIGHT_WHEEL_MAC "00:00:00:00:00:00"   // Replace with right wheel MAC

// Which wheel to connect to (uncomment ONE)
#define CONNECT_LEFT_WHEEL
// #define CONNECT_RIGHT_WHEEL
// #define CONNECT_BOTH_WHEELS  // Not yet implemented

// BLE Configuration
#define BLE_SCAN_TIME 5  // Scan time in seconds
#define BLE_RECONNECT_DELAY 5000  // Delay before reconnect attempt (ms)

// WiFi Configuration
#define WIFI_SSID "M25-Controller"
#define WIFI_PASSWORD "m25wheel"  // Min 8 characters

#endif // DEVICE_CONFIG_H
