/*
 * Fake M25 Wheel Simulator
 * 
 * ESP32 Arduino sketch that simulates an Alber e-motion M25 wheel for testing.
 * Acts as a BLE peripheral that can be discovered and connected to by m5squared.
 * 
 * Hardware: ESP32-WROOM-32 or similar
 * 
 * IMPORTANT: Each M25 wheel has unique encryption keys!
 *   - Configure device_config.h with the correct DEVICE_NAME and ENCRYPTION_KEY
 *   - For LEFT wheel: Use M25_LEFT_KEY from .env
 *   - For RIGHT wheel: Use M25_RIGHT_KEY from .env
 *   - See README.md for detailed setup instructions
 * 
 * Features:
 * - BLE advertising as M25 wheel
 * - Full M25 protocol support (CBC encryption, CRC-16, byte stuffing)
 * - Responds to commands with realistic data
 * - Handles encrypted packets with proper frame structure
 * - Serial monitor shows all activity
 * 
 * Usage:
 * 1. Configure device_config.h for LEFT or RIGHT wheel
 * 2. Upload to ESP32
 * 3. Open Serial Monitor at 115200 baud
 * 4. Use m5squared Python code to connect
 * 5. Type 'help' for available commands
 * 6. Type 'hardware' to see pin configuration
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <mbedtls/aes.h>
#include <freertos/queue.h>

// Device-specific configuration
#include "device_config.h"

// Module headers
#include "m25_protocol.h"
#include "wheel_state.h"
#include "buzzer_control.h"
#include "led_control.h"
#include "command_handler.h"

// Encryption key from config
const uint8_t encryptionKey[16] = ENCRYPTION_KEY;

// BLE Configuration
#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"  // SPP UUID
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

// Button configuration
#define BUTTON_PIN 33       // Force advertising button
#define BUTTON_DEBOUNCE 50  // Debounce delay in ms

// Grace period settings
#define CONNECTION_GRACE_PERIOD_MS 3000   // Extended to 3 seconds for safety
#define STALE_DATA_TIMEOUT_MS 15000       // If still getting bad data after 15s, give up

// BLE Globals
BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
BLECharacteristic* pRxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long connectionTime = 0;  // Time when client connected (for grace period)
bool firstValidPacketReceived = false;  // Track if we've seen a valid packet after connection
uint16_t stalePacketCount = 0;  // Count of stale packets discarded after connection

// RX packet queue: onWrite() posts here; loop() processes outside BLE callback context.
// Calling notify() from within onWrite() causes Bluedroid internal errors (rc=-1 on client).
struct RxPacket { uint8_t data[128]; size_t len; };
static QueueHandle_t _rxQueue = nullptr;

// Wheel state
WheelState wheel;

// Button state
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Forward declarations
void handleCommand(uint8_t* data, size_t len);
void sendResponse();
void handleButton();
bool cryptPacket(uint8_t* data, size_t len, uint8_t* output, bool encrypt);
void updateSpeedIndicators();
size_t addDelimiters(const uint8_t* in, size_t inLen, uint8_t* out);

// Server callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        connectionTime = millis();  // Track connection time for grace period
        firstValidPacketReceived = false;  // Reset valid packet flag
        stalePacketCount = 0;  // Reset stale packet counter
        // Clear RX characteristic buffer to prevent stale data
        if (pRxCharacteristic) {
            pRxCharacteristic->setValue("");
        }
        Serial.println("Client connected!");
        Serial.println("INFO: Ignoring invalid packets until first valid packet received...");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        connectionTime = 0;
        firstValidPacketReceived = false;  // Reset on disconnect
        stalePacketCount = 0;  // Reset counter
        // Clear RX characteristic buffer to prevent stale data on reconnect
        if (pRxCharacteristic) {
            pRxCharacteristic->setValue("");
        }
        Serial.println("Client disconnected!");
    }
};

// Characteristic callbacks - handle received data
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        uint8_t* rxValue = pCharacteristic->getData();
        size_t len = pCharacteristic->getValue().length();

        if (len == 0 || len > 128) return;

        // Do NOT process here or call notify() ─ we are on the Bluedroid BT callback
        // thread. Calling notify() from within onWrite() causes GATT errors (rc=-1)
        // on the client side. Post to queue; loop() will process safely.
        RxPacket pkt;
        memcpy(pkt.data, rxValue, len);
        pkt.len = len;
        if (_rxQueue) xQueueSend(_rxQueue, &pkt, 0);  // non-blocking
    }
};

void handleCommand(uint8_t* data, size_t len) {
    if (debugFlags & DBG_PROTOCOL) {
        Serial.println("\n=== Incoming Packet ===");
        Serial.printf("Raw Length: %d bytes\n", len);
    }
    
    if (debugFlags & DBG_RAW_DATA) {
        Serial.print("Raw data: ");
        for (size_t i = 0; i < len; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
    }
    
    // Step 1: Remove byte stuffing
    uint8_t unstuffed[128];
    size_t unstuffedLen = removeDelimiters(data, len, unstuffed);
    if (debugFlags & DBG_PROTOCOL) {
        Serial.printf("After unstuffing: %d bytes\n", unstuffedLen);
    }
    
    // Step 2: Validate frame structure
    if (unstuffedLen < M25_HEADER_SIZE + M25_CRC_SIZE) {
        Serial.println("ERROR: Frame too short");
        Serial.println("======================\n");
        return;
    }
    
    // Check header marker
    if (unstuffed[0] != M25_HEADER_MARKER) {
        Serial.printf("ERROR: Invalid header marker (expected 0xEF, got 0x%02X)\n", unstuffed[0]);
        return;
    }
    
    // Extract frame length from header
    uint16_t frameLength = ((uint16_t)unstuffed[1] << 8) | unstuffed[2];
    if (debugFlags & DBG_PROTOCOL) {
        Serial.printf("Frame length field: %d\n", frameLength);
    }
    
    // Verify CRC (over everything except final 2 CRC bytes)
    size_t crcDataLen = unstuffedLen - M25_CRC_SIZE;
    uint16_t calculatedCRC = calculateCRC16(unstuffed, crcDataLen);
    uint16_t receivedCRC = ((uint16_t)unstuffed[crcDataLen] << 8) | unstuffed[crcDataLen + 1];
    
    if (debugFlags & DBG_CRC) {
        Serial.printf("CRC: received=0x%04X, calculated=0x%04X\n", receivedCRC, calculatedCRC);
    }
    if (calculatedCRC != receivedCRC) {
        // Ignore all invalid packets until we see first valid one after connection
        if (!firstValidPacketReceived) {
            stalePacketCount++;
            unsigned long elapsed = millis() - connectionTime;
            
            // Timeout: if we've been getting bad data for too long, give up
            if (elapsed > STALE_DATA_TIMEOUT_MS) {
                Serial.printf("ERROR: Timeout - received %d invalid packets over %ld seconds\n", stalePacketCount, elapsed/1000);
                Serial.println("ERROR: Remote appears to be sending corrupt data continuously");
                Serial.println("ERROR: Disconnecting to force reconnection...");
                // Force disconnect to trigger reconnection
                if (pServer) {
                    pServer->disconnect(pServer->getConnId());
                }
                return;
            }
            
            // Only log every 10th packet to reduce spam
            if (stalePacketCount % 10 == 1) {
                if (elapsed < CONNECTION_GRACE_PERIOD_MS) {
                    Serial.printf("INFO: Discarding stale packets (CRC mismatch) - %d packets, %ldms after connect\n", stalePacketCount, elapsed);
                } else {
                    Serial.printf("INFO: Still draining stale buffer (CRC mismatch) - %d packets, %ldms after connect\n", stalePacketCount, elapsed);
                }
            }
            return;  // Discard until first valid packet
        }
        Serial.println("WARNING: CRC mismatch");
        // Don't return - continue for testing purposes
    } else if (debugFlags & DBG_CRC) {
        Serial.println("CRC: VALID");
    }
    
    // Mark that we've received our first valid packet after connection
    if (!firstValidPacketReceived && calculatedCRC == receivedCRC) {
        firstValidPacketReceived = true;
        unsigned long elapsed = millis() - connectionTime;
        if (stalePacketCount > 0) {
            Serial.printf("INFO: First valid packet received after discarding %d stale packets (%ldms after connect) - connection stable\n", stalePacketCount, elapsed);
        } else {
            Serial.printf("INFO: First valid packet received %ldms after connect - connection stable\n", elapsed);
        }
    }
    
    // Step 3: Extract encrypted payload (between header and CRC)
    size_t payloadLen = crcDataLen - M25_HEADER_SIZE;
    if (debugFlags & DBG_CRYPTO) {
        Serial.printf("Encrypted payload length: %d bytes\n", payloadLen);
    }
    
    if (payloadLen < 16 || payloadLen % 16 != 0) {
        // Ignore all invalid packets until we see first valid one after connection
        if (!firstValidPacketReceived) {
            stalePacketCount++;
            unsigned long elapsed = millis() - connectionTime;
            
            // Timeout: if we've been getting bad data for too long, give up
            if (elapsed > STALE_DATA_TIMEOUT_MS) {
                Serial.printf("ERROR: Timeout - received %d invalid packets over %ld seconds\n", stalePacketCount, elapsed/1000);
                Serial.println("ERROR: Remote appears to be sending corrupt data continuously");
                Serial.println("ERROR: Disconnecting to force reconnection...");
                // Force disconnect to trigger reconnection
                if (pServer) {
                    pServer->disconnect(pServer->getConnId());
                }
                return;
            }
            
            // Only log every 10th packet to reduce spam
            if (stalePacketCount % 10 == 1) {
                if (elapsed < CONNECTION_GRACE_PERIOD_MS) {
                    Serial.printf("INFO: Discarding stale packets (AES alignment) - %d packets, %ldms after connect\n", stalePacketCount, elapsed);
                } else {
                    Serial.printf("INFO: Still draining stale buffer (AES alignment) - %d packets, %ldms after connect\n", stalePacketCount, elapsed);
                }
            }
            return;  // Discard until first valid packet
        }
        Serial.println("ERROR: Payload not AES-block aligned");
        return;
    }
    
    uint8_t* encryptedPayload = unstuffed + M25_HEADER_SIZE;
    
    // Step 4: Decrypt using CBC (IV encrypted via ECB, then CBC decrypt)
    // Payload format: [IV_encrypted(16)][data_encrypted(16 or 32)]
    if (payloadLen < 32) {
        Serial.println("ERROR: Payload too short (need at least IV + one data block)");
        return;
    }
    
    // Extract encrypted IV (first 16 bytes)
    uint8_t ivEncrypted[16];
    memcpy(ivEncrypted, encryptedPayload, 16);
    
    // Decrypt IV using ECB
    uint8_t iv[16];
    mbedtls_aes_context aesCtx;
    mbedtls_aes_init(&aesCtx);
    mbedtls_aes_setkey_dec(&aesCtx, encryptionKey, 128);
    if (mbedtls_aes_crypt_ecb(&aesCtx, MBEDTLS_AES_DECRYPT, ivEncrypted, iv) != 0) {
        Serial.println("ERROR: IV decryption failed");
        mbedtls_aes_free(&aesCtx);
        return;
    }
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.print("Decrypted IV: ");
        for (int i = 0; i < 16; i++) {
            Serial.printf("%02X ", iv[i]);
        }
        Serial.println();
    }
    
    // Decrypt data using CBC
    size_t dataLen = payloadLen - 16;
    uint8_t* encryptedData = encryptedPayload + 16;
    uint8_t decryptedData[32];
    
    uint8_t ivCopy[16];
    memcpy(ivCopy, iv, 16);  // CBC modifies IV in place
    
    if (mbedtls_aes_crypt_cbc(&aesCtx, MBEDTLS_AES_DECRYPT, dataLen, ivCopy, encryptedData, decryptedData) != 0) {
        Serial.println("ERROR: CBC decryption failed");
        mbedtls_aes_free(&aesCtx);
        return;
    }
    mbedtls_aes_free(&aesCtx);
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.println("Decryption: SUCCESS");
    }
    
    // Step 5: Remove PKCS7 padding
    if (dataLen == 0) {
        Serial.println("ERROR: No data to unpad");
        return;
    }
    
    uint8_t padLen = decryptedData[dataLen - 1];
    if (padLen == 0 || padLen > 16 || padLen > dataLen) {
        if (debugFlags & DBG_CRYPTO) {
            Serial.printf("WARNING: Invalid PKCS7 padding (%d), continuing anyway\n", padLen);
        }
        padLen = 0;  // Assume no padding
    }
    size_t sppLen = dataLen - padLen;
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.printf("PKCS7 padding: %d bytes, SPP length: %d bytes\n", padLen, sppLen);
    }
    
    // Print decrypted SPP packet
    if (debugFlags & DBG_RAW_DATA) {
        Serial.print("Decrypted SPP: ");
        for (size_t i = 0; i < sppLen; i++) {
            Serial.printf("%02X ", decryptedData[i]);
        }
        Serial.println();
    }
    
    // Parse SPP packet structure
    if (sppLen >= 6) {
        if (debugFlags & DBG_PROTOCOL) {
            Serial.println("\nSPP Packet Structure:");
            Serial.printf("  Protocol ID:  0x%02X\n", decryptedData[0]);
            Serial.printf("  Telegram ID:  0x%02X\n", decryptedData[1]);
            Serial.printf("  Source:       0x%02X\n", decryptedData[2]);
            Serial.printf("  Destination:  0x%02X\n", decryptedData[3]);
            Serial.printf("  Service ID:   0x%02X\n", decryptedData[4]);
            Serial.printf("  Parameter ID: 0x%02X\n", decryptedData[5]);
        }
        
        if (sppLen > 6 && (debugFlags & DBG_PROTOCOL)) {
            Serial.print("  Payload:      ");
            for (size_t i = 6; i < sppLen; i++) {
                Serial.printf("%02X ", decryptedData[i]);
            }
            Serial.println();
        }
        
        // Extract command identifiers
        uint8_t serviceId = decryptedData[4];
        uint8_t paramId = decryptedData[5];
        
        // Process commands and update state (ALWAYS, regardless of debug flags)
        if (serviceId == 0x01 && sppLen > 6) {  // APP_MGMT service
            if (paramId == 0x10) {  // SYSTEM_MODE
                // No state to update for SYSTEM_MODE
            } else if (paramId == 0x20) {  // DRIVE_MODE
                uint8_t mode = decryptedData[6];
                wheel.hillHold = (mode & 0x01) != 0;
                playBeep(2);
            } else if (paramId == 0x30 && sppLen >= 8) {  // REMOTE_SPEED
                int16_t speed = ((int16_t)decryptedData[6] << 8) | decryptedData[7];
                // Update current speed and check for direction change
                bool directionChanged = (wheel.lastSpeed > 0 && speed < 0) || (wheel.lastSpeed < 0 && speed > 0);
                if (directionChanged && abs(speed) > 5 && audioFeedbackEnabled) {
                    playBeep(1);  // Beep on direction change
                }
                wheel.lastSpeed = wheel.currentSpeed;
                wheel.currentSpeed = speed;
                
                // Simulate wheel rotation based on speed (rough estimate)
                // Speed scale: 250 raw units = 100% = ~10 km/h
                // Update every 5 seconds of movement at current speed
                unsigned long now = millis();
                if (abs(speed) > 5 && (now - wheel.lastSpeedUpdate) > 5000) {
                    // Rotation estimate: higher speed = more rotations
                    int rotations = abs(speed) / 50;  // ~5 rotations at max speed
                    if (rotations > 0) {
                        wheel.simulateRotation(rotations);
                    }
                    wheel.lastSpeedUpdate = now;
                }
            } else if (paramId == 0x40) {  // ASSIST_LEVEL
                uint8_t level = decryptedData[6];
                if (level < 3) wheel.assistLevel = level;
                playBeep(level + 1);  // Beep based on assist level
            }
            
            // Send ACK for all commands except REMOTE_SPEED (0x30), too many
            if (paramId != 0x30) {
                sendResponse();
            }
        }
        
        // Debug logging (only when debug flags enabled)
        if (debugFlags & DBG_COMMANDS) {
            if (serviceId == 0x01) {  // APP_MGMT
                if (paramId == 0x10) {
                    Serial.print("[CMD] SYSTEM_MODE");
                    if (sppLen > 6) Serial.printf(" = 0x%02X\n", decryptedData[6]);
                    else Serial.println();
                } else if (paramId == 0x20) {
                    Serial.print("[CMD] DRIVE_MODE");
                    if (sppLen > 6) {
                        uint8_t mode = decryptedData[6];
                        Serial.printf(" = 0x%02X [", mode);
                        if (mode & 0x01) Serial.print("HILL_HOLD ");
                        if (mode & 0x02) Serial.print("CRUISE ");
                        if (mode & 0x04) Serial.print("REMOTE");
                        Serial.println("]");
                    } else Serial.println();
                } else if (paramId == 0x30) {
                    Serial.print("[CMD] REMOTE_SPEED");
                    if (sppLen >= 8) {
                        int16_t speed = ((int16_t)decryptedData[6] << 8) | decryptedData[7];
                        float percent = speed / 2.5;
                        Serial.printf(" = %d raw (%.1f%%)\n", speed, percent);
                    } else Serial.println();
                } else if (paramId == 0x40) {
                    Serial.print("[CMD] ASSIST_LEVEL");
                    if (sppLen > 6) {
                        uint8_t level = decryptedData[6];
                        const char* names[] = {"INDOOR", "OUTDOOR", "LEARNING"};
                        Serial.printf(" = %d (%s)\n", level, level < 3 ? names[level] : "UNKNOWN");
                    } else Serial.println();
                } else {
                    Serial.printf("[CMD] Service=0x%02X, Param=0x%02X\n", serviceId, paramId);
                }
            } else {
                Serial.printf("[CMD] Unknown service 0x%02X, param 0x%02X\n", serviceId, paramId);
            }
        }
    }
}

void sendResponse() {
    if (!deviceConnected) return;
    
    // Step 1: Build SPP ACK packet (9 bytes)
    uint8_t spp[9] = {
        0x23,                    // Protocol ID (matching received)
        0x01,                    // Telegram ID
        0x10,                    // Source: Wheel
        0x01,                    // Dest: Remote
        0x01,                    // Service: APP_MGMT
        0xFF,                    // Param: ACK
        wheel.batteryLevel,      // Battery level
        wheel.assistLevel,       // Assist level
        wheel.driveProfile       // Drive profile
    };
    size_t sppLen = 9;
    
    if (debugFlags & DBG_PROTOCOL) {
        Serial.print("[ACK] SPP packet: ");
        for (size_t i = 0; i < sppLen; i++) {
            Serial.printf("%02X ", spp[i]);
        }
        Serial.println();
    }
    
    // Step 2: Apply PKCS7 padding to make it 16 bytes
    uint8_t paddedSpp[16];
    memcpy(paddedSpp, spp, sppLen);
    uint8_t padLen = 16 - sppLen;
    for (size_t i = sppLen; i < 16; i++) {
        paddedSpp[i] = padLen;
    }
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.printf("[ACK] After PKCS7 padding (%d bytes): ", padLen);
        for (int i = 0; i < 16; i++) {
            Serial.printf("%02X ", paddedSpp[i]);
        }
        Serial.println();
    }
    
    // Step 3: Generate random IV
    uint8_t iv[16];
    for (int i = 0; i < 16; i++) {
        iv[i] = random(0, 256);
    }
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.print("[ACK] Generated IV: ");
        for (int i = 0; i < 16; i++) {
            Serial.printf("%02X ", iv[i]);
        }
        Serial.println();
    }
    
    // Step 4: Encrypt IV using ECB
    uint8_t ivEncrypted[16];
    mbedtls_aes_context aesCtx;
    mbedtls_aes_init(&aesCtx);
    mbedtls_aes_setkey_enc(&aesCtx, encryptionKey, 128);
    
    if (mbedtls_aes_crypt_ecb(&aesCtx, MBEDTLS_AES_ENCRYPT, iv, ivEncrypted) != 0) {
        Serial.println("[ACK] ERROR: IV encryption failed");
        mbedtls_aes_free(&aesCtx);
        return;
    }
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.print("[ACK] Encrypted IV: ");
        for (int i = 0; i < 16; i++) {
            Serial.printf("%02X ", ivEncrypted[i]);
        }
        Serial.println();
    }
    
    // Step 5: Encrypt padded SPP data using CBC
    uint8_t encryptedData[16];
    uint8_t ivCopy[16];
    memcpy(ivCopy, iv, 16);  // CBC modifies IV in place
    
    if (mbedtls_aes_crypt_cbc(&aesCtx, MBEDTLS_AES_ENCRYPT, 16, ivCopy, paddedSpp, encryptedData) != 0) {
        Serial.println("[ACK] ERROR: CBC encryption failed");
        mbedtls_aes_free(&aesCtx);
        return;
    }
    mbedtls_aes_free(&aesCtx);
    
    if (debugFlags & DBG_CRYPTO) {
        Serial.print("[ACK] Encrypted data: ");
        for (int i = 0; i < 16; i++) {
            Serial.printf("%02X ", encryptedData[i]);
        }
        Serial.println();
    }
    
    // Step 6: Build M25 frame: [header][encrypted_IV][encrypted_data]
    size_t payloadLen = 32;  // 16 (IV) + 16 (data)
    uint8_t frame[64];
    
    // Calculate frame length (matches remote's format: header + payload + CRC - 1)
    uint16_t frameLength = (uint16_t)(M25_HEADER_SIZE + payloadLen + M25_CRC_SIZE - 1);
    
    // Header
    frame[0] = M25_HEADER_MARKER;  // 0xEF
    frame[1] = (frameLength >> 8) & 0xFF;  // Length high byte
    frame[2] = frameLength & 0xFF;          // Length low byte
    
    // Encrypted payload
    memcpy(frame + M25_HEADER_SIZE, ivEncrypted, 16);
    memcpy(frame + M25_HEADER_SIZE + 16, encryptedData, 16);
    size_t frameLen = M25_HEADER_SIZE + payloadLen;  // Actual frame size before CRC
    
    // Step 7: Calculate CRC16 over the frame
    uint16_t crc = calculateCRC16(frame, frameLen);
    
    if (debugFlags & DBG_CRC) {
        Serial.printf("[ACK] Calculated CRC: 0x%04X\n", crc);
    }
    
    // Step 8: Append CRC
    frame[frameLen] = (crc >> 8) & 0xFF;  // CRC high byte
    frame[frameLen + 1] = crc & 0xFF;     // CRC low byte
    frameLen += M25_CRC_SIZE;
    
    if (debugFlags & DBG_PROTOCOL) {
        Serial.printf("[ACK] Frame before stuffing (%d bytes): ", frameLen);
        for (size_t i = 0; i < frameLen; i++) {
            Serial.printf("%02X ", frame[i]);
        }
        Serial.println();
    }
    
    // Step 9: Apply byte stuffing
    uint8_t stuffed[128];
    size_t stuffedLen = addDelimiters(frame, frameLen, stuffed);
    
    if (debugFlags & DBG_PROTOCOL) {
        Serial.printf("[ACK] Frame after stuffing (%d bytes): ", stuffedLen);
        for (size_t i = 0; i < stuffedLen; i++) {
            Serial.printf("%02X ", stuffed[i]);
        }
        Serial.println();
    }
    
    // Step 10: Send via BLE
    pTxCharacteristic->setValue(stuffed, stuffedLen);
    pTxCharacteristic->notify();
    
    if (debugFlags & DBG_PROTOCOL) {
        Serial.printf("[ACK] Response sent (%d bytes)\n", stuffedLen);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Increased delay for serial connection stability
    
    // Initialize hardware
    initLEDs();
    pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button with internal pullup
    initBuzzers();
    
    // Test buzzers and LEDs at startup
    testBuzzers();
    showBatteryLevel(wheel.batteryLevel);
    
    Serial.println("\n=================================");
    Serial.println("Fake M25 Wheel Simulator");
    Serial.println("=================================");
    Serial.println();
    Serial.println("Device name: " + String(DEVICE_NAME));
    Serial.println();

    // Initialize RX packet queue before BLE setup
    _rxQueue = xQueueCreate(4, sizeof(RxPacket));

    // Initialize BLE
    BLEDevice::init(DEVICE_NAME);
    
    // Display MAC address
    Serial.print("MAC Address: ");
    Serial.println(BLEDevice::getAddress().toString().c_str());
    Serial.println();
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create TX Characteristic (wheel -> client)
    pTxCharacteristic = pService->createCharacteristic(
        CHAR_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    // Create RX Characteristic (client -> wheel)
    pRxCharacteristic = pService->createCharacteristic(
        CHAR_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Start the service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // iPhone connection issue fix
    pAdvertising->setMinPreferred(0x12);
    
    BLEDevice::startAdvertising();
    
    Serial.println("BLE device ready!");
    Serial.println("Waiting for connection...");
    Serial.println();
    
    Serial.println("Type 'help' for available commands");
    Serial.println("Type 'hardware' to see pin configuration");
    Serial.println();
    
    // Initialize command handler
    initCommandHandler(pServer, pTxCharacteristic, &deviceConnected, encryptionKey);
}

void loop() {
    // Drain the RX packet queue ─ process commands here, not in the BLE callback.
    // This avoids calling notify() from within onWrite(), which causes rc=-1 on client.
    {
        RxPacket pkt;
        while (xQueueReceive(_rxQueue, &pkt, 0) == pdTRUE) {
            if (debugFlags & DBG_RAW_DATA) {
                Serial.print("Received packet (" + String(pkt.len) + " bytes): ");
                for (size_t i = 0; i < pkt.len; i++) Serial.printf("%02X ", pkt.data[i]);
                Serial.println();
            }
            handleCommand(pkt.data, pkt.len);
        }
    }

    // Handle connection state changes
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
        Serial.println("=== CONNECTED ===");
        Serial.println("Ready to receive commands");
        Serial.println();
        setLEDState(LED_CONNECTED);
        playBeep(2);  // Two beeps on connect
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("=== DISCONNECTED ===");
        Serial.println("Advertising restarted");
        Serial.println();
        oldDeviceConnected = deviceConnected;
        setLEDState(LED_ADVERTISING);
        playBeep(1);  // One beep on disconnect
        wheel.currentSpeed = 0;  // Reset speed on disconnect
    }
    
    // Update LED states
    updateLEDs();
    
    // Handle button press
    handleButton();
    
    // Handle serial commands
    handleSerialCommand(wheel);
    
    // Update speed indicators (LED and buzzer)
    updateSpeedIndicators();
    
    // Simulate battery drain (faster when active)
    static unsigned long lastBatteryUpdate = 0;
    unsigned long batteryInterval = (abs(wheel.currentSpeed) > 5) ? 15000 : 30000;  // 15s when moving, 30s idle
    if (millis() - lastBatteryUpdate > batteryInterval) {
        if (wheel.batteryLevel > 0) {
            int oldLevel = wheel.batteryLevel;
            wheel.updateBattery();
            // Only update LED if battery crosses threshold (no spam)
            int oldThreshold = (oldLevel > 66) ? 2 : (oldLevel > 33) ? 1 : 0;
            int newThreshold = (wheel.batteryLevel > 66) ? 2 : (wheel.batteryLevel > 33) ? 1 : 0;
            if (oldThreshold != newThreshold) {
                showBatteryLevel(wheel.batteryLevel);
            }
        }
        lastBatteryUpdate = millis();
    }
    
    delay(20);  // Reduced delay for better responsiveness
}

void handleButton() {
    int reading = digitalRead(BUTTON_PIN);
    
    // Debounce check - wait for stable state
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
        lastButtonState = reading;
    }
    
    // If button is pressed (LOW) and debounce time has passed
    if (reading == LOW && (millis() - lastDebounceTime) > BUTTON_DEBOUNCE) {
        Serial.println("\n*** BUTTON PRESSED ***");
        
        if (deviceConnected) {
            Serial.println("Disconnecting client...");
            pServer->disconnect(pServer->getConnId());
            delay(100);  // Wait for disconnect to complete
        }
        
        // Force clear RX buffer to prevent stale data on reconnect
        if (pRxCharacteristic) {
            pRxCharacteristic->setValue("");
            Serial.println("Cleared RX buffer");
        }
        
        // Reset connection state
        firstValidPacketReceived = false;
        stalePacketCount = 0;
        connectionTime = 0;
        
        Serial.println("Force advertising...");
        BLEDevice::startAdvertising();
        Serial.println("Broadcasting as: " + String(DEVICE_NAME));
        setLEDState(LED_ADVERTISING);
        
        // Wait for button release
        while (digitalRead(BUTTON_PIN) == LOW) {
            delay(10);
        }
        setLEDState(LED_BATTERY, wheel.batteryLevel);
        Serial.println("Button released\n");
        
        // Reset debounce timer
        lastDebounceTime = millis();
    }
}

bool cryptPacket(uint8_t* data, size_t len, uint8_t* output, bool encrypt) {
    if (len % 16 != 0) {
        Serial.println("Error: Data length must be multiple of 16");
        return false;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Set key based on mode
    int mode = encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT;
    int keyResult = encrypt ? 
        mbedtls_aes_setkey_enc(&aes, encryptionKey, 128) :
        mbedtls_aes_setkey_dec(&aes, encryptionKey, 128);
    
    if (keyResult != 0) {
        Serial.println(encrypt ? "Error: Failed to set encryption key" : "Error: Failed to set decryption key");
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Process each 16-byte block
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, mode, data + i, output + i) != 0) {
            Serial.println(encrypt ? "Error: Encryption failed" : "Error: Decryption failed");
            mbedtls_aes_free(&aes);
            return false;
        }
    }
    
    mbedtls_aes_free(&aes);
    return true;
}

/**
 * Add byte stuffing (matching add_delimiters in m25_protocol.py)
 * First byte kept as-is; every 0xEF after first position is doubled.
 */
size_t addDelimiters(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];  // First byte (0xEF header) always kept as-is
    
    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        // Double any 0xEF bytes after the first position
        if (in[i] == M25_HEADER_MARKER) {
            out[pos++] = M25_HEADER_MARKER;
        }
    }
    return pos;
}

void updateSpeedIndicators() {
    if (!visualFeedbackEnabled && !audioFeedbackEnabled) return;
    
    unsigned long now = millis();
    
    // Calculate speed percentage and direction
    float speedPercent = abs(wheel.currentSpeed) / 2.5;  // M25_SPEED_SCALE = 2.5
    bool moving = abs(wheel.currentSpeed) > 5;  // Deadzone threshold
    
    // Update visual speed indicator (Blue LED)
    if (visualFeedbackEnabled && moving) {
        updateSpeedIndicator(wheel);
    } else if (!moving) {
        digitalWrite(LED_BLUE, LOW);
    }
    
    // Update audio feedback (passive buzzer)
    if (audioFeedbackEnabled && moving) {
        // Frequency range: 200Hz (slow) to 2000Hz (fast)
        uint16_t frequency = (uint16_t)constrain(200 + (speedPercent * 18), 200, 2000);
        playTone(frequency, 0);  // Continuous tone
    } else {
        stopBuzzers();
    }
}