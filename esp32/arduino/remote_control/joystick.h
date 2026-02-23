/*
 * joystick.h - Joystick reading, calibration, deadzone, normalization
 *
 * Reads the analog X/Y axes of a 2-axis joystick (e.g. KY-023).
 * Provides normalized values in [-1.0, +1.0] with deadzone applied.
 *
 * Coordinate convention:
 *   X: negative = left,   positive = right
 *   Y: negative = backward, positive = forward
 */

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>
#include "device_config.h"

#ifdef NO_JOYSTICK
// ---------------------------------------------------------------------------
// Stub implementation: no hardware joystick connected.
// All reads return a perfectly centered, in-deadzone state.
// ---------------------------------------------------------------------------
inline void joystickInit()        { Serial.println("[Joystick] NO_JOYSTICK defined - ADC disabled"); }
inline void joystickRecalibrate() { Serial.println("[Joystick] NO_JOYSTICK - recal skipped"); }
inline int  joystickReadRawAxis(uint8_t) { return JOYSTICK_CENTER; }
struct JoystickRaw  { int x = JOYSTICK_CENTER; int y = JOYSTICK_CENTER; };
struct JoystickNorm { float x = 0.0f; float y = 0.0f; bool inDeadzone = true; };
inline JoystickRaw  joystickReadRaw() { return { JOYSTICK_CENTER, JOYSTICK_CENTER }; }
inline JoystickNorm joystickRead()    { return { 0.0f, 0.0f, true }; }
#else // real joystick below

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
struct JoystickRaw {
    int x;   // ADC counts 0-4095
    int y;
};

struct JoystickNorm {
    float x;   // -1.0 (left/backward) ... +1.0 (right/forward)
    float y;
    bool  inDeadzone;  // true if both axes are within deadzone
};

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static int _jsXCenter = JOYSTICK_CENTER;
static int _jsYCenter = JOYSTICK_CENTER;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
inline void joystickInit() {
    analogReadResolution(ADC_RESOLUTION_BITS);
    // Calibrate center by averaging ADC samples taken at rest
    long sumX = 0, sumY = 0;
    for (int i = 0; i < 32; i++) {
        sumX += analogRead(JOYSTICK_X_PIN);
        sumY += analogRead(JOYSTICK_Y_PIN);
        delay(5);
    }
    _jsXCenter = (int)(sumX / 32);
    _jsYCenter = (int)(sumY / 32);
    Serial.printf("[Joystick] Center calibrated: X=%d  Y=%d\n",
                  _jsXCenter, _jsYCenter);
}

// ---------------------------------------------------------------------------
// Low-level: averaged ADC read
// ---------------------------------------------------------------------------
inline int joystickReadRawAxis(uint8_t pin) {
    long sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / ADC_SAMPLES);
}

inline JoystickRaw joystickReadRaw() {
    return { joystickReadRawAxis(JOYSTICK_X_PIN),
             joystickReadRawAxis(JOYSTICK_Y_PIN) };
}

// ---------------------------------------------------------------------------
// Normalize one axis to [-1.0, +1.0], applying deadzone
// Returns 0.0 if within deadzone.
// ---------------------------------------------------------------------------
inline float joystickNormalizeAxis(int raw, int center) {
    int offset = raw - center;

    if (abs(offset) <= JOYSTICK_DEADZONE) {
        return 0.0f;
    }

    // Remap outside deadzone to full [-1, +1] range
    float usable;
    if (offset > 0) {
        // Positive side: deadzone edge to ADC_MAX
        int edgePos    = center + JOYSTICK_DEADZONE;
        int rangePos   = JOYSTICK_ADC_MAX - edgePos;
        if (rangePos <= 0) return 1.0f;
        usable = (float)(raw - edgePos) / (float)rangePos;
    } else {
        // Negative side: ADC_MIN to deadzone edge
        int edgeNeg    = center - JOYSTICK_DEADZONE;
        int rangeNeg   = edgeNeg - JOYSTICK_ADC_MIN;
        if (rangeNeg <= 0) return -1.0f;
        usable = -((float)(edgeNeg - raw) / (float)rangeNeg);
    }

    // Clamp to [-1, +1] to guard against ADC noise at extremes
    if (usable >  1.0f) usable =  1.0f;
    if (usable < -1.0f) usable = -1.0f;
    return usable;
}

// ---------------------------------------------------------------------------
// Main read function - returns normalized joystick state
// ---------------------------------------------------------------------------
inline JoystickNorm joystickRead() {
    JoystickRaw raw = joystickReadRaw();

    JoystickNorm n;
    n.x = joystickNormalizeAxis(raw.x, _jsXCenter);
    n.y = joystickNormalizeAxis(raw.y, _jsYCenter);
    n.inDeadzone = (n.x == 0.0f && n.y == 0.0f);
    return n;
}

// ---------------------------------------------------------------------------
// Re-calibrate center position (call at runtime when joystick is known
// to be at rest, e.g. during the power-on safety check)
// ---------------------------------------------------------------------------
inline void joystickRecalibrate() {
    joystickInit();
}

#endif // !NO_JOYSTICK
#endif // JOYSTICK_H
