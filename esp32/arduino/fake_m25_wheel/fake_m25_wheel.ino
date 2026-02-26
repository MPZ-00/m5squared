/*
 * Fake M25 Wheel Simulator
 * 
 * ESP32 Arduino sketch that simulates an Alber e-motion M25 wheel for testing.
 * Acts as a BLE peripheral that can be discovered and connected to by m5squared.
 * 
 * Hardware: ESP32-WROOM-32 or similar
 * 
 * Features:
 * - BLE advertising as M25 wheel
 * - Simulates basic SPP (Serial Port Profile) communication
 * - Responds to commands with realistic data
 * - Handles encrypted packets (displays received data)
 * - Serial monitor shows all activity
 * 
 * Usage:
 * 1. Upload to ESP32
 * 2. Open Serial Monitor at 115200 baud
 * 3. Use m5squared Python code to connect
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <mbedtls/aes.h>

// Device-specific configuration
#include "device_config.h"

// Encryption key from config
const uint8_t encryptionKey[16] = ENCRYPTION_KEY;

// Configuration
#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"  // SPP UUID
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

// M25 Protocol constants (matching m25_ble.h)
#define M25_HEADER_MARKER 0xEF
#define M25_HEADER_SIZE 3
#define M25_CRC_SIZE 2

// CRC-16 lookup table (matching m25_protocol.py)
static const uint16_t _crcTable[256] PROGMEM = {
    0,49345,49537,320,49921,960,640,49729,50689,1728,1920,51009,1280,50625,50305,1088,
    52225,3264,3456,52545,3840,53185,52865,3648,2560,51905,52097,2880,51457,2496,2176,51265,
    55297,6336,6528,55617,6912,56257,55937,6720,7680,57025,57217,8000,56577,7616,7296,56385,
    5120,54465,54657,5440,55041,6080,5760,54849,53761,4800,4992,54081,4352,53697,53377,4160,
    61441,12480,12672,61761,13056,62401,62081,12864,13824,63169,63361,14144,62721,13760,13440,62529,
    15360,64705,64897,15680,65281,16320,16000,65089,64001,15040,15232,64321,14592,63937,63617,14400,
    10240,59585,59777,10560,60161,11200,10880,59969,60929,11968,12160,61249,11520,60865,60545,11328,
    58369,9408,9600,58689,9984,59329,59009,9792,8704,58049,58241,9024,57601,8640,8320,57409,
    40961,24768,24960,41281,25344,41921,41601,25152,26112,42689,42881,26432,42241,26048,25728,42049,
    27648,44225,44417,27968,44801,28608,28288,44609,43521,27328,27520,43841,26880,43457,43137,26688,
    30720,47297,47489,31040,47873,31680,31360,47681,48641,32448,32640,48961,32000,48577,48257,31808,
    46081,29888,30080,46401,30464,47041,46721,30272,29184,45761,45953,29504,45313,29120,28800,45121,
    20480,37057,37249,20800,37633,21440,21120,37441,38401,22208,22400,38721,21760,38337,38017,21568,
    39937,23744,23936,40257,24320,40897,40577,24128,23040,39617,39809,23360,39169,22976,22656,38977,
    34817,18624,18816,35137,19200,35777,35457,19008,19968,36545,36737,20288,36097,19904,19584,35905,
    17408,33985,34177,17728,34561,18368,18048,34369,33281,17088,17280,33601,16640,33217,32897,16448
};

// CRC-16 calculation (matching m25_protocol.py)
static uint16_t calculateCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ pgm_read_word(&_crcTable[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

// Remove byte stuffing (reverse of add_delimiters in m25_protocol.py)
// First byte kept as-is; every doubled 0xEF is reduced to single 0xEF.
static size_t removeDelimiters(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];  // First byte always kept
    
    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        // Skip next byte if current is 0xEF followed by another 0xEF
        if (in[i] == M25_HEADER_MARKER && i + 1 < inLen && in[i + 1] == M25_HEADER_MARKER) {
            i++;  // Skip the duplicate
        }
    }
    return pos;
}

// Hardware pins for visual feedback
#define LED_RED 25          // Low battery indicator
#define LED_YELLOW 26       // Medium battery indicator
#define LED_GREEN 27        // High battery indicator
#define LED_WHITE 32        // Connection status
#define LED_BLUE 14         // Speed/direction indicator
#define BUTTON_PIN 33       // Force advertising button
#define BUTTON_DEBOUNCE 50  // Debounce delay in ms

// Buzzer pins
#define BUZZER_PASSIVE 23   // Passive buzzer (PWM capable pin)
#define BUZZER_ACTIVE 22    // Active buzzer

// LED states
enum LEDState {
    LED_ADVERTISING,    // White blinking fast
    LED_CONNECTING,     // White blinking slow
    LED_CONNECTED,      // White solid
    LED_ERROR,          // Blue blinking slow
    LED_BATTERY         // Show battery level
};

// Globals
BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
BLECharacteristic* pRxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
LEDState currentLEDState = LED_ADVERTISING;
unsigned long connectionTime = 0;  // Time when client connected (for grace period)

// Grace period after connection to ignore stale packets (milliseconds)
#define CONNECTION_GRACE_PERIOD_MS 500

// Debug flags (bitfield)
#define DBG_PROTOCOL    0x01  // Protocol parsing details
#define DBG_CRYPTO      0x02  // Encryption/decryption steps
#define DBG_CRC         0x04  // CRC validation
#define DBG_COMMANDS    0x08  // Command interpretation
#define DBG_RAW_DATA    0x10  // Raw hex dumps
uint8_t debugFlags = DBG_COMMANDS;  // Default: only show decoded commands

// Simulated wheel state
int16_t currentSpeed = 0;     // Current speed (-32768 to +32767 raw units)
int16_t lastSpeed = 0;        // Previous speed (for change detection)
int batteryLevel = 85;        // 85%
int assistLevel = 1;          // 0-2
bool hillHold = false;
int driveProfile = 0;         // 0 = standard
long wheelRotation = 0;       // Total wheel rotations
float distanceTraveled = 0.0; // Distance in meters (approx 2m per rotation)

// Visual/Audio feedback state
unsigned long lastSpeedUpdate = 0;
bool audioFeedbackEnabled = true;
bool visualFeedbackEnabled = true;

// Button state
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

void handleCommand(uint8_t* data, size_t len);
void sendResponse();
void handleSerialCommand();
void printHelp();
void updateLEDs();
void handleButton();
void setLEDState(LEDState state);
void showBatteryLevel();
bool cryptPacket(uint8_t* data, size_t len, uint8_t* output, bool encrypt);
void printKey();
bool validatePacket(uint8_t* data, size_t len);
void printPacket(uint8_t* data, size_t len);
void simulateWheelRotation(int rotations);
void printDebugHelp();
void updateSpeedIndicators();
void playTone(uint16_t frequency, uint16_t duration);
void playBeep(uint8_t count);

// Server callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        connectionTime = millis();  // Track connection time for grace period
        Serial.println("Client connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        connectionTime = 0;
        Serial.println("Client disconnected!");
    }
};

// Characteristic callbacks - handle received data
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        uint8_t* rxValue = pCharacteristic->getData();
        size_t len = pCharacteristic->getValue().length();

        if (len <= 0) return;
        
        if (debugFlags & DBG_RAW_DATA) {
            Serial.print("Received packet (" + String(len) + " bytes): ");
            
            // Print as hex
            for (int i = 0; i < len; i++) {
                Serial.printf("%02X ", rxValue[i]);
            }
            Serial.println();
        }
        
        // Try to decode command (will be encrypted in real use)
        handleCommand(rxValue, len);
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
        // Check if we're in grace period after connection
        bool inGracePeriod = (connectionTime > 0) && ((millis() - connectionTime) < CONNECTION_GRACE_PERIOD_MS);
        if (inGracePeriod) {
            if (debugFlags & DBG_PROTOCOL) {
                Serial.println("INFO: CRC mismatch during grace period (likely stale data from previous session) - ignoring");
            }
            return;  // Silently discard during grace period
        }
        Serial.println("WARNING: CRC mismatch");
        // Don't return - continue for testing purposes
    } else if (debugFlags & DBG_CRC) {
        Serial.println("CRC: VALID");
    }
    
    // Step 3: Extract encrypted payload (between header and CRC)
    size_t payloadLen = crcDataLen - M25_HEADER_SIZE;
    if (debugFlags & DBG_CRYPTO) {
        Serial.printf("Encrypted payload length: %d bytes\n", payloadLen);
    }
    
    if (payloadLen < 16 || payloadLen % 16 != 0) {
        // Check if we're in grace period after connection
        bool inGracePeriod = (connectionTime > 0) && ((millis() - connectionTime) < CONNECTION_GRACE_PERIOD_MS);
        if (inGracePeriod) {
            if (debugFlags & DBG_PROTOCOL) {
                Serial.println("INFO: Payload not AES-block aligned during grace period (likely stale data) - ignoring");
            }
            return;  // Silently discard during grace period
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
            if (paramId == 0x20) {  // DRIVE_MODE
                uint8_t mode = decryptedData[6];
                hillHold = (mode & 0x01) != 0;
            } else if (paramId == 0x30 && sppLen >= 8) {  // REMOTE_SPEED
                int16_t speed = ((int16_t)decryptedData[6] << 8) | decryptedData[7];
                // Update current speed and check for direction change
                bool directionChanged = (lastSpeed > 0 && speed < 0) || (lastSpeed < 0 && speed > 0);
                if (directionChanged && abs(speed) > 5 && audioFeedbackEnabled) {
                    playBeep(1);  // Beep on direction change
                }
                lastSpeed = currentSpeed;
                currentSpeed = speed;
            } else if (paramId == 0x40) {  // ASSIST_LEVEL
                uint8_t level = decryptedData[6];
                if (level < 3) assistLevel = level;
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
    
    // Note: Response sending disabled for now - real wheel doesn't respond to all commands
    // delay(50);
    // sendResponse();
}

void sendResponse() {
    if (!deviceConnected) return;
    
    // Build unencrypted response (16 bytes)
    uint8_t plainResponse[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 
        0x00, 0x00, 0x00, 0x00,
        batteryLevel, assistLevel, driveProfile, 0x00,
        0x11, 0x22, 0x33, 0x44
    };
    
    // Encrypt response
    uint8_t encrypted[16];
    cryptPacket(plainResponse, 16, encrypted, true);
    
    Serial.print("Sending encrypted response: ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02X ", encrypted[i]);
    }
    Serial.println();
    
    pTxCharacteristic->setValue(encrypted, 16);
    pTxCharacteristic->notify();
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Increased delay for serial connection stability
    
    // Initialize LED pins
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_WHITE, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button with internal pullup
    
    // Initialize buzzers
    pinMode(BUZZER_ACTIVE, OUTPUT);
    digitalWrite(BUZZER_ACTIVE, LOW);
    
    // Initialize passive buzzer with LEDC
    // Returns actual frequency, 0 means error
    double freq = ledcAttach(BUZZER_PASSIVE, 2000, 8);  // Pin, 2kHz frequency, 8-bit resolution
    if (freq == 0) {
        Serial.println("WARNING: Failed to attach LEDC to passive buzzer!");
    } else {
        Serial.printf("Passive buzzer initialized at %.2f Hz\n", freq);
    }
    
    // All LEDs on briefly for test
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_WHITE, HIGH);
    digitalWrite(LED_BLUE, HIGH);
    delay(500);
    // Turn off all LEDs
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);
    
    // Test buzzers at startup
    Serial.println("\\nTesting buzzers...");
    playBeep(2);  // Active buzzer test
    delay(200);
    Serial.println("Testing passive buzzer (1 kHz tone, 500ms)...");
    playTone(1000, 500);  // Passive buzzer test
    Serial.println("Buzzer tests complete\\n");
    
    // Show initial battery level (before connection)
    showBatteryLevel();
    
    Serial.println("\n=================================");
    Serial.println("Fake M25 Wheel Simulator");
    Serial.println("=================================");
    Serial.println();
    Serial.println("Device name: " + String(DEVICE_NAME));
    Serial.println();
    Serial.println("Hardware:");
    Serial.println("  White LED (Pin " + String(LED_WHITE) + ") - Connection Status");
    Serial.println("  Blue LED (Pin " + String(LED_BLUE) + ") - Speed Indicator");
    Serial.println("  Red LED (Pin " + String(LED_RED) + ") - Low Battery");
    Serial.println("  Yellow LED (Pin " + String(LED_YELLOW) + ") - Medium Battery");
    Serial.println("  Green LED (Pin " + String(LED_GREEN) + ") - High Battery");
    Serial.println("  Passive Buzzer - Speed Tone (frequency indicates speed)");
    Serial.println("    Wiring: Pin 23 -> Buzzer+, Buzzer- -> GND");
    Serial.println("  Active Buzzer - Event Beeps");
    Serial.println("  Button (Pin " + String(BUTTON_PIN) + ") - Force Advertising");
    Serial.println();

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
    
    // Display encryption key
    printKey();
    
    Serial.println("Type 'help' for available commands");
    Serial.println();
    
    // Start with advertising state
    setLEDState(LED_ADVERTISING);
}

void loop() {
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
        currentSpeed = 0;  // Reset speed on disconnect
    }
    
    // Update LED states
    updateLEDs();
    
    // Handle button press
    handleButton();
    
    // Handle serial commands
    handleSerialCommand();
    
    // Update speed indicators (RGB LED, buzzer, blue LED)
    updateSpeedIndicators();
    
    // Simulate battery drain (very slow)
    static unsigned long lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate > 60000) {  // Every minute
        if (batteryLevel > 0) {
            int oldLevel = batteryLevel;
            batteryLevel--;
            Serial.print("Battery: ");
            Serial.print(batteryLevel);
            Serial.println("%");
            // Only update LED if battery crosses threshold (no spam)
            int oldThreshold = (oldLevel > 66) ? 2 : (oldLevel > 33) ? 1 : 0;
            int newThreshold = (batteryLevel > 66) ? 2 : (batteryLevel > 33) ? 1 : 0;
            if (oldThreshold != newThreshold) {
                showBatteryLevel();
            }
        }
        lastBatteryUpdate = millis();
    }
    
    delay(20);  // Reduced delay for better responsiveness
}

void handleSerialCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.length() == 0) return;
    
    Serial.print("> ");
    Serial.println(cmd);
    
    // Parse command
    int spaceIndex = cmd.indexOf(' ');
    String command = spaceIndex > 0 ? cmd.substring(0, spaceIndex) : cmd;
    String arg = spaceIndex > 0 ? cmd.substring(spaceIndex + 1) : "";
    
    command.toLowerCase();
    
    // Process commands
    if (command == "help") {
        printHelp();
    }
    else if (command == "battery") {
        if (arg.length() > 0) {
            int value = arg.toInt();
            if (value >= 0 && value <= 100) {
                batteryLevel = value;
                Serial.print("Battery set to ");
                Serial.print(batteryLevel);
                Serial.println("%");
                showBatteryLevel();  // Update LED to match new level
            } else {
                Serial.println("Error: Battery must be 0-100");
            }
        } else {
            Serial.print("Current battery: ");
            Serial.print(batteryLevel);
            Serial.println("%");
        }
    }
    else if (command == "speed") {
        if (arg.length() > 0) {
            currentSpeed = arg.toInt();
            Serial.print("Speed set to ");
            Serial.println(currentSpeed);
        } else {
            Serial.print("Current speed: ");
            Serial.println(currentSpeed);
        }
    }
    else if (command == "assist") {
        if (arg.length() > 0) {
            int value = arg.toInt();
            if (value >= 0 && value <= 2) {
                assistLevel = value;
                Serial.print("Assist level set to ");
                Serial.println(assistLevel);
            } else {
                Serial.println("Error: Assist level must be 0-2");
            }
        } else {
            Serial.print("Current assist level: ");
            Serial.println(assistLevel);
        }
    }
    else if (command == "profile") {
        if (arg.length() > 0) {
            int value = arg.toInt();
            if (value >= 0 && value <= 5) {
                driveProfile = value;
                Serial.print("Drive profile set to ");
                Serial.println(driveProfile);
            } else {
                Serial.println("Error: Profile must be 0-5");
            }
        } else {
            Serial.print("Current drive profile: ");
            Serial.println(driveProfile);
        }
    }
    else if (command == "hillhold") {
        if (arg == "on" || arg == "1") {
            hillHold = true;
            Serial.println("Hill hold enabled");
        } else if (arg == "off" || arg == "0") {
            hillHold = false;
            Serial.println("Hill hold disabled");
        } else {
            hillHold = !hillHold;
            Serial.print("Hill hold: ");
            Serial.println(hillHold ? "ON" : "OFF");
        }
    }
    else if (command == "debug") {
        if (arg == "" || arg == "status") {
            Serial.printf("Debug flags: 0x%02X\n", debugFlags);
            Serial.printf("  Protocol:  %s (0x01)\n", (debugFlags & DBG_PROTOCOL)  ? "ON" : "off");
            Serial.printf("  Crypto:    %s (0x02)\n", (debugFlags & DBG_CRYPTO)    ? "ON" : "off");
            Serial.printf("  CRC:       %s (0x04)\n", (debugFlags & DBG_CRC)       ? "ON" : "off");
            Serial.printf("  Commands:  %s (0x08)\n", (debugFlags & DBG_COMMANDS)  ? "ON" : "off");
            Serial.printf("  Raw data:  %s (0x10)\n", (debugFlags & DBG_RAW_DATA)  ? "ON" : "off");
        } else if (arg == "help") {
            printDebugHelp();
        } else if (arg == "all") {
            debugFlags = 0xFF;
            Serial.println("All debug flags enabled");
        } else if (arg == "none") {
            debugFlags = 0x00;
            Serial.println("All debug flags disabled");
        } else if (arg == "protocol") {
            debugFlags ^= DBG_PROTOCOL;
            Serial.printf("Protocol debug: %s\n", (debugFlags & DBG_PROTOCOL) ? "ON" : "off");
        } else if (arg == "crypto") {
            debugFlags ^= DBG_CRYPTO;
            Serial.printf("Crypto debug: %s\n", (debugFlags & DBG_CRYPTO) ? "ON" : "off");
        } else if (arg == "crc") {
            debugFlags ^= DBG_CRC;
            Serial.printf("CRC debug: %s\n", (debugFlags & DBG_CRC) ? "ON" : "off");
        } else if (arg == "commands") {
            debugFlags ^= DBG_COMMANDS;
            Serial.printf("Commands debug: %s\n", (debugFlags & DBG_COMMANDS) ? "ON" : "off");
        } else if (arg == "raw") {
            debugFlags ^= DBG_RAW_DATA;
            Serial.printf("Raw data debug: %s\n", (debugFlags & DBG_RAW_DATA) ? "ON" : "off");
        } else {
            Serial.println("Unknown debug option. Use 'debug help'");
        }
    }
    else if (command == "status") {
        Serial.println("\n=== Wheel Status ===");
        Serial.print("Device: ");
        Serial.println(DEVICE_NAME);
        Serial.print("Connected: ");
        Serial.println(deviceConnected ? "YES" : "NO");
        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");
        Serial.print("Speed: ");
        Serial.println(currentSpeed);
        Serial.print("Assist Level: ");
        Serial.println(assistLevel);
        Serial.print("Drive Profile: ");
        Serial.println(driveProfile);
        Serial.print("Hill Hold: ");
        Serial.println(hillHold ? "ON" : "OFF");
        Serial.printf("Debug Flags: 0x%02X\n", debugFlags);
        Serial.printf("Audio Feedback: %s\n", audioFeedbackEnabled ? "ON" : "OFF");
        Serial.printf("Visual Feedback: %s\n", visualFeedbackEnabled ? "ON" : "OFF");
        Serial.print("Wheel Rotations: ");
        Serial.println(wheelRotation);
        Serial.print("Distance: ");
        Serial.print(distanceTraveled);
        Serial.println(" m");
        Serial.println("==================\n");
    }
    else if (command == "send") {
        if (deviceConnected) {
            sendResponse();
            Serial.println("Sent response packet");
        } else {
            Serial.println("Error: No client connected");
        }
    }
    else if (command == "disconnect") {
        if (deviceConnected) {
            pServer->disconnect(pServer->getConnId());
            Serial.println("Disconnected client");
        } else {
            Serial.println("No client connected");
        }
    }
    else if (command == "advertise") {
        BLEDevice::startAdvertising();
        Serial.println("Forced BLE advertising restart");
        Serial.println("Broadcasting as: " + String(DEVICE_NAME));
        Serial.println("Service UUID: " + String(SERVICE_UUID));
    }
    else if (command == "mac") {
        Serial.print("MAC Address: ");
        Serial.println(BLEDevice::getAddress().toString().c_str());
    }
    else if (command == "key") {
        printKey();
    }
    else if (command == "rotate" || command == "wheel") {
        int rotations = arg.length() > 0 ? arg.toInt() : 1;
        simulateWheelRotation(rotations);
    }
    else if (command == "reset") {
        wheelRotation = 0;
        distanceTraveled = 0.0;
        Serial.println("Wheel rotation counter reset");
    }
    else if (command == "audio") {
        if (arg == "on" || arg == "1") {
            audioFeedbackEnabled = true;
            Serial.println("Audio feedback enabled");
            playBeep(1);
        } else if (arg == "off" || arg == "0") {
            audioFeedbackEnabled = false;
            playTone(0, 0);  // Stop any playing tone
            Serial.println("Audio feedback disabled");
        } else {
            audioFeedbackEnabled = !audioFeedbackEnabled;
            Serial.print("Audio feedback: ");
            Serial.println(audioFeedbackEnabled ? "ON" : "OFF");
            if (audioFeedbackEnabled) playBeep(1);
        }
    }
    else if (command == "visual") {
        if (arg == "on" || arg == "1") {
            visualFeedbackEnabled = true;
            Serial.println("Visual feedback enabled");
        } else if (arg == "off" || arg == "0") {
            visualFeedbackEnabled = false;
            digitalWrite(LED_BLUE, LOW);
            Serial.println("Visual feedback disabled");
        } else {
            visualFeedbackEnabled = !visualFeedbackEnabled;
            Serial.print("Visual feedback: ");
            Serial.println(visualFeedbackEnabled ? "ON" : "OFF");
        }
    }
    else if (command == "beep") {
        int count = arg.length() > 0 ? arg.toInt() : 1;
        if (count > 0 && count <= 10) {
            playBeep(count);
            Serial.printf("Playing %d beep(s)\n", count);
        } else {
            Serial.println("Beep count must be 1-10");
        }
    }
    else if (command == "tone") {
        int freq = arg.toInt();
        if (freq >= 50 && freq <= 5000) {
            playTone(freq, 500);
            Serial.printf("Playing %d Hz tone\n", freq);
        } else {
            Serial.println("Frequency must be 50-5000 Hz");
        }
    }
    else {
        Serial.print("Unknown command: ");
        Serial.println(command);
        Serial.println("Type 'help' for available commands");
    }
}

void printHelp() {
    Serial.println("\n=== Available Commands ===");
    Serial.println("help              - Show this help");
    Serial.println("status            - Show current wheel state");
    Serial.println("mac               - Show BLE MAC address");
    Serial.println("key               - Show encryption key");
    Serial.println("battery [0-100]   - Set/show battery level");
    Serial.println("speed <value>     - Set/show simulated speed");
    Serial.println("assist [0-2]      - Set/show assist level");
    Serial.println("profile [0-5]     - Set/show drive profile");
    Serial.println("hillhold [on/off] - Toggle hill hold");
    Serial.println("rotate [n]        - Simulate n wheel rotations (default=1)");
    Serial.println("reset             - Reset wheel rotation counter");
    Serial.println("debug [option]    - Control debug flags (use 'debug help')");
    Serial.println("audio [on/off]    - Toggle audio feedback (buzzer)");
    Serial.println("visual [on/off]   - Toggle visual feedback (Blue LED)");
    Serial.println("beep [count]      - Play beeps (1-10)");
    Serial.println("tone <freq>       - Play tone (50-5000 Hz)");
    Serial.println("send              - Send response packet now");
    Serial.println("disconnect        - Disconnect client");
    Serial.println("advertise         - Force restart BLE advertising");
    Serial.println("========================\n");
}

void printDebugHelp() {
    Serial.println("\n=== Debug Flags ===");
    Serial.println("debug [status]       - Show current flags");
    Serial.println("debug help           - This help");
    Serial.println("debug all            - Enable all flags");
    Serial.println("debug none           - Disable all flags");
    Serial.println("debug protocol       - Toggle protocol parsing (0x01)");
    Serial.println("debug crypto         - Toggle encryption steps (0x02)");
    Serial.println("debug crc            - Toggle CRC validation (0x04)");
    Serial.println("debug commands       - Toggle command decode (0x08)");
    Serial.println("debug raw            - Toggle raw hex dumps (0x10)");
    Serial.println("===================\n");
}

// LED Management Functions
void updateLEDs() {
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    unsigned long currentMillis = millis();
    
    // Handle blinking states
    if (currentLEDState == LED_ERROR) {
        // Error: Blue blinks slow (500ms interval)
        if (currentMillis - lastBlink >= 500) {
            blinkState = !blinkState;
            digitalWrite(LED_BLUE, blinkState ? HIGH : LOW);
            lastBlink = currentMillis;
        }
    } else if (!deviceConnected) {
        // Advertising: White blinks fast (250ms interval)
        if (currentMillis - lastBlink >= 250) {
            blinkState = !blinkState;
            digitalWrite(LED_WHITE, blinkState ? HIGH : LOW);
            lastBlink = currentMillis;
        }
    }
}

void setLEDState(LEDState state) {
    currentLEDState = state;
    
    // Turn white LED off (battery LEDs controlled separately)
    digitalWrite(LED_WHITE, LOW);
    
    switch (state) {
        case LED_ADVERTISING:
            // Will blink in updateLEDs()
            Serial.println("LED: Advertising mode (white blinking)");
            break;
            
        case LED_CONNECTING:
            digitalWrite(LED_WHITE, HIGH);
            Serial.println("LED: Connecting (white slow blink)");
            break;
            
        case LED_CONNECTED:
            digitalWrite(LED_WHITE, HIGH);
            Serial.println("LED: Connected (white solid)");
            break;
            
        case LED_ERROR:
            // Will blink in updateLEDs()
            Serial.println("LED: Error (blue blinking slow)");
            break;
            
        case LED_BATTERY:
            showBatteryLevel();
            break;
    }
}

void showBatteryLevel() {
    // Debug output
    Serial.print("showBatteryLevel() called - Battery: " + String(batteryLevel) + "% -> ");
    
    // Turn all battery LEDs off first
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    
    if (batteryLevel > 66) {
        // High battery: Green LED
        digitalWrite(LED_GREEN, HIGH);
        Serial.println("GREEN LED");
    } else if (batteryLevel > 33) {
        // Medium battery: Yellow LED
        digitalWrite(LED_YELLOW, HIGH);
        Serial.println("YELLOW LED");
    } else {
        // Low battery: Red LED
        digitalWrite(LED_RED, HIGH);
        Serial.println("RED LED");
    }
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
        }
        
        Serial.println("Force advertising...");
        BLEDevice::startAdvertising();
        Serial.println("Broadcasting as: " + String(DEVICE_NAME));
        setLEDState(LED_ADVERTISING);
        
        // Wait for button release
        while (digitalRead(BUTTON_PIN) == LOW) {
            delay(10);
        }
        setLEDState(LED_BATTERY);
        Serial.println("Button released\n");
        
        // Reset debounce timer
        lastDebounceTime = millis();
    }
}

// Encryption Functions
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

void printKey() {
    Serial.println("\n=== Encryption Key ===");
    Serial.print("Key (hex): ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02X ", encryptionKey[i]);
        if (i == 7) Serial.print(" ");
    }
    Serial.println();
    Serial.println("======================\n");
}

void printPacket(uint8_t* data, size_t len) {
    Serial.println("\n--- Packet Contents ---");
    
    // Print full hex dump
    Serial.print("Raw (hex): ");
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
        if ((i + 1) % 8 == 0 && i < len - 1) {
            Serial.print(" ");
        }
    }
    Serial.println();
    
    // Parse packet structure (typical M25 format)
    if (len >= 16) {
        Serial.println("\nParsed Structure:");
        Serial.printf("  Header:    %02X %02X\n", data[0], data[1]);
        Serial.printf("  Command:   %02X\n", data[2]);
        Serial.printf("  Flags:     %02X\n", data[3]);
        
        if (len >= 8) {
            Serial.print("  Payload:   ");
            for (size_t i = 4; i < (len > 12 ? 12 : len - 2); i++) {
                Serial.printf("%02X ", data[i]);
            }
            Serial.println();
        }
        
        Serial.printf("  Checksum:  %02X %02X\n", data[len - 2], data[len - 1]);
    }
    
    Serial.println("----------------------");
}

bool validatePacket(uint8_t* data, size_t len) {
    // Legacy function - validation now done in handleCommand()
    // Kept for compatibility with old code paths
    if (len < 4) {
        if (debugFlags & DBG_PROTOCOL) {
            Serial.println("Validation error: Packet too short (< 4 bytes)");
        }
        return false;
    }
    return true;
}

void simulateWheelRotation(int rotations) {
    wheelRotation += rotations;
    distanceTraveled += rotations * 2.0; // Assuming ~2m per rotation
    
    Serial.print("Wheel turned by ");
    Serial.print(rotations);
    Serial.print(" rotation(s) - Total: ");
    Serial.print(wheelRotation);
    Serial.print(" rotations, ");
    Serial.print(distanceTraveled);
    Serial.println(" m traveled");
    
    // Simulate small battery drain with distance
    if (wheelRotation % 100 == 0 && batteryLevel > 0) {
        batteryLevel--;
        Serial.print("Battery drained to ");
        Serial.print(batteryLevel);
        Serial.println("%");
        showBatteryLevel();
    }
}

// Play tone on passive buzzer
void playTone(uint16_t frequency, uint16_t duration) {
    if (!audioFeedbackEnabled || frequency == 0) {
        ledcWriteTone(BUZZER_PASSIVE, 0);
        ledcWrite(BUZZER_PASSIVE, 0);  // Stop PWM
        return;
    }
    
    // Set frequency and write 50% duty cycle (128 out of 255)
    double actualFreq = ledcWriteTone(BUZZER_PASSIVE, frequency);
    if (actualFreq > 0) {
        ledcWrite(BUZZER_PASSIVE, 128);  // 50% duty cycle for passive buzzer
    }
    
    if (duration > 0) {
        delay(duration);
        ledcWriteTone(BUZZER_PASSIVE, 0);
        ledcWrite(BUZZER_PASSIVE, 0);
    }
}

// Play beep(s) on active buzzer
void playBeep(uint8_t count) {
    if (!audioFeedbackEnabled) return;
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(BUZZER_ACTIVE, HIGH);
        delay(50);
        digitalWrite(BUZZER_ACTIVE, LOW);
        if (i < count - 1) delay(100);
    }
}

// Update visual and audio feedback based on current speed
void updateSpeedIndicators() {
    if (!visualFeedbackEnabled && !audioFeedbackEnabled) return;
    
    unsigned long now = millis();
    
    // Calculate speed percentage and direction
    float speedPercent = abs(currentSpeed) / 2.5;  // M25_SPEED_SCALE = 2.5
    bool moving = abs(currentSpeed) > 5;  // Deadzone threshold
    
    // --- Blue LED: Speed intensity (blink rate increases with speed) ---
    if (visualFeedbackEnabled) {
        if (moving) {
            uint16_t blinkInterval = (uint16_t)constrain(500 - (speedPercent * 4), 50, 500);
            if (now - lastSpeedUpdate >= blinkInterval) {
                lastSpeedUpdate = now;
                digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
            }
        } else {
            digitalWrite(LED_BLUE, LOW);
        }
    }
    
    // --- Passive Buzzer: Tone frequency based on speed ---
    if (audioFeedbackEnabled && moving) {
        // Frequency range: 200Hz (slow) to 2000Hz (fast)
        uint16_t frequency = (uint16_t)constrain(200 + (speedPercent * 18), 200, 2000);
        playTone(frequency, 0);  // Continuous tone
    } else {
        playTone(0, 0);  // Stop tone
    }
}
