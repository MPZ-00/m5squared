/*
 * WiFi Virtual Joystick Controller for M25 Wheels
 * 
 * Creates a WiFi access point that serves a web-based virtual joystick.
 * Connect with your phone/tablet to control the wheelchair via BLE.
 * 
 * Features:
 * - WiFi AP mode (no router needed)
 * - HTML5 touch joystick interface (embedded)
 * - Real-time WebSocket communication
 * - Connects to M25 wheels via BLE
 * - AES encryption for M25 protocol
 * 
 * Setup:
 * 1. Configure device_config.h with encryption key and wheel MAC
 * 2. Upload this sketch to ESP32
 * 3. Connect phone to WiFi "M25-Controller" (password: "m25wheel")
 * 4. Open browser to http://192.168.4.1
 * 
 * Hardware: ESP32-WROOM-32 or similar
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <mbedtls/aes.h>

#include "device_config.h"
#include "index_html.h"

// Encryption key from config
const uint8_t encryptionKey[16] = ENCRYPTION_KEY;

// BLE Service and Characteristic UUIDs (SPP profile)
#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

// Get target wheel MAC address
#if defined(CONNECT_LEFT_WHEEL)
    #define TARGET_WHEEL_MAC LEFT_WHEEL_MAC
    #define TARGET_WHEEL_NAME "LEFT"
#elif defined(CONNECT_RIGHT_WHEEL)
    #define TARGET_WHEEL_MAC RIGHT_WHEEL_MAC
    #define TARGET_WHEEL_NAME "RIGHT"
#else
    #error "Define either CONNECT_LEFT_WHEEL or CONNECT_RIGHT_WHEEL in device_config.h"
#endif

// Web server on port 80
WebServer server(80);

// WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Joystick state
struct JoystickState {
    float x;  // -1.0 to 1.0 (left to right)
    float y;  // -1.0 to 1.0 (down to up)
    bool active;  // Deadman switch
} joystick = {0.0, 0.0, false};

// BLE connection state
bool bleConnected = false;
bool bleScanning = false;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pTxCharacteristic = nullptr;
BLERemoteCharacteristic* pRxCharacteristic = nullptr;
BLEAdvertisedDevice* targetDevice = nullptr;
unsigned long lastBLEReconnectAttempt = 0;

// Discovered wheels storage
#define MAX_DISCOVERED_WHEELS 20
struct DiscoveredWheel {
    String mac;
    String name;
    int rssi;
    bool valid;
};
DiscoveredWheel discoveredWheels[MAX_DISCOVERED_WHEELS];
int discoveredWheelCount = 0;
String selectedWheelMAC = "";
bool autoConnectEnabled = true;

// Timing
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_INTERVAL = 50;  // 20Hz update rate

// Forward declarations
void connectToBLE();
void disconnectBLE();
void sendWheelCommand(int leftSpeed, int rightSpeed);
bool encryptPacket(uint8_t* data, size_t len, uint8_t* output);
void sendBLEStatus();

// BLE Scan callback
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String deviceMAC = advertisedDevice.getAddress().toString().c_str();
        deviceMAC.toUpperCase();
        String deviceName = advertisedDevice.getName().c_str();
        int rssi = advertisedDevice.getRSSI();
        
        // Check if this looks like an M25 wheel (contains "M5" or "M25" in name)
        bool isM25Wheel = (deviceName.indexOf("M5") >= 0 || 
                          deviceName.indexOf("M25") >= 0 ||
                          deviceName.indexOf("Wheel") >= 0);
        
        // Also check against configured wheel MACs
        String leftMAC = String(LEFT_WHEEL_MAC);
        String rightMAC = String(RIGHT_WHEEL_MAC);
        leftMAC.toUpperCase();
        rightMAC.toUpperCase();
        
        if (deviceMAC == leftMAC || deviceMAC == rightMAC) {
            isM25Wheel = true;
        }
        
        if (isM25Wheel) {
            // Add to discovered wheels list if not already present
            bool alreadyFound = false;
            for (int i = 0; i < discoveredWheelCount; i++) {
                if (discoveredWheels[i].mac == deviceMAC) {
                    alreadyFound = true;
                    // Update RSSI
                    discoveredWheels[i].rssi = rssi;
                    break;
                }
            }
            
            if (!alreadyFound && discoveredWheelCount < MAX_DISCOVERED_WHEELS) {
                discoveredWheels[discoveredWheelCount].mac = deviceMAC;
                discoveredWheels[discoveredWheelCount].name = deviceName.length() > 0 ? deviceName : "Unknown";
                discoveredWheels[discoveredWheelCount].rssi = rssi;
                discoveredWheels[discoveredWheelCount].valid = true;
                discoveredWheelCount++;
                Serial.println("Found M25 wheel: " + deviceMAC + " (" + deviceName + ") RSSI: " + String(rssi));
            }
            
            // If this is the target device for auto-connect, save it
            if (selectedWheelMAC.length() > 0 && deviceMAC == selectedWheelMAC) {
                targetDevice = new BLEAdvertisedDevice(advertisedDevice);
            }
        }
    }
};

// BLE Client callbacks
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("BLE Connected!");
        bleConnected = true;
        sendBLEStatus();
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("BLE Disconnected!");
        bleConnected = false;
        sendBLEStatus();
        
        if (pClient != nullptr) {
            delete pClient;
            pClient = nullptr;
        }
    }
};

// Send BLE status to web interface
void sendBLEStatus() {
    String status = bleConnected ? "connected" : "disconnected";
    String json = "{\"bleStatus\":\"" + status + "\"}";
    webSocket.broadcastTXT(json);
}

// Send discovered wheels list to web interface
void sendDiscoveredWheels() {
    String json = "{\"wheels\":[";
    for (int i = 0; i < discoveredWheelCount; i++) {
        if (discoveredWheels[i].valid) {
            if (i > 0) json += ",";
            json += "{";
            json += "\"mac\":\"" + discoveredWheels[i].mac + "\",";
            json += "\"name\":\"" + discoveredWheels[i].name + "\",";
            json += "\"rssi\":" + String(discoveredWheels[i].rssi);
            json += "}";
        }
    }
    json += "],\"scanning\":" + String(bleScanning ? "true" : "false") + "}";
    webSocket.broadcastTXT(json);
}

// Start scanning for wheels
void startWheelScan() {
    if (bleScanning) {
        Serial.println("[BLE] Already scanning");
        return;
    }
    
    // Clear previous results
    discoveredWheelCount = 0;
    for (int i = 0; i < MAX_DISCOVERED_WHEELS; i++) {
        discoveredWheels[i].valid = false;
    }
    
    Serial.println("[BLE] Starting wheel scan...");
    bleScanning = true;
    
    // Notify clients that scanning started
    String json = "{\"scanning\":true}";
    webSocket.broadcastTXT(json);
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(BLE_SCAN_TIME, false);
}

// Connect to specific wheel by MAC address
void connectToWheel(String mac) {
    if (bleConnected) {
        Serial.println("[BLE] Already connected, disconnecting first...");
        disconnectBLE();
        delay(1000);
    }
    
    selectedWheelMAC = mac;
    mac.toUpperCase();
    Serial.println("[BLE] Connecting to wheel: " + mac);
    
    // Check if we already discovered this device
    bool foundInCache = false;
    for (int i = 0; i < discoveredWheelCount; i++) {
        if (discoveredWheels[i].valid && discoveredWheels[i].mac == mac) {
            foundInCache = true;
            Serial.println("[BLE] Using cached device info");
            break;
        }
    }
    
    if (!foundInCache) {
        Serial.println("[BLE] Device not in cache, scanning...");
        startWheelScan();
    } else {
        connectToBLE();
    }
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client %u disconnected\n", num);
            joystick.active = false;
            joystick.x = 0;
            joystick.y = 0;
            break;
            
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[WS] Client %u connected from %d.%d.%d.%d\n", 
                num, ip[0], ip[1], ip[2], ip[3]);
            // Send initial BLE status
            sendBLEStatus();
            break;
        }
        
        case WStype_TEXT: {
            String data = String((char*)payload);
            
            // Check for commands
            if (data.indexOf("\"command\"") >= 0) {
                if (data.indexOf("emergencyStop") >= 0) {
                    Serial.println("[WS] Emergency stop!");
                    joystick.active = false;
                    joystick.x = 0;
                    joystick.y = 0;
                    sendWheelCommand(0, 0);
                }
                else if (data.indexOf("scanWheels") >= 0) {
                    Serial.println("[WS] Scan wheels command received");
                    startWheelScan();
                }
                else if (data.indexOf("connectWheel") >= 0) {
                    // Parse MAC address from command: {"command":"connectWheel","mac":"AA:BB:CC:DD:EE:FF"}
                    int macPos = data.indexOf("\"mac\":\"") + 7;
                    int macEnd = data.indexOf("\"", macPos);
                    if (macPos > 6 && macEnd > macPos) {
                        String mac = data.substring(macPos, macEnd);
                        Serial.println("[WS] Connect to wheel: " + mac);
                        connectToWheel(mac);
                    }
                }
                else if (data.indexOf("disconnectWheel") >= 0) {
                    Serial.println("[WS] Disconnect wheel command received");
                    disconnectBLE();
                    sendBLEStatus();
                }
                else if (data.indexOf("getWheels") >= 0) {
                    Serial.println("[WS] Get wheels list command received");
                    sendDiscoveredWheels();
                }
                return;
            }
            
            // Parse joystick data: {"x":"0.500","y":"-0.300","active":true}
            int xPos = data.indexOf("\"x\":\"") + 5;
            int xEnd = data.indexOf("\"", xPos);
            int yPos = data.indexOf("\"y\":\"") + 5;
            int yEnd = data.indexOf("\"", yPos);
            int activePos = data.indexOf("\"active\":") + 9;
            
            if (xPos > 4 && yPos > 4) {
                joystick.x = data.substring(xPos, xEnd).toFloat();
                joystick.y = data.substring(yPos, yEnd).toFloat();
                joystick.active = data.substring(activePos, activePos + 4) == "true";
            }
            break;
        }
    }
}

// Convert joystick to wheel speeds
void joystickToWheelSpeeds(float joyX, float joyY, int& leftSpeed, int& rightSpeed) {
    // Differential drive calculation
    float left = joyY + joyX;
    float right = joyY - joyX;
    
    // Clamp to -1.0 to 1.0
    left = constrain(left, -1.0, 1.0);
    right = constrain(right, -1.0, 1.0);
    
    // Scale to M25 speed range (-100 to 100)
    leftSpeed = (int)(left * 100);
    rightSpeed = (int)(right * 100);
}

// Encrypt packet using AES-128-ECB
bool encryptPacket(uint8_t* data, size_t len, uint8_t* output) {
    if (len % 16 != 0) {
        Serial.println("Packet size must be multiple of 16");
        return false;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    if (mbedtls_aes_setkey_enc(&aes, encryptionKey, 128) != 0) {
        Serial.println("Failed to set encryption key");
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Encrypt each 16-byte block
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, 
                                   data + i, output + i) != 0) {
            Serial.println("Encryption failed");
            mbedtls_aes_free(&aes);
            return false;
        }
    }
    
    mbedtls_aes_free(&aes);
    return true;
}

// Send command to M25 wheel
void sendWheelCommand(int leftSpeed, int rightSpeed) {
    if (!bleConnected || !pRxCharacteristic) {
        return;
    }
    
    // Build M25 protocol packet (simplified)
    // NOTE: This is a basic implementation - may need adjustment
    uint8_t plainPacket[16] = {0};
    
    // Basic command structure (adjust as needed)
    plainPacket[0] = 0x01;  // Command ID
    plainPacket[1] = leftSpeed & 0xFF;
    plainPacket[2] = (leftSpeed >> 8) & 0xFF;
    plainPacket[3] = rightSpeed & 0xFF;
    plainPacket[4] = (rightSpeed >> 8) & 0xFF;
    
    // Encrypt packet
    uint8_t encrypted[16];
    if (encryptPacket(plainPacket, 16, encrypted)) {
        pRxCharacteristic->writeValue(encrypted, 16, false);
        Serial.printf("[BLE] Sent: L=%d, R=%d\n", leftSpeed, rightSpeed);
    } else {
        Serial.println("[BLE] Encryption failed!");
    }
}

// Connect to BLE wheel
void connectToBLE() {
    if (bleConnected || bleScanning) {
        return;
    }
    
    // Use selected wheel MAC or fallback to default
    String targetMAC = selectedWheelMAC.length() > 0 ? selectedWheelMAC : String(TARGET_WHEEL_MAC);
    targetMAC.toUpperCase();
    
    Serial.println("\n[BLE] Starting scan for wheel: " + targetMAC);
    bleScanning = true;
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(BLE_SCAN_TIME, false);
    
    // Wait for scan to complete
    delay(BLE_SCAN_TIME * 1000);
    bleScanning = false;
    
    // Send discovered wheels to clients
    sendDiscoveredWheels();
    
    if (targetDevice == nullptr) {
        Serial.println("[BLE] Wheel not found!");
        return;
    }
    
    Serial.println("[BLE] Connecting to wheel...");
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    if (!pClient->connect(targetDevice)) {
        Serial.println("[BLE] Connection failed!");
        delete pClient;
        pClient = nullptr;
        delete targetDevice;
        targetDevice = nullptr;
        return;
    }
    
    Serial.println("[BLE] Connected! Getting service...");
    
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService == nullptr) {
        Serial.println("[BLE] Service not found!");
        pClient->disconnect();
        return;
    }
    
    Serial.println("[BLE] Getting characteristics...");
    
    pTxCharacteristic = pRemoteService->getCharacteristic(CHAR_UUID_TX);
    pRxCharacteristic = pRemoteService->getCharacteristic(CHAR_UUID_RX);
    
    if (pTxCharacteristic == nullptr || pRxCharacteristic == nullptr) {
        Serial.println("[BLE] Characteristics not found!");
        pClient->disconnect();
        return;
    }
    
    Serial.println("[BLE] Setup complete!");
    bleConnected = true;
    sendBLEStatus();
}

// Disconnect from BLE
void disconnectBLE() {
    if (pClient != nullptr && bleConnected) {
        pClient->disconnect();
    }
    bleConnected = false;
}

// Handle 404 errors
void handleNotFound() {
    server.send(404, "text/plain", "File not found");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("M25 WiFi Virtual Joystick Controller");
    Serial.println("========================================\n");
    
    // Setup WiFi Access Point
    Serial.print("[WiFi] Creating AP: ");
    Serial.println(WIFI_SSID);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("[WiFi] AP IP address: ");
    Serial.println(IP);
    
    // Setup web server routes
    server.on("/", []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });
    
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[HTTP] Server started on port 80");
    
    // Setup WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("[WebSocket] Server started on port 81");
    
    // Initialize BLE
    Serial.println("[BLE] Initializing...");
    BLEDevice::init("M25-WiFi-Controller");
    Serial.println("[BLE] Target wheel (" + String(TARGET_WHEEL_NAME) + "): " + String(TARGET_WHEEL_MAC));
    
    // Print encryption key status
    bool keySet = false;
    for (int i = 0; i < 16; i++) {
        if (encryptionKey[i] != 0) {
            keySet = true;
            break;
        }
    }
    if (!keySet) {
        Serial.println("[WARNING] Encryption key is all zeros!");
        Serial.println("[WARNING] Set ENCRYPTION_KEY in device_config.h");
    } else {
        Serial.println("[Config] Encryption key configured");
    }
    
    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("========================================");
    Serial.println("1. Connect phone to WiFi: " + String(WIFI_SSID));
    Serial.println("2. Open browser to: http://192.168.4.1");
    Serial.println("3. Use web interface to scan and connect to wheels");
    Serial.println("========================================\n");
    
    // Initialize with default selected wheel for auto-connect
    selectedWheelMAC = String(TARGET_WHEEL_MAC);
    
    // Start BLE connection if auto-connect is enabled
    if (autoConnectEnabled) {
        Serial.println("[BLE] Auto-connect enabled");
        connectToBLE();
    } else {
        Serial.println("[BLE] Auto-connect disabled. Use web interface to scan and connect.");
    }
}

void loop() {
    // Handle web server requests
    server.handleClient();
    
    // Handle WebSocket events
    webSocket.loop();
    
    // Handle scan completion
    if (bleScanning) {
        BLEScan* pBLEScan = BLEDevice::getScan();
        if (!pBLEScan->isScanning()) {
            bleScanning = false;
            Serial.println("[BLE] Scan completed. Found " + String(discoveredWheelCount) + " wheels");
            sendDiscoveredWheels();
            
            // If we have a selected wheel and found it, connect automatically
            if (selectedWheelMAC.length() > 0 && targetDevice != nullptr) {
                connectToBLE();
            }
        }
    }
    
    // Try to reconnect BLE if disconnected and a wheel was selected
    if (!bleConnected && !bleScanning && selectedWheelMAC.length() > 0) {
        unsigned long now = millis();
        if (now - lastBLEReconnectAttempt > BLE_RECONNECT_DELAY) {
            lastBLEReconnectAttempt = now;
            Serial.println("[BLE] Attempting reconnection to " + selectedWheelMAC + "...");
            connectToBLE();
        }
    }
    
    // Send commands to wheel at fixed rate
    unsigned long currentTime = millis();
    if (currentTime - lastCommandTime >= COMMAND_INTERVAL) {
        lastCommandTime = currentTime;
        
        if (joystick.active && bleConnected) {
            // Convert joystick to wheel speeds
            int leftSpeed, rightSpeed;
            joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
            
            // Send to wheel
            sendWheelCommand(leftSpeed, rightSpeed);
        } else if (bleConnected) {
            // Deadman released or not active - send stop command
            sendWheelCommand(0, 0);
        }
    }
    
    delay(1);  // Small delay to prevent watchdog issues
}
