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
#include "Logger.h"

#ifdef NO_JOYSTICK
 // ---------------------------------------------------------------------------
 // Stub implementation: no hardware joystick connected.
 // All reads return a perfectly centered, in-deadzone state.
 // ---------------------------------------------------------------------------
inline void joystickInit() {
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__, "NO_JOYSTICK defined - ADC disabled");
}
inline void joystickRecalibrate() {
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__, "NO_JOYSTICK - recal skipped");
}
inline int  joystickReadRawAxis(uint8_t) { return JOYSTICK_CENTER; }
struct JoystickRaw { int x = JOYSTICK_CENTER; int y = JOYSTICK_CENTER; };
struct JoystickNorm { float x = 0.0f; float y = 0.0f; bool inDeadzone = true; };
inline const char* joystickDirectionLabel(float, float) { return "CENTER"; }
inline JoystickRaw  joystickReadRaw() { return { JOYSTICK_CENTER, JOYSTICK_CENTER }; }
inline JoystickNorm joystickRead() { return { 0.0f, 0.0f, true }; }
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
    analogSetPinAttenuation(JOYSTICK_X_PIN, ADC_11db);
    analogSetPinAttenuation(JOYSTICK_Y_PIN, ADC_11db);
    const int8_t gpioX = digitalPinToGPIONumber(JOYSTICK_X_PIN);
    const int8_t gpioY = digitalPinToGPIONumber(JOYSTICK_Y_PIN);
    const int8_t chX = digitalPinToAnalogChannel(JOYSTICK_X_PIN);
    const int8_t chY = digitalPinToAnalogChannel(JOYSTICK_Y_PIN);
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__,
        "Pin map: X pin %d->gpio %d ch %d, Y pin %d->gpio %d ch %d",
        JOYSTICK_X_PIN, gpioX, chX, JOYSTICK_Y_PIN, gpioY, chY);
    if (chX < 0 || chY < 0) {
        Logger::instance().logForced(LogLevel::ERROR, TAG_JOYSTICK, __FILE__, __LINE__,
            "Configured joystick pins are not ADC-capable on this board variant");
    }

    // Warm-up reads reduce first-sample artifacts after attenuation changes.
    for (int i = 0; i < 4; i++) {
        (void)analogRead(JOYSTICK_X_PIN);
        (void)analogRead(JOYSTICK_Y_PIN);
        delay(2);
    }

    const int bootProbeX = analogRead(JOYSTICK_X_PIN);
    const int bootProbeY = analogRead(JOYSTICK_Y_PIN);
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__,
        "Boot ADC probe: X=%d Y=%d (pins %d/%d)",
        bootProbeX, bootProbeY, JOYSTICK_X_PIN, JOYSTICK_Y_PIN);

    if (bootProbeX == 0 && bootProbeY == 0) {
        Logger::instance().logForced(LogLevel::WARN, TAG_JOYSTICK, __FILE__, __LINE__,
            "Boot probe is hard-zero on both axes - check joystick VCC/GND and signal wiring");
    }

    // Calibrate center by averaging ADC samples taken at rest
    const int calSamples = 64;
    long sumX = 0, sumY = 0;
    int minX = JOYSTICK_ADC_MAX;
    int maxX = JOYSTICK_ADC_MIN;
    int minY = JOYSTICK_ADC_MAX;
    int maxY = JOYSTICK_ADC_MIN;
    for (int i = 0; i < calSamples; i++) {
        int x = analogRead(JOYSTICK_X_PIN);
        int y = analogRead(JOYSTICK_Y_PIN);
        sumX += x;
        sumY += y;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        delay(4);
    }
    _jsXCenter = (int)(sumX / calSamples);
    _jsYCenter = (int)(sumY / calSamples);
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__,
        "Center calibrated: X=%d Y=%d", _jsXCenter, _jsYCenter);
    Logger::instance().logForced(LogLevel::INFO, TAG_JOYSTICK, __FILE__, __LINE__,
        "Boot ADC span: X=%d..%d (d=%d) Y=%d..%d (d=%d)",
        minX, maxX, (maxX - minX), minY, maxY, (maxY - minY));

    const bool xClamped = (maxX >= (JOYSTICK_ADC_MAX - 2)) || (minX <= (JOYSTICK_ADC_MIN + 2));
    const bool yClamped = (maxY >= (JOYSTICK_ADC_MAX - 2)) || (minY <= (JOYSTICK_ADC_MIN + 2));
    if (xClamped || yClamped) {
        Logger::instance().logForced(LogLevel::WARN, TAG_JOYSTICK, __FILE__, __LINE__,
            "ADC near rail during boot calibration (check wiring/3.3V/GND, attenuation, and pin mapping)");
    }
}

// ---------------------------------------------------------------------------
// Low-level: averaged ADC read
// ---------------------------------------------------------------------------
inline int joystickReadRawAxis(uint8_t pin) {
    const int readSamples = 8;
    long sum = 0;
    for (int i = 0; i < readSamples; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / readSamples);
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
        int edgePos = center + JOYSTICK_DEADZONE;
        int rangePos = JOYSTICK_ADC_MAX - edgePos;
        if (rangePos <= 0) return 1.0f;
        usable = (float)(raw - edgePos) / (float)rangePos;
    }
    else {
        // Negative side: ADC_MIN to deadzone edge
        int edgeNeg = center - JOYSTICK_DEADZONE;
        int rangeNeg = edgeNeg - JOYSTICK_ADC_MIN;
        if (rangeNeg <= 0) return -1.0f;
        usable = -((float)(edgeNeg - raw) / (float)rangeNeg);
    }

    // Clamp to [-1, +1] to guard against ADC noise at extremes
    if (usable > 1.0f) usable = 1.0f;
    if (usable < -1.0f) usable = -1.0f;
    return usable;
}

// ---------------------------------------------------------------------------
// Classify normalized joystick vector into a readable direction label.
// ---------------------------------------------------------------------------
inline const char* joystickDirectionLabel(float x, float y) {
    const float primary = 0.55f;
    const float secondary = 0.25f;
    const float ax = (x >= 0.0f) ? x : -x;
    const float ay = (y >= 0.0f) ? y : -y;

    if (x == 0.0f && y == 0.0f) return "CENTER";

    if (y >= primary && ax <= secondary) return "UP";
    if (y <= -primary && ax <= secondary) return "DOWN";
    if (x >= primary && ay <= secondary) return "RIGHT";
    if (x <= -primary && ay <= secondary) return "LEFT";

    if (x >= secondary && y >= secondary) return "UP-RIGHT";
    if (x <= -secondary && y >= secondary) return "UP-LEFT";
    if (x >= secondary && y <= -secondary) return "DOWN-RIGHT";
    if (x <= -secondary && y <= -secondary) return "DOWN-LEFT";

    if (ay >= ax) return (y > 0.0f) ? "UP-ish" : "DOWN-ish";
    return (x > 0.0f) ? "RIGHT-ish" : "LEFT-ish";
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
