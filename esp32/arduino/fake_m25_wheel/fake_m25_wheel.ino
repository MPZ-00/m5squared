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

// Hardware pins for visual feedback
// Wire colors: 3.3V=red, GND=brown, button/LEDs as marked below
// Can be overridden in device_config.h
#ifndef LED_RED_PIN
#define LED_RED_PIN 25      // D25 (orange wire) - Low battery (red)
#endif
#ifndef LED_YELLOW_PIN
#define LED_YELLOW_PIN 26   // D26 (yellow wire) - Medium battery (yellow)
#endif
#ifndef LED_GREEN_PIN
#define LED_GREEN_PIN 27    // D27 (turquoise wire) - High battery (green)
#endif
#ifndef LED_WHITE_PIN
#define LED_WHITE_PIN 32    // D32 (white wire) - Connection status
#endif
#ifndef LED_BLUE_PIN
#define LED_BLUE_PIN 14     // D14 - Error state (blue)
#endif
#ifndef BUTTON_PIN_OVERRIDE
#define BUTTON_PIN_OVERRIDE 33   // D33 (blue wire) - Button to force advertising
#endif

// Actual pin assignments (allows override)
#define LED_RED LED_RED_PIN
#define LED_YELLOW LED_YELLOW_PIN
#define LED_GREEN LED_GREEN_PIN
#define LED_WHITE LED_WHITE_PIN
#define LED_BLUE LED_BLUE_PIN
#define BUTTON_PIN BUTTON_PIN_OVERRIDE
#define BUTTON_DEBOUNCE 50  // Debounce delay in ms

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

// Simulated wheel state
int currentSpeed = 0;
int batteryLevel = 85;  // 85%
int assistLevel = 1;    // 0-2
bool hillHold = false;
int driveProfile = 0;   // 0 = standard
bool debugMode = false; // Verbose logging

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
bool decryptPacket(uint8_t* data, size_t len, uint8_t* output);
void encryptPacket(uint8_t* data, size_t len, uint8_t* output);
void printKey();

// Server callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Client connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Client disconnected!");
    }
};

// Characteristic callbacks - handle received data
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        uint8_t* rxValue = pCharacteristic->getData();
        size_t len = pCharacteristic->getValue().length();

        if (len <= 0) return;
        
        Serial.print("Received packet (" + String(len) + " bytes): ");
        
        // Print as hex
        for (int i = 0; i < len; i++) {
            Serial.printf("%02X ", rxValue[i]);
        }
        Serial.println();
        
        // Try to decode command (will be encrypted in real use)
        handleCommand(rxValue, len);
    }
};

void handleCommand(uint8_t* data, size_t len) {
    Serial.print("Command length: ");
    Serial.println(len);
    
    // M25 commands are typically 16 or 32 bytes (AES blocks)
    if (len != 16 && len != 32) {
        Serial.println("Unexpected packet size");
        return;
    }
    
    Serial.println("Valid packet size (encrypted)");
    
    // Decrypt packet
    uint8_t decrypted[32];
    if (decryptPacket(data, len, decrypted)) {
        Serial.print("Decrypted: ");
        for (size_t i = 0; i < len; i++) {
            Serial.printf("%02X ", decrypted[i]);
        }
        Serial.println();
        
        // Parse command from decrypted data
        // TODO: Implement actual command parsing
        
        // Send response after short delay
        delay(50);
        sendResponse();
    } else {
        Serial.println("Decryption failed");
    }
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
    encryptPacket(plainResponse, 16, encrypted);
    
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
    Serial.println("  Red LED (Pin " + String(LED_RED) + ") - Low Battery");
    Serial.println("  Yellow LED (Pin " + String(LED_YELLOW) + ") - Medium Battery");
    Serial.println("  Green LED (Pin " + String(LED_GREEN) + ") - High Battery");
    Serial.println("  Blue LED (Pin " + String(LED_BLUE) + ") - Error State");
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
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("=== DISCONNECTED ===");
        Serial.println("Advertising restarted");
        Serial.println();
        oldDeviceConnected = deviceConnected;
        setLEDState(LED_ADVERTISING);
    }
    
    // Update LED states
    updateLEDs();
    
    // Handle button press
    handleButton();
    
    // Handle serial commands
    handleSerialCommand();
    
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
        debugMode = !debugMode;
        Serial.print("Debug mode: ");
        Serial.println(debugMode ? "ON" : "OFF");
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
        Serial.print("Debug: ");
        Serial.println(debugMode ? "ON" : "OFF");
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
    Serial.println("debug             - Toggle debug mode");
    Serial.println("send              - Send response packet now");
    Serial.println("disconnect        - Disconnect client");
    Serial.println("advertise         - Force restart BLE advertising");
    Serial.println("========================\n");
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

bool decryptPacket(uint8_t* data, size_t len, uint8_t* output) {
    if (len % 16 != 0) {
        Serial.println("Error: Data length must be multiple of 16");
        return false;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Set decryption key
    if (mbedtls_aes_setkey_dec(&aes, encryptionKey, 128) != 0) {
        Serial.println("Error: Failed to set decryption key");
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Decrypt each 16-byte block
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + i, output + i) != 0) {
            Serial.println("Error: Decryption failed");
            mbedtls_aes_free(&aes);
            return false;
        }
    }
    
    mbedtls_aes_free(&aes);
    return true;
}

void encryptPacket(uint8_t* data, size_t len, uint8_t* output) {
    if (len % 16 != 0) {
        Serial.println("Error: Data length must be multiple of 16");
        return;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Set encryption key
    if (mbedtls_aes_setkey_enc(&aes, encryptionKey, 128) != 0) {
        Serial.println("Error: Failed to set encryption key");
        mbedtls_aes_free(&aes);
        return;
    }
    
    // Encrypt each 16-byte block
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data + i, output + i) != 0) {
            Serial.println("Error: Encryption failed");
            mbedtls_aes_free(&aes);
            return;
        }
    }
    
    mbedtls_aes_free(&aes);
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
