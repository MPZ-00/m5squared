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

// Configuration
#define DEVICE_NAME "M25_FAKE_LEFT"  // Change to M25_FAKE_RIGHT for second wheel
#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"  // SPP UUID
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

// Globals
BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
BLECharacteristic* pRxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Simulated wheel state
int currentSpeed = 0;
int batteryLevel = 85;  // 85%
int assistLevel = 1;    // 0-2
bool hillHold = false;
int driveProfile = 0;   // 0 = standard
bool debugMode = false; // Verbose logging

void handleCommand(uint8_t* data, size_t len);
void sendResponse();
void handleSerialCommand();
void printHelp();

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
    // In real implementation, data is AES-encrypted
    // For now, just acknowledge we received something
    
    Serial.print("Command length: ");
    Serial.println(len);
    
    // M25 commands are typically 16 or 32 bytes (AES blocks)
    if (len == 16 || len == 32) {
        Serial.println("Valid packet size (encrypted)");
        
        // Send a fake response after short delay
        delay(50);
        sendResponse();
    } else {
        Serial.println("Unexpected packet size");
    }
}

void sendResponse() {
    if (!deviceConnected) return;
    
    // Fake encrypted response (16 bytes)
    uint8_t response[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 
        0x00, 0x00, 0x00, 0x00,
        batteryLevel, assistLevel, driveProfile, 0x00,
        0x11, 0x22, 0x33, 0x44
    };
    
    pTxCharacteristic->setValue(response, 16);
    pTxCharacteristic->notify();
    
    Serial.println("Sent response");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=================================");
    Serial.println("Fake M25 Wheel Simulator");
    Serial.println("=================================");
    Serial.println();
    Serial.println("Device name: " + String(DEVICE_NAME));
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
    Serial.println("Type 'help' for available commands");
    Serial.println();
}

void loop() {
    // Handle connection state changes
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
        Serial.println("=== CONNECTED ===");
        Serial.println("Ready to receive commands");
        Serial.println();
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("=== DISCONNECTED ===");
        Serial.println("Advertising restarted");
        Serial.println();
        oldDeviceConnected = deviceConnected;
    }
    
    // Handle serial commands
    handleSerialCommand();
    
    // Simulate battery drain (very slow)
    static unsigned long lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate > 60000) {  // Every minute
        if (batteryLevel > 0) {
            batteryLevel--;
            Serial.print("Battery: ");
            Serial.print(batteryLevel);
            Serial.println("%");
        }
        lastBatteryUpdate = millis();
    }
    
    delay(100);
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
