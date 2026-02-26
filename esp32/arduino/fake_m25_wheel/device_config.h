/*
 * Device-specific configuration
 * 
 * Edit DEVICE_NAME and ENCRYPTION_KEY for each device
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// Device identification - Change to "M25_FAKE_RIGHT" for right wheel
#define DEVICE_NAME "M25_FAKE_LEFT"

// Encryption key (16 bytes for AES-128)
// Replace with your actual key derived from QR code
#ifndef ENCRYPTION_KEY
#define ENCRYPTION_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}
#endif

#endif // DEVICE_CONFIG_H
