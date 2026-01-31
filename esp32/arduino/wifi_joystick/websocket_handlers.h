#ifndef WEBSOCKET_HANDLERS_H
#define WEBSOCKET_HANDLERS_H

// Declared in main sketch
extern JoystickState joystick;
extern bool bleConnected;
extern int discoveredWheelCount;
extern DiscoveredWheel discoveredWheels[];
extern bool bleScanning;
extern WebSocketsServer webSocket;

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

#endif
