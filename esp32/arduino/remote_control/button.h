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
        lastStableState    = true;   // assume released at startup
        currentReading     = true;
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

// ---------------------------------------------------------------------------
// Convenience functions
// ---------------------------------------------------------------------------
inline void buttonsInit() {
    btnEstop.init();
    btnHillHold.init();
    btnAssist.init();
}

inline void buttonsTick() {
    btnEstop.tick();
    btnHillHold.tick();
    btnAssist.tick();
}

#endif // BUTTON_H
