#ifndef JOYSTICK_INPUT_H
#define JOYSTICK_INPUT_H

// Declared in main sketch
extern PhysicalJoystickState physicalJoystick;
extern bool usePhysicalJoystick;

// ADC pin mappings from device_config
// JOYSTICK_VRX_PIN, JOYSTICK_VRY_PIN, JOYSTICK_SW_PIN, JOYSTICK_EXTRA_PIN

// ============== Joystick Input Functions ==============

// Initialize ADC and joystick pins
void initializeJoystick() {
    // Configure ADC for joystick analog pins
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation((adc_attenuation_t)ADC_ATTENUATION);
    
    // Configure input pins
    pinMode(JOYSTICK_VRX_PIN, INPUT);
    pinMode(JOYSTICK_VRY_PIN, INPUT);
    pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);  // Pull-up for button
    pinMode(JOYSTICK_EXTRA_PIN, INPUT);
    
    Serial.println("[Joystick] ADC initialized (12-bit, 11dB attenuation)");
}

// Read raw ADC value with averaging
int readADCAverage(int pin, int samples = ADC_SAMPLES) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
        delayMicroseconds(100);  // Small delay between samples
    }
    return sum / samples;
}

// Convert 12-bit ADC value (0-4095) to normalized -1.0 to 1.0
// Accounting for voltage divider output (~0-3.3V for 0-5V input)
float normalizeJoystickAxis(int rawValue) {
    // Expected range: 0-4095 (12-bit ADC)
    // Voltage divider typically gives 50% swing, so:
    // 0V = 0 ADC
    // 1.65V (mid) = 2048 ADC
    // 3.3V = 4095 ADC
    
    // Center is typically around 2048 (+/- some deadzone)
    const int CENTER = 2048;
    const int DEADZONE = 100;  // Ignore small movements near center
    
    int centered = rawValue - CENTER;
    
    // Apply deadzone
    if (abs(centered) < DEADZONE) {
        return 0.0;
    }
    
    // Normalize to -1.0 to 1.0 range
    // Max deviation is 2047 from center in either direction
    float normalized = (float)centered / 2048.0;
    
    // Clamp to valid range
    return constrain(normalized, -1.0, 1.0);
}

// Read physical joystick state from ADC pins
void readPhysicalJoystick(PhysicalJoystickState* state) {
    if (!state) return;
    
    // Read analog axes with averaging
    int rawX = readADCAverage(JOYSTICK_VRX_PIN, ADC_SAMPLES);
    int rawY = readADCAverage(JOYSTICK_VRY_PIN, ADC_SAMPLES);
    int rawExtra = readADCAverage(JOYSTICK_EXTRA_PIN, ADC_SAMPLES);
    
    // Read digital button (inverted due to pull-up)
    int rawButton = digitalRead(JOYSTICK_SW_PIN);
    
    // Normalize analog values to -1.0 to 1.0
    state->x = normalizeJoystickAxis(rawX);
    state->y = normalizeJoystickAxis(rawY);
    state->extra = normalizeJoystickAxis(rawExtra);
    state->button = (rawButton == LOW);  // Button pressed when LOW
    state->lastRead = millis();
    
    if (verboseLogging) {
        Serial.printf("[Joystick] Raw: X=%d Y=%d Extra=%d Button=%d | Norm: X=%.2f Y=%.2f Extra=%.2f Btn=%d\n",
                      rawX, rawY, rawExtra, rawButton,
                      state->x, state->y, state->extra, state->button ? 1 : 0);
    }
}

// Print joystick calibration data for debugging
void printJoystickCalibration() {
    Serial.println("\n[Joystick] Calibration Data:");
    Serial.println("Move joystick through full range and press button...");
    Serial.println("(Reading for 5 seconds, then reset with 'resetcal' command)\n");
    
    int minX = 4095, maxX = 0;
    int minY = 4095, maxY = 0;
    int minExtra = 4095, maxExtra = 0;
    unsigned long endTime = millis() + 5000;
    
    while (millis() < endTime) {
        int x = analogRead(JOYSTICK_VRX_PIN);
        int y = analogRead(JOYSTICK_VRY_PIN);
        int extra = analogRead(JOYSTICK_EXTRA_PIN);
        
        minX = min(minX, x);
        maxX = max(maxX, x);
        minY = min(minY, y);
        maxY = max(maxY, y);
        minExtra = min(minExtra, extra);
        maxExtra = max(maxExtra, extra);
        
        delay(50);
    }
    
    Serial.printf("X-Axis:   Min=%d, Max=%d, Mid=%d, Range=%d\n", 
                  minX, maxX, (minX + maxX) / 2, maxX - minX);
    Serial.printf("Y-Axis:   Min=%d, Max=%d, Mid=%d, Range=%d\n", 
                  minY, maxY, (minY + maxY) / 2, maxY - minY);
    Serial.printf("Extra:    Min=%d, Max=%d, Mid=%d, Range=%d\n", 
                  minExtra, maxExtra, (minExtra + maxExtra) / 2, maxExtra - minExtra);
    Serial.printf("Button:   %s\n", digitalRead(JOYSTICK_SW_PIN) == LOW ? "Pressed" : "Released");
}

// Declared in serial_commands.h, need to enable physical joystick
// Command: 'joystick on' to enable, 'joystick off' to disable

#endif // JOYSTICK_INPUT_H
