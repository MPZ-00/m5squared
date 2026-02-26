/*
 * buzzer.h - Active Buzzer Control
 *
 * Provides audio feedback for user interactions and system events.
 * Uses a single GPIO pin to drive an active buzzer (no PWM frequency needed).
 * Pin definition: device_config.h (BUZZER_PIN)
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include "device_config.h"

// ---------------------------------------------------------------------------
// Sound pattern definitions
// ---------------------------------------------------------------------------
enum BuzzerPattern : uint8_t {
    BUZZ_SILENT       = 0,   // No sound
    BUZZ_BUTTON       = 1,   // Short beep (button press)
    BUZZ_CONFIRM      = 2,   // Double beep (confirmation)
    BUZZ_CONNECTING   = 3,   // Single medium beep (entering CONNECTING)
    BUZZ_READY        = 4,   // Two short beeps (connected, ready)
    BUZZ_OPERATING    = 5,   // Single short high beep (joystick active)
    BUZZ_ERROR        = 6,   // Rapid beeps (error state)
    BUZZ_WARNING      = 7,   // Three medium beeps (warning)
    BUZZ_POWER_ON     = 8,   // Rising tone pattern (power on)
    BUZZ_POWER_OFF    = 9,   // Falling tone pattern (power off)
};

// ---------------------------------------------------------------------------
// Pattern timing structures (on-off sequences in milliseconds)
// ---------------------------------------------------------------------------
struct BuzzerSequence {
    uint16_t onDuration;    // Buzzer ON time (ms)
    uint16_t offDuration;   // Buzzer OFF time (ms)
};

// Maximum pattern steps (adjust if more complex patterns needed)
#define MAX_PATTERN_STEPS 8

struct BuzzerPatternDef {
    uint8_t          stepCount;
    BuzzerSequence   steps[MAX_PATTERN_STEPS];
};

// Pattern definitions
static const BuzzerPatternDef PATTERNS[] = {
    // BUZZ_SILENT
    { 0, {} },
    
    // BUZZ_BUTTON - short beep (50 ms)
    { 1, { {50, 0} } },
    
    // BUZZ_CONFIRM - double beep (50 ms on, 80 ms off, 50 ms on)
    { 2, { {50, 80}, {50, 0} } },
    
    // BUZZ_CONNECTING - single medium beep (150 ms)
    { 1, { {150, 0} } },
    
    // BUZZ_READY - two short beeps (60 ms on, 80 ms off, 60 ms on)
    { 2, { {60, 80}, {60, 0} } },
    
    // BUZZ_OPERATING - single short beep (40 ms)
    { 1, { {40, 0} } },
    
    // BUZZ_ERROR - rapid beeps (100 ms on/off, 3 times)
    { 3, { {100, 100}, {100, 100}, {100, 0} } },
    
    // BUZZ_WARNING - three medium beeps (80 ms on, 120 ms off, repeat)
    { 3, { {80, 120}, {80, 120}, {80, 0} } },
    
    // BUZZ_POWER_ON - rising pattern (50, 100, 150 ms with gaps)
    { 3, { {50, 60}, {100, 60}, {150, 0} } },
    
    // BUZZ_POWER_OFF - falling pattern (150, 100, 50 ms with gaps)
    { 3, { {150, 60}, {100, 60}, {50, 0} } },
};

// ---------------------------------------------------------------------------
// State variables
// ---------------------------------------------------------------------------
static bool     _buzzerActive       = false;  // Currently playing a pattern
static uint8_t  _currentPattern     = BUZZ_SILENT;
static uint8_t  _currentStep        = 0;
static uint32_t _stepStartMs        = 0;
static bool     _buzzerOn           = false;  // Current output state

// ---------------------------------------------------------------------------
// buzzerInit() - Initialize buzzer GPIO
// ---------------------------------------------------------------------------
void buzzerInit() {
#ifdef BUZZER_PIN
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    _buzzerActive = false;
    _currentPattern = BUZZ_SILENT;
    _currentStep = 0;
    _buzzerOn = false;
    Serial.printf("[Buzzer] Initialized on GPIO %d\n", BUZZER_PIN);
#else
    Serial.println("[Buzzer] Not configured (BUZZER_PIN not defined)");
#endif
}

// ---------------------------------------------------------------------------
// buzzerPlay() - Start playing a pattern
// Call this to trigger a sound. Play is non-blocking; buzzerTick() advances.
// ---------------------------------------------------------------------------
void buzzerPlay(BuzzerPattern pattern) {
#ifdef BUZZER_PIN
    if (pattern >= sizeof(PATTERNS) / sizeof(PATTERNS[0])) {
        Serial.printf("[Buzzer] Invalid pattern %d\n", pattern);
        return;
    }
    
    _currentPattern = pattern;
    _currentStep    = 0;
    _stepStartMs    = millis();
    _buzzerActive   = (PATTERNS[pattern].stepCount > 0);
    
    if (_buzzerActive) {
        // Start first step immediately
        digitalWrite(BUZZER_PIN, HIGH);
        _buzzerOn = true;
    }
#endif
}

// ---------------------------------------------------------------------------
// buzzerStop() - Immediately stop current pattern
// ---------------------------------------------------------------------------
void buzzerStop() {
#ifdef BUZZER_PIN
    digitalWrite(BUZZER_PIN, LOW);
    _buzzerActive = false;
    _buzzerOn = false;
#endif
}

// ---------------------------------------------------------------------------
// buzzerTick() - Advance pattern playback (call from main loop)
// ---------------------------------------------------------------------------
void buzzerTick() {
#ifdef BUZZER_PIN
    if (!_buzzerActive) return;
    
    uint32_t now = millis();
    const BuzzerPatternDef& pattern = PATTERNS[_currentPattern];
    
    if (_currentStep >= pattern.stepCount) {
        // Pattern complete
        digitalWrite(BUZZER_PIN, LOW);
        _buzzerActive = false;
        _buzzerOn = false;
        return;
    }
    
    const BuzzerSequence& step = pattern.steps[_currentStep];
    uint32_t elapsed = now - _stepStartMs;
    
    if (_buzzerOn) {
        // Currently in ON phase
        if (elapsed >= step.onDuration) {
            digitalWrite(BUZZER_PIN, LOW);
            _buzzerOn = false;
            _stepStartMs = now;
            
            // If no OFF duration, advance to next step immediately
            if (step.offDuration == 0) {
                _currentStep++;
                if (_currentStep < pattern.stepCount) {
                    digitalWrite(BUZZER_PIN, HIGH);
                    _buzzerOn = true;
                    _stepStartMs = now;
                }
            }
        }
    } else {
        // Currently in OFF phase
        if (elapsed >= step.offDuration) {
            _currentStep++;
            if (_currentStep < pattern.stepCount) {
                digitalWrite(BUZZER_PIN, HIGH);
                _buzzerOn = true;
                _stepStartMs = now;
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// buzzerIsActive() - Check if a pattern is currently playing
// ---------------------------------------------------------------------------
bool buzzerIsActive() {
#ifdef BUZZER_PIN
    return _buzzerActive;
#else
    return false;
#endif
}

#endif // BUZZER_H
