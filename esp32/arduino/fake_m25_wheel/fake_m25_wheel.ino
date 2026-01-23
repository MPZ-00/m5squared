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

void handleCommand(uint8_t* data, size_t len);
void sendResponse();

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

        if (len > 0) {
            Serial.print("Received packet (");
            Serial.print(len);
            Serial.print(" bytes): ");
            
            // Print as hex
            for (int i = 0; i < len; i++) {
                Serial.printf("%02X ", rxValue[i]);
            }
            Serial.println();
            
            // Try to decode command (will be encrypted in real use)
            handleCommand(rxValue, len);
        }
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
    Serial.print("Device name: ");
    Serial.println(DEVICE_NAME);
    Serial.println();

    // Initialize BLE
    BLEDevice::init(DEVICE_NAME);
    
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
