/*
 * WiFi Virtual Joystick Controller for M25 Wheels
 * 
 * Creates a WiFi access point that serves a web-based virtual joystick.
 * Connect with your phone/tablet to control the wheelchair.
 * 
 * Features:
 * - WiFi AP mode (no router needed)
 * - HTML5 touch joystick interface
 * - Real-time WebSocket communication
 * - Connects to M25 wheels via BLE
 * 
 * Setup:
 * 1. Upload this sketch to ESP32
 * 2. ESP32 creates WiFi network "M25-Controller"
 * 3. Connect phone to "M25-Controller" (password: "m25wheel")
 * 4. Open browser to http://192.168.4.1
 * 5. Use touchscreen joystick to control
 * 
 * Hardware: ESP32-WROOM-32 or similar
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <BLEClient.h>

// WiFi AP Configuration
const char* ssid = "M25-Controller";
const char* password = "m25wheel";  // Change this for security

// Web server on port 80
WebServer server(80);

// WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

// BLE Configuration for M25 wheels
#define SERVICE_UUID "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_TX "00001101-0000-1000-8000-00805F9B34FB"
#define CHAR_UUID_RX "00001102-0000-1000-8000-00805F9B34FB"

// Joystick state
struct JoystickState {
    float x;  // -1.0 to 1.0 (left to right)
    float y;  // -1.0 to 1.0 (down to up)
    bool active;  // Deadman switch
} joystick = {0.0, 0.0, false};

// BLE connection state
bool bleConnected = false;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pTxCharacteristic = nullptr;
BLERemoteCharacteristic* pRxCharacteristic = nullptr;

// Timing
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_INTERVAL = 50;  // 20Hz update rate

// HTML page with virtual joystick
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>M25 Virtual Joystick</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            touch-action: none;
        }
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            padding: 20px;
        }
        h1 {
            font-size: 24px;
            margin-bottom: 10px;
        }
        .status {
            margin-bottom: 20px;
            padding: 10px 20px;
            background: rgba(255,255,255,0.2);
            border-radius: 20px;
            font-size: 14px;
        }
        .status.connected {
            background: rgba(76, 175, 80, 0.8);
        }
        .status.disconnected {
            background: rgba(244, 67, 54, 0.8);
        }
        .joystick-container {
            position: relative;
            width: 300px;
            height: 300px;
            background: rgba(255,255,255,0.1);
            border: 3px solid rgba(255,255,255,0.3);
            border-radius: 50%;
            margin: 20px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.3);
        }
        .joystick-base {
            position: absolute;
            top: 50%;
            left: 50%;
            width: 80%;
            height: 80%;
            transform: translate(-50%, -50%);
            border: 2px dashed rgba(255,255,255,0.3);
            border-radius: 50%;
        }
        .joystick-stick {
            position: absolute;
            top: 50%;
            left: 50%;
            width: 80px;
            height: 80px;
            background: radial-gradient(circle, #ffffff, #cccccc);
            border: 3px solid #888;
            border-radius: 50%;
            transform: translate(-50%, -50%);
            box-shadow: 0 5px 20px rgba(0,0,0,0.5);
            cursor: pointer;
            transition: box-shadow 0.1s;
        }
        .joystick-stick.active {
            box-shadow: 0 5px 30px rgba(255,255,255,0.6);
            border-color: #4CAF50;
        }
        .coordinates {
            margin-top: 20px;
            font-size: 16px;
            font-family: monospace;
        }
        .instructions {
            margin-top: 20px;
            text-align: center;
            font-size: 12px;
            opacity: 0.8;
            max-width: 300px;
        }
        .emergency-stop {
            margin-top: 20px;
            padding: 15px 40px;
            font-size: 18px;
            font-weight: bold;
            background: #f44336;
            color: white;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            box-shadow: 0 5px 15px rgba(0,0,0,0.3);
        }
        .emergency-stop:active {
            transform: scale(0.95);
        }
    </style>
</head>
<body>
    <h1>M25 Virtual Joystick</h1>
    <div class="status" id="status">Connecting...</div>
    
    <div class="joystick-container" id="joystickContainer">
        <div class="joystick-base"></div>
        <div class="joystick-stick" id="joystickStick"></div>
    </div>
    
    <div class="coordinates">
        X: <span id="xValue">0.00</span> | Y: <span id="yValue">0.00</span>
    </div>
    
    <button class="emergency-stop" onclick="emergencyStop()">EMERGENCY STOP</button>
    
    <div class="instructions">
        Touch and drag the joystick to control the wheelchair.
        Release to stop. The emergency stop button will immediately halt all movement.
    </div>

    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + ':81');
        const stick = document.getElementById('joystickStick');
        const container = document.getElementById('joystickContainer');
        const xValue = document.getElementById('xValue');
        const yValue = document.getElementById('yValue');
        const status = document.getElementById('status');
        
        let isDragging = false;
        let centerX = 0;
        let centerY = 0;
        let maxRadius = 0;
        
        function updateCenter() {
            const rect = container.getBoundingClientRect();
            centerX = rect.width / 2;
            centerY = rect.height / 2;
            maxRadius = (rect.width / 2) * 0.7;  // 70% of radius
        }
        
        updateCenter();
        window.addEventListener('resize', updateCenter);
        
        // WebSocket events
        ws.onopen = function() {
            status.textContent = 'Connected';
            status.className = 'status connected';
        };
        
        ws.onclose = function() {
            status.textContent = 'Disconnected';
            status.className = 'status disconnected';
        };
        
        ws.onerror = function() {
            status.textContent = 'Connection Error';
            status.className = 'status disconnected';
        };
        
        function sendJoystickData(x, y, active) {
            if (ws.readyState === WebSocket.OPEN) {
                const data = JSON.stringify({
                    x: x.toFixed(3),
                    y: y.toFixed(3),
                    active: active
                });
                ws.send(data);
            }
        }
        
        function updateStickPosition(x, y) {
            // Calculate distance from center
            const dx = x - centerX;
            const dy = y - centerY;
            const distance = Math.sqrt(dx * dx + dy * dy);
            
            // Limit to max radius
            let finalX = dx;
            let finalY = dy;
            if (distance > maxRadius) {
                const angle = Math.atan2(dy, dx);
                finalX = Math.cos(angle) * maxRadius;
                finalY = Math.sin(angle) * maxRadius;
            }
            
            // Update stick position
            stick.style.transform = `translate(calc(-50% + ${finalX}px), calc(-50% + ${finalY}px))`;
            
            // Convert to -1.0 to 1.0 range
            const normalizedX = finalX / maxRadius;
            const normalizedY = -finalY / maxRadius;  // Invert Y (up is positive)
            
            xValue.textContent = normalizedX.toFixed(2);
            yValue.textContent = normalizedY.toFixed(2);
            
            return { x: normalizedX, y: normalizedY };
        }
        
        function resetStick() {
            stick.style.transform = 'translate(-50%, -50%)';
            stick.classList.remove('active');
            xValue.textContent = '0.00';
            yValue.textContent = '0.00';
            sendJoystickData(0, 0, false);
        }
        
        // Touch events
        stick.addEventListener('touchstart', function(e) {
            e.preventDefault();
            isDragging = true;
            stick.classList.add('active');
        });
        
        container.addEventListener('touchmove', function(e) {
            if (!isDragging) return;
            e.preventDefault();
            
            const touch = e.touches[0];
            const rect = container.getBoundingClientRect();
            const x = touch.clientX - rect.left;
            const y = touch.clientY - rect.top;
            
            const pos = updateStickPosition(x, y);
            sendJoystickData(pos.x, pos.y, true);
        });
        
        document.addEventListener('touchend', function(e) {
            if (isDragging) {
                isDragging = false;
                resetStick();
            }
        });
        
        // Mouse events (for desktop testing)
        stick.addEventListener('mousedown', function(e) {
            e.preventDefault();
            isDragging = true;
            stick.classList.add('active');
        });
        
        container.addEventListener('mousemove', function(e) {
            if (!isDragging) return;
            e.preventDefault();
            
            const rect = container.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const y = e.clientY - rect.top;
            
            const pos = updateStickPosition(x, y);
            sendJoystickData(pos.x, pos.y, true);
        });
        
        document.addEventListener('mouseup', function(e) {
            if (isDragging) {
                isDragging = false;
                resetStick();
            }
        });
        
        function emergencyStop() {
            sendJoystickData(0, 0, false);
            resetStick();
            if (confirm('Emergency stop activated! Reconnect to wheels?')) {
                ws.send(JSON.stringify({ command: 'reconnect' }));
            }
        }
    </script>
</body>
</html>
)rawliteral";

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected\n", num);
            joystick.active = false;
            joystick.x = 0;
            joystick.y = 0;
            break;
            
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
            break;
        }
        
        case WStype_TEXT: {
            // Parse JSON: {"x":"0.500","y":"-0.300","active":true}
            String data = String((char*)payload);
            
            // Simple JSON parsing
            int xPos = data.indexOf("\"x\":\"") + 5;
            int xEnd = data.indexOf("\"", xPos);
            int yPos = data.indexOf("\"y\":\"") + 5;
            int yEnd = data.indexOf("\"", yPos);
            int activePos = data.indexOf("\"active\":") + 9;
            
            if (xPos > 4 && yPos > 4) {
                joystick.x = data.substring(xPos, xEnd).toFloat();
                joystick.y = data.substring(yPos, yEnd).toFloat();
                joystick.active = data.substring(activePos, activePos + 4) == "true";
                
                Serial.printf("Joystick: X=%.2f, Y=%.2f, Active=%d\n", 
                    joystick.x, joystick.y, joystick.active);
            }
            break;
        }
    }
}

// Convert joystick to wheel speeds
void joystickToWheelSpeeds(float joyX, float joyY, int& leftSpeed, int& rightSpeed) {
    // Differential drive calculation
    // Forward/back = Y axis, Turn = X axis
    
    float left = joyY + joyX;   // Forward + turn right increases left wheel
    float right = joyY - joyX;  // Forward + turn right decreases right wheel
    
    // Clamp to -1.0 to 1.0
    left = constrain(left, -1.0, 1.0);
    right = constrain(right, -1.0, 1.0);
    
    // Scale to M25 speed range (-100 to 100)
    leftSpeed = (int)(left * 100);
    rightSpeed = (int)(right * 100);
}

// Send command to M25 wheels (placeholder - implement with actual protocol)
void sendWheelCommand(int leftSpeed, int rightSpeed) {
    if (!bleConnected || !pTxCharacteristic) {
        return;
    }
    
    // TODO: Implement actual M25 protocol encoding
    // This is a placeholder - you'll need to integrate m25_protocol.py logic
    
    Serial.printf("Sending to wheels: L=%d, R=%d\n", leftSpeed, rightSpeed);
    
    // Example: Send simple command packet
    uint8_t command[16] = {0};
    command[0] = 0xAA;  // Start marker
    command[1] = leftSpeed & 0xFF;
    command[2] = (leftSpeed >> 8) & 0xFF;
    command[3] = rightSpeed & 0xFF;
    command[4] = (rightSpeed >> 8) & 0xFF;
    
    // NOTE: Real implementation needs encryption!
    // pTxCharacteristic->writeValue(command, 16);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nM25 WiFi Virtual Joystick Controller");
    Serial.println("====================================");
    
    // Setup WiFi Access Point
    Serial.print("Creating WiFi AP: ");
    Serial.println(ssid);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    // Setup web server
    server.on("/", []() {
        server.send(200, "text/html", htmlPage);
    });
    
    server.begin();
    Serial.println("HTTP server started");
    
    // Setup WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("WebSocket server started on port 81");
    
    // TODO: Setup BLE connection to M25 wheels
    // BLEDevice::init("M25-Controller");
    // ... (implement BLE client connection)
    
    Serial.println("\nSetup complete!");
    Serial.println("Connect your phone to WiFi: " + String(ssid));
    Serial.println("Open browser to: http://192.168.4.1");
}

void loop() {
    // Handle web server requests
    server.handleClient();
    
    // Handle WebSocket events
    webSocket.loop();
    
    // Send commands to wheels at fixed rate
    unsigned long currentTime = millis();
    if (currentTime - lastCommandTime >= COMMAND_INTERVAL) {
        lastCommandTime = currentTime;
        
        if (joystick.active) {
            // Convert joystick to wheel speeds
            int leftSpeed, rightSpeed;
            joystickToWheelSpeeds(joystick.x, joystick.y, leftSpeed, rightSpeed);
            
            // Send to wheels
            sendWheelCommand(leftSpeed, rightSpeed);
        } else {
            // Deadman released - send stop command
            sendWheelCommand(0, 0);
        }
    }
    
    delay(1);  // Small delay to prevent watchdog issues
}
