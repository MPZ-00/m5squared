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
 * 
 * Module Organization:
 * - encryption.h       - AES encryption/decryption
 * - wheel_command.h    - M25 wheel commands and joystick conversion
 * - websocket_handlers.h - WebSocket event handling
 * - ble_scanner.h      - BLE discovery and connection management
 * - serial_commands.h  - Serial interface for debugging/control
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>

#include "device_config.h"
#include "index_html.h"

// ============== Configuration ==============
const uint8_t encryptionKey[16] = ENCRYPTION_KEY;

#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

#if defined(CONNECT_LEFT_WHEEL)
    #define TARGET_WHEEL_MAC LEFT_WHEEL_MAC
    #define TARGET_WHEEL_NAME "LEFT"
#elif defined(CONNECT_RIGHT_WHEEL)
    #define TARGET_WHEEL_MAC RIGHT_WHEEL_MAC
    #define TARGET_WHEEL_NAME "RIGHT"
#else
    #error "Define either CONNECT_LEFT_WHEEL or CONNECT_RIGHT_WHEEL in device_config.h"
#endif

// ============== Types & Constants ==============
#define MAX_DISCOVERED_WHEELS 20

struct JoystickState {
    float x;
    float y;
    bool active;
};

struct DiscoveredWheel {
    String mac;
    String name;
    int rssi;
    bool valid;
};

// ============== Module Includes ==============
#include "encryption.h"
#include "wheel_command.h"
#include "websocket_handlers.h"
#include "ble_scanner.h"
#include "serial_commands.h"

// ============== Global State ==============
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

JoystickState joystick = {0.0, 0.0, false};

bool bleConnected = false;
bool bleScanning = false;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pTxCharacteristic = nullptr;
BLERemoteCharacteristic* pRxCharacteristic = nullptr;
BLEAdvertisedDevice* targetDevice = nullptr;
unsigned long lastBLEReconnectAttempt = 0;

DiscoveredWheel discoveredWheels[MAX_DISCOVERED_WHEELS];
int discoveredWheelCount = 0;
String selectedWheelMAC = "";
bool autoConnectEnabled = true;
bool autoReconnectEnabled = false;
bool debugMode = false;

unsigned long lastCommandTime = 0;
const unsigned long COMMAND_INTERVAL = 50;
unsigned long scanStartTime = 0;

// ============== Web Server Handler ==============
void handleNotFound() {
    server.send(404, "text/plain", "File not found");
}

// ============== Setup & Loop ==============
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
    Serial.println("Type 'help' for available serial commands\n");
    
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
    
    // Handle serial commands
    handleSerialCommand();
    
    // Handle scan completion
    if (bleScanning) {
        unsigned long scanElapsed = millis() - scanStartTime;
        if (scanElapsed >= (BLE_SCAN_TIME * 1000)) {
            bleScanning = false;
            BLEDevice::getScan()->stop();
            Serial.println("[BLE] Scan completed. Found " + String(discoveredWheelCount) + " wheels");
            sendDiscoveredWheels();
            
            // If we have a selected wheel and found it, connect automatically
            if (selectedWheelMAC.length() > 0 && targetDevice != nullptr) {
                connectToBLE();
            }
        }
    }
    
    // Try to reconnect BLE if disconnected and a wheel was selected
    if (!bleConnected && !bleScanning && selectedWheelMAC.length() > 0 && autoReconnectEnabled) {
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
            int leftSpeed, rightSpeed;
            joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
            sendWheelCommand(leftSpeed, rightSpeed);
        } else if (bleConnected) {
            sendWheelCommand(0, 0);
        }
    }
    
    delay(1);
}
