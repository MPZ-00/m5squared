#ifndef WHEEL_COMMAND_H
#define WHEEL_COMMAND_H

// Declared in main sketch
extern bool bleConnected;
extern bool verboseLogging;
extern BLERemoteCharacteristic* pRxCharacteristic;

// Declared in encryption module
bool encryptPacket(uint8_t* data, size_t len, uint8_t* output);

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
        if (verboseLogging) {
            Serial.printf("[BLE] Sent: L=%d, R=%d\n", leftSpeed, rightSpeed);
        }
    } else {
        Serial.println("[BLE] Encryption failed!");
    }
}

#endif
