/*
 * button.h - Debounced push-button driver
 *
 * All three physical buttons are momentary push buttons wired as:
 *   Button terminal A -> GPIO pin
 *   Button terminal B -> GND
 *   Internal pull-up enabled (INPUT_PULLUP)
 *   Active LOW: GPIO reads LOW when button is pressed
 *
 * The driver exposes "wasPressed()" which returns true exactly once per
 * physical button press (falling-edge detection after debounce window).
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "device_config.h"

// ---------------------------------------------------------------------------
// Single-button state machine
// ---------------------------------------------------------------------------
struct Button {
    uint8_t  pin;
    bool     lastStableState;  // true = released (HIGH)
    bool     currentReading;
    uint32_t readingChangeTime;
    bool     pressedEvent;     // consumed by wasPressed()

    // Call once in setup()
    void init() {
        pinMode(pin, INPUT_PULLUP);
        delay(10);  // Allow pull-up to settle
        
        // Read actual initial state instead of assuming released
        bool initialState = (digitalRead(pin) == HIGH);
        lastStableState    = initialState;
        currentReading     = initialState;
        readingChangeTime  = 0;
        pressedEvent       = false;
    }

    // Call every loop iteration; updates internal state
    void tick() {
        bool reading = (digitalRead(pin) == HIGH); // HIGH = released
        uint32_t now = millis();

        if (reading != currentReading) {
            // Input changed - restart debounce window
            currentReading    = reading;
            readingChangeTime = now;
        }

        if ((now - readingChangeTime) >= DEBOUNCE_MS) {
            // Stable long enough - check for press event
            if (currentReading != lastStableState) {
                lastStableState = currentReading;
                if (!lastStableState) {
                    // Transitioned to LOW (pressed)
                    pressedEvent = true;
                }
            }
        }
    }

    // Returns true once per button press; clears the flag
    bool wasPressed() {
        if (pressedEvent) {
            pressedEvent = false;
            return true;
        }
        return false;
    }

    // Current debounced state: true = button is being held down
    bool isHeld() const {
        return !lastStableState;
    }
};

// ---------------------------------------------------------------------------
// Button instances (global, accessed from main sketch)
// ---------------------------------------------------------------------------
Button btnEstop     = { BTN_ESTOP_PIN };
Button btnHillHold  = { BTN_HILL_HOLD_PIN };
Button btnAssist    = { BTN_ASSIST_PIN };
Button btnPower     = { BTN_POWER_PIN };

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------
inline void buttonsInit() {
    btnEstop.init();
    btnHillHold.init();
    btnAssist.init();
    btnPower.init();
    
    // Diagnostic: check if all buttons are stuck LOW (wiring problem)
    delay(50);  // Extra settle time
    int e = digitalRead(BTN_ESTOP_PIN);
    int h = digitalRead(BTN_HILL_HOLD_PIN);
    int a = digitalRead(BTN_ASSIST_PIN);
    int p = digitalRead(BTN_POWER_PIN);
    
    if (e == LOW && h == LOW && a == LOW && p == LOW) {
        Serial.println("[Button] WARNING: All buttons read LOW at startup!");
        Serial.println("[Button] Check wiring:");
        Serial.println("[Button]   - Pins 14,25,26 should connect to button terminals");
        Serial.println("[Button]   - Other button terminals should connect to GND");
        Serial.println("[Button]   - Use normally-open (NO) buttons, not normally-closed (NC)");
        Serial.println("[Button]   - Check for shorts to GND on breadboard");
    }
}

inline void buttonsTick() {
    btnEstop.tick();
    btnHillHold.tick();
    btnAssist.tick();
    btnPower.tick();
}

// Debug: print raw button pin states (call from serial command)
inline void buttonsPrintDebug() {
    int estop = digitalRead(BTN_ESTOP_PIN);
    int hill  = digitalRead(BTN_HILL_HOLD_PIN);
    int assist = digitalRead(BTN_ASSIST_PIN);
    int power = digitalRead(BTN_POWER_PIN);
    Serial.printf("[Button-Debug] RAW pins: E-Stop=%d  HillHold=%d  Assist=%d  Power=%d  (LOW=pressed)\n",
                  estop, hill, assist, power);
    Serial.printf("[Button-Debug] Debounced: E-Stop=%s  HillHold=%s  Assist=%s  Power=%s\n",
                  btnEstop.isHeld() ? "HELD" : "released",
                  btnHillHold.isHeld() ? "HELD" : "released",
                  btnAssist.isHeld() ? "HELD" : "released",
                  btnPower.isHeld() ? "HELD" : "released");
}

#endif // BUTTON_H
