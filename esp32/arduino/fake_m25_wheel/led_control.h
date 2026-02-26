/**
 * LED Control
 * 
 * Visual feedback using status and battery LEDs
 */

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>
#include "device_config.h"
#include "wheel_state.h"

// LED pins (from device_config.h or defaults)
#ifndef LED_RED
#define LED_RED 25          // Low battery indicator
#endif

#ifndef LED_YELLOW
#define LED_YELLOW 26       // Medium battery indicator
#endif

#ifndef LED_GREEN
#define LED_GREEN 27        // High battery indicator
#endif

#ifndef LED_WHITE
#define LED_WHITE 32        // Connection status
#endif

#ifndef LED_BLUE
#define LED_BLUE 14         // Speed/direction indicator
#endif

// LED states
enum LEDState {
    LED_ADVERTISING,    // White blinking fast
    LED_CONNECTING,     // White blinking slow
    LED_CONNECTED,      // White solid
    LED_ERROR,          // Blue blinking slow
    LED_BATTERY         // Show battery level
};

// LED control state
static LEDState currentLEDState = LED_ADVERTISING;
static bool visualFeedbackEnabled = true;

/**
 * Initialize LED hardware
 */
static void initLEDs() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_WHITE, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    
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
}

/**
 * Show battery level using colored LEDs
 */
static void showBatteryLevel(int batteryLevel) {
    // Debug output
    Serial.print("showBatteryLevel() called - Battery: " + String(batteryLevel) + "% -> ");
    
    // Turn all battery LEDs off first
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    
    if (batteryLevel > 66) {
        digitalWrite(LED_GREEN, HIGH);
        Serial.println("Green LED (high)");
    } else if (batteryLevel > 33) {
        digitalWrite(LED_YELLOW, HIGH);
        Serial.println("Yellow LED (medium)");
    } else {
        digitalWrite(LED_RED, HIGH);
        Serial.println("Red LED (low)");
    }
}

/**
 * Set LED state for connection status
 */
static void setLEDState(LEDState state, int batteryLevel = 0) {
    currentLEDState = state;
    
    // Turn white LED off (battery LEDs controlled separately)
    digitalWrite(LED_WHITE, LOW);
    
    switch (state) {
        case LED_ADVERTISING:
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
            Serial.println("LED: Error (blue blinking slow)");
            break;
            
        case LED_BATTERY:
            showBatteryLevel(batteryLevel);
            break;
    }
}

/**
 * Update LED blinking states (call in loop)
 */
static void updateLEDs() {
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    unsigned long currentMillis = millis();
    
    // Handle blinking states
    if (currentLEDState == LED_ERROR) {
        // Slow blink (500ms)
        if (currentMillis - lastBlink > 500) {
            blinkState = !blinkState;
            digitalWrite(LED_BLUE, blinkState ? HIGH : LOW);
            lastBlink = currentMillis;
        }
    } else if (currentLEDState == LED_ADVERTISING) {
        // Fast blink (200ms)
        if (currentMillis - lastBlink > 200) {
            blinkState = !blinkState;
            digitalWrite(LED_WHITE, blinkState ? HIGH : LOW);
            lastBlink = currentMillis;
        }
    }
}

/**
 * Update speed indicator LED based on wheel speed
 */
static void updateSpeedIndicator(const WheelState& wheel) {
    if (!visualFeedbackEnabled) return;
    
    // Calculate speed percentage and direction
    float speedPercent = abs(wheel.currentSpeed) / 2.5;  // M25_SPEED_SCALE = 2.5
    bool moving = abs(wheel.currentSpeed) > 5;  // Deadzone threshold
    
    // Blue LED: Speed intensity (blink rate increases with speed)
    if (moving) {
        static unsigned long lastBlink = 0;
        unsigned long interval = 1000 - min(900UL, (unsigned long)(speedPercent * 10));
        if (millis() - lastBlink > interval) {
            digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
            lastBlink = millis();
        }
    } else {
        digitalWrite(LED_BLUE, LOW);
    }
}

#endif // LED_CONTROL_H
