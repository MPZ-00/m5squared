#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

// Declared in main sketch
extern bool debugMode;
extern bool verboseLogging;
extern bool autoConnectEnabled;
extern bool autoReconnectEnabled;
extern String selectedWheelMAC;
extern bool bleConnected;
extern int discoveredWheelCount;
extern DiscoveredWheel discoveredWheels[];
extern JoystickState joystick;
extern const uint8_t encryptionKey[16];
extern bool continuousJoystickMonitor;
extern unsigned long lastSerialActivity;
extern unsigned long lastJoystickPrint;
extern const unsigned long JOYSTICK_MONITOR_INTERVAL;
extern const unsigned long SERIAL_TIMEOUT;

// Declared in other modules
void startWheelScan();
void connectToWheel(String mac);
void connectToBLE();
void disconnectBLE();
void sendBLEStatus();
void joystickToWheelSpeeds(float joyX, float joyY, int& leftSpeed, int& rightSpeed);
void sendWheelCommand(int leftSpeed, int rightSpeed);

// Local helpers
void printHelp();
void printStatus();
void printKey();
void handleContinuousMonitoring();

inline void handleSerialCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.length() == 0) return;
    
    // Track serial activity for continuous monitoring timeout
    lastSerialActivity = millis();
    
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
    else if (command == "status") {
        printStatus();
    }
    else if (command == "scan") {
        Serial.println("Starting BLE scan for wheels...");
        startWheelScan();
    }
    else if (command == "connect") {
        if (arg.length() > 0) {
            Serial.println("Connecting to wheel: " + arg);
            connectToWheel(arg);
        } else if (selectedWheelMAC.length() > 0) {
            Serial.println("Connecting to selected wheel: " + selectedWheelMAC);
            connectToBLE();
        } else {
            Serial.println("Error: No wheel MAC specified. Usage: connect <MAC>");
            Serial.println("Or use 'scan' first to discover wheels");
        }
    }
    else if (command == "disconnect") {
        if (bleConnected) {
            Serial.println("Disconnecting from wheel...");
            disconnectBLE();
            sendBLEStatus();
        } else {
            Serial.println("Not connected to any wheel");
        }
    }
    else if (command == "wheels") {
        Serial.println("\n=== Discovered Wheels ===");
        if (discoveredWheelCount == 0) {
            Serial.println("No wheels discovered. Use 'scan' command first.");
        } else {
            for (int i = 0; i < discoveredWheelCount; i++) {
                if (discoveredWheels[i].valid) {
                    Serial.print(i + 1);
                    Serial.print(". ");
                    Serial.print(discoveredWheels[i].mac);
                    Serial.print(" - ");
                    Serial.print(discoveredWheels[i].name);
                    Serial.print(" (RSSI: ");
                    Serial.print(discoveredWheels[i].rssi);
                    Serial.println(")");
                    if (discoveredWheels[i].mac == selectedWheelMAC) {
                        Serial.println("   ^ Selected for connection");
                    }
                }
            }
        }
        Serial.println("========================\n");
    }
    else if (command == "select") {
        if (arg.length() > 0) {
            selectedWheelMAC = arg;
            selectedWheelMAC.toUpperCase();
            Serial.println("Selected wheel: " + selectedWheelMAC);
            Serial.println("Use 'connect' to connect to this wheel");
        } else {
            Serial.println("Error: MAC address required. Usage: select <MAC>");
        }
    }
    else if (command == "autoconnect") {
        if (arg == "on" || arg == "1") {
            autoConnectEnabled = true;
            Serial.println("Auto-connect enabled");
        } else if (arg == "off" || arg == "0") {
            autoConnectEnabled = false;
            Serial.println("Auto-connect disabled");
        } else {
            autoConnectEnabled = !autoConnectEnabled;
            Serial.print("Auto-connect: ");
            Serial.println(autoConnectEnabled ? "ON" : "OFF");
        }
    }
    else if (command == "autoreconnect") {
        if (arg == "on" || arg == "1") {
            autoReconnectEnabled = true;
            Serial.println("Auto-reconnect enabled");
        } else if (arg == "off" || arg == "0") {
            autoReconnectEnabled = false;
            Serial.println("Auto-reconnect disabled");
        } else {
            autoReconnectEnabled = !autoReconnectEnabled;
            Serial.print("Auto-reconnect: ");
            Serial.println(autoReconnectEnabled ? "ON" : "OFF");
        }
    }
    else if (command == "wifi") {
        Serial.println("\n=== WiFi Status ===");
        Serial.print("SSID: ");
        Serial.println(WIFI_SSID);
        Serial.print("IP Address: ");
        Serial.println(WiFi.softAPIP());
        Serial.print("Connected Clients: ");
        Serial.println(WiFi.softAPgetStationNum());
        Serial.println("===================\n");
    }
    else if (command == "joystick") {
        if (debugMode && arg != "once") {
            // Start continuous monitoring in debug mode
            continuousJoystickMonitor = !continuousJoystickMonitor;
            if (continuousJoystickMonitor) {
                Serial.println("[Debug] Continuous joystick monitoring started");
                Serial.println("[Debug] Will stop after 5 seconds of inactivity or on any command");
                lastSerialActivity = millis();
                lastJoystickPrint = 0;  // Print immediately
            } else {
                Serial.println("[Debug] Continuous joystick monitoring stopped");
            }
        } else {
            // Single shot
            Serial.println("\n=== Joystick State ===");
            Serial.print("X: ");
            Serial.println(joystick.x, 3);
            Serial.print("Y: ");
            Serial.println(joystick.y, 3);
            Serial.print("Active: ");
            Serial.println(joystick.active ? "YES" : "NO");
            
            if (joystick.active) {
                int leftSpeed, rightSpeed;
                joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
                Serial.print("Left Speed: ");
                Serial.println(leftSpeed);
                Serial.print("Right Speed: ");
                Serial.println(rightSpeed);
            }
            Serial.println("======================\n");
        }
    }
    else if (command == "stopinfo" || command == "stopmonitor") {
        if (continuousJoystickMonitor) {
            continuousJoystickMonitor = false;
            Serial.println("[Debug] Continuous monitoring stopped");
        } else {
            Serial.println("No continuous monitoring active");
        }
    }
    else if (command == "stop") {
        Serial.println("Emergency stop!");
        joystick.active = false;
        joystick.x = 0;
        joystick.y = 0;
        sendWheelCommand(0, 0);
        Serial.println("All movement stopped");
    }
    else if (command == "debug") {
        debugMode = !debugMode;
        Serial.print("Debug mode: ");
        Serial.println(debugMode ? "ON" : "OFF");
        // Stop monitoring if debug is turned off
        if (!debugMode && continuousJoystickMonitor) {
            continuousJoystickMonitor = false;
            Serial.println("[Debug] Continuous monitoring stopped");
        }
    }
    else if (command == "verbose") {
        verboseLogging = !verboseLogging;
        Serial.print("Verbose logging: ");
        Serial.println(verboseLogging ? "ON" : "OFF");
    }
    else if (command == "key") {
        printKey();
    }
    else if (command == "mac") {
        Serial.print("BLE MAC Address: ");
        Serial.println(BLEDevice::getAddress().toString().c_str());
    }
    else if (command == "restart") {
        Serial.println("Restarting ESP32...");
        delay(500);
        ESP.restart();
    }
    else {
        Serial.print("Unknown command: ");
        Serial.println(command);
        Serial.println("Type 'help' for available commands");
    }
}

inline void printHelp() {
    Serial.println("\n=== Available Commands ===");
    Serial.println("help              - Show this help");
    Serial.println("status            - Show system status");
    Serial.println("scan              - Scan for M25 wheels");
    Serial.println("wheels            - List discovered wheels");
    Serial.println("select <MAC>      - Select wheel for connection");
    Serial.println("connect [MAC]     - Connect to wheel (selected or specified)");
    Serial.println("disconnect        - Disconnect from current wheel");
    Serial.println("autoconnect       - Toggle auto-connect on startup");
    Serial.println("autoreconnect     - Toggle auto-reconnect when disconnected");
    Serial.println("wifi              - Show WiFi AP status");
    Serial.println("joystick [once]   - Show joystick (continuous if debug on)");
    Serial.println("stopinfo          - Stop continuous monitoring");
    Serial.println("stop              - Emergency stop (zero all movement)");
    Serial.println("key               - Show encryption key");
    Serial.println("mac               - Show BLE MAC address");
    Serial.println("debug             - Toggle debug mode");
    Serial.println("verbose           - Toggle verbose logging (BLE commands)");
    Serial.println("restart           - Restart ESP32");
    Serial.println("==========================\n");
}

inline void printStatus() {
    Serial.println("\n=== WiFi Joystick Status ===");
    
    // WiFi Info
    Serial.println("\nWiFi AP:");
    Serial.print("  SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("  Clients: ");
    Serial.println(WiFi.softAPgetStationNum());
    
    // BLE Connection
    Serial.println("\nBLE:");
    Serial.print("  Status: ");
    Serial.println(bleConnected ? "CONNECTED" : "DISCONNECTED");
    if (selectedWheelMAC.length() > 0) {
        Serial.print("  Selected Wheel: ");
        Serial.println(selectedWheelMAC);
    }
    Serial.print("  Auto-connect: ");
    Serial.println(autoConnectEnabled ? "ON" : "OFF");
    Serial.print("  Auto-reconnect: ");
    Serial.println(autoReconnectEnabled ? "ON" : "OFF");
    Serial.print("  Discovered Wheels: ");
    Serial.println(discoveredWheelCount);
    
    // Joystick State
    Serial.println("\nJoystick:");
    Serial.print("  X: ");
    Serial.print(joystick.x, 3);
    Serial.print(", Y: ");
    Serial.println(joystick.y, 3);
    Serial.print("  Active: ");
    Serial.println(joystick.active ? "YES" : "NO");
    
    if (joystick.active) {
        int leftSpeed, rightSpeed;
        joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
        Serial.print("  Wheel Speeds - L: ");
        Serial.print(leftSpeed);
        Serial.print(", R: ");
        Serial.println(rightSpeed);
    }
    
    // System
    Serial.println("\nSystem:");
    Serial.print("  Debug Mode: ");
    Serial.println(debugMode ? "ON" : "OFF");
    Serial.print("  Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    Serial.print("  Free Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    
    Serial.println("============================\n");
}

inline void printKey() {
    Serial.println("\n=== Encryption Key ===");
    Serial.print("Key (hex): ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02X ", encryptionKey[i]);
        if (i == 7) Serial.print(" ");
    }
    Serial.println();
    Serial.println("======================\n");
}

inline void handleContinuousMonitoring() {
    if (!continuousJoystickMonitor) return;
    
    unsigned long now = millis();
    
    // Check for timeout (5 seconds of no serial activity)
    if (now - lastSerialActivity > SERIAL_TIMEOUT) {
        continuousJoystickMonitor = false;
        Serial.println("\n[Debug] Monitoring stopped (timeout)");
        return;
    }
    
    // Print joystick info at regular intervals
    if (now - lastJoystickPrint >= JOYSTICK_MONITOR_INTERVAL) {
        lastJoystickPrint = now;
        
        Serial.print("[Joy] X:");
        Serial.print(joystick.x, 2);
        Serial.print(" Y:");
        Serial.print(joystick.y, 2);
        Serial.print(" Active:");
        Serial.print(joystick.active ? "Y" : "N");
        
        if (joystick.active) {
            int leftSpeed, rightSpeed;
            joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
            Serial.print(" L:");
            Serial.print(leftSpeed);
            Serial.print(" R:");
            Serial.print(rightSpeed);
        }
        Serial.println();
    }
}

#endif
