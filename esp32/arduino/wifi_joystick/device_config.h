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
#define LEFT_WHEEL_MAC  "28:05:A5:6F:76:26"   // Replace with left wheel MAC, (testing device)
#define RIGHT_WHEEL_MAC "28:05:A5:70:4B:42"   // Replace with right wheel MAC, (testing device)

// Which wheel to connect to (uncomment ONE)
// #define CONNECT_LEFT_WHEEL
#define CONNECT_RIGHT_WHEEL
// #define CONNECT_BOTH_WHEELS  // Not yet implemented

// BLE Configuration
#define BLE_SCAN_TIME 5  // Scan time in seconds
#define BLE_RECONNECT_DELAY 5000  // Delay before reconnect attempt (ms)

// WiFi Configuration
#define WIFI_SSID "M25-Controller"
#define WIFI_PASSWORD "m25wheel"  // Min 8 characters

// ============== Physical Joystick Pins ==============
// ESP32 ADC pins for analog joystick (requires voltage divider for 5V input)
// Use 10k + 10k resistor divider to step down 5V to ~2.5V
//
// Joystick Type: Outdoor analog joystick (no button)
// 6 cables: VCC, GND, VRx, VRy, + 2 extras (TBD - test with multimeter)
//
// Wiring:
// VCC (5V)  -> 4×AA battery pack (6V) or 3×AA (4.5V)
// GND       -> ESP32 GND (common ground with battery)
// VRx       -> GPIO 32 (ADC1_CH4) via voltage divider
// VRy       -> GPIO 33 (ADC1_CH5) via voltage divider
// Extra 1   -> GPIO 34 (ADC1_CH6) via voltage divider (if analog axis)
// Extra 2   -> GPIO 35 (ADC1_CH7) via voltage divider (if analog axis)

#define JOYSTICK_VRX_PIN 32      // X-axis analog input (ADC1_CH4)
#define JOYSTICK_VRY_PIN 33      // Y-axis analog input (ADC1_CH5)
#define JOYSTICK_SW_PIN 34       // Extra axis 1 (GPIO34 input-only, ADC1_CH6)
#define JOYSTICK_EXTRA_PIN 35    // Extra axis 2 (ADC1_CH7)

// ADC Configuration
#define ADC_RESOLUTION 12        // 12-bit resolution (0-4095)
#define ADC_ATTENUATION 3        // 11dB attenuation for 0-3.3V range
#define ADC_SAMPLES 10           // Number of samples for averaging

#endif // DEVICE_CONFIG_H
