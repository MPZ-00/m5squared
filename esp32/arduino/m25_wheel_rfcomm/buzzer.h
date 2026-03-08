/**
 * buzzer.h - Audio feedback.
 *
 * Two buzzers are supported:
 *   Active buzzer  (PIN_BUZZER_ACTIVE)  - simple on/off, driven digitally.
 *   Passive buzzer (PIN_BUZZER_PASSIVE) - frequency-capable, driven via LEDC PWM.
 *
 * One function per task:
 *   buzzer_init()               - configure pins and LEDC
 *   buzzer_beep(count)          - play count short beeps on the active buzzer
 *   buzzer_tone(freq, durationMs) - play a tone on the passive buzzer
 *   buzzer_stop()               - silence both buzzers immediately
 *   buzzer_test()               - startup self-test sequence
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include "config.h"

static bool _buzzerAudioEnabled = true;

// ---------------------------------------------------------------------------
// buzzer_init - configure buzzer pins and attach LEDC to the passive buzzer.
//   Returns true if LEDC attached successfully.
// ---------------------------------------------------------------------------
inline bool buzzer_init() {
    pinMode(PIN_BUZZER_ACTIVE, OUTPUT);
    digitalWrite(PIN_BUZZER_ACTIVE, LOW);

    const double actualFreq = ledcAttach(PIN_BUZZER_PASSIVE, 2000, 8);
    if (actualFreq == 0.0) {
        Serial.println("[BUZZER] ERROR: passive buzzer LEDC init failed");
        return false;
    }
    ledcWrite(PIN_BUZZER_PASSIVE, 0);   // Silence at start
    return true;
}

// ---------------------------------------------------------------------------
// buzzer_enable / buzzer_disable - runtime audio toggle.
// ---------------------------------------------------------------------------
inline void buzzer_enable()  { _buzzerAudioEnabled = true; }
inline void buzzer_disable() {
    _buzzerAudioEnabled = false;
    digitalWrite(PIN_BUZZER_ACTIVE, LOW);
    ledcWrite(PIN_BUZZER_PASSIVE, 0);
}
inline bool buzzer_is_enabled() { return _buzzerAudioEnabled; }

// ---------------------------------------------------------------------------
// buzzer_beep - play count short beeps on the active buzzer.
// ---------------------------------------------------------------------------
inline void buzzer_beep(uint8_t count) {
    if (!_buzzerAudioEnabled) return;
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(PIN_BUZZER_ACTIVE, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER_ACTIVE, LOW);
        if (i < count - 1) delay(100);
    }
}

// ---------------------------------------------------------------------------
// buzzer_tone - play a tone on the passive buzzer.
//   durationMs == 0 : continuous (caller must call buzzer_stop())
//   durationMs  > 0 : play and then silence automatically (blocking delay)
// ---------------------------------------------------------------------------
inline void buzzer_tone(uint16_t freq, uint16_t durationMs) {
    if (!_buzzerAudioEnabled || freq == 0) {
        if (durationMs > 0) delay(durationMs);
        return;
    }
    ledcWriteTone(PIN_BUZZER_PASSIVE, freq);
    ledcWrite(PIN_BUZZER_PASSIVE, 128);   // 50% duty cycle

    if (durationMs > 0) {
        delay(durationMs);
        ledcWrite(PIN_BUZZER_PASSIVE, 0);
    }
}

// ---------------------------------------------------------------------------
// buzzer_stop - silence both buzzers immediately.
// ---------------------------------------------------------------------------
inline void buzzer_stop() {
    digitalWrite(PIN_BUZZER_ACTIVE, LOW);
    ledcWrite(PIN_BUZZER_PASSIVE, 0);
}

// ---------------------------------------------------------------------------
// buzzer_speed_tone - drive passive buzzer proportional to speed.
//   Call each loop() when connected and moving.
//   Freq range: 200 Hz (slow) -> 2000 Hz (full speed).
// ---------------------------------------------------------------------------
inline void buzzer_speed_tone(int16_t speed) {
    if (!_buzzerAudioEnabled) return;
    const bool moving = abs(speed) > 5;
    if (!moving) {
        buzzer_stop();
        return;
    }
    const float    pct  = abs(speed) / 2.5f;                           // 0-100%
    const uint16_t freq = (uint16_t)constrain(200 + pct * 18, 200, 2000);
    buzzer_tone(freq, 0);   // Continuous
}

// ---------------------------------------------------------------------------
// buzzer_test - startup self-test.
// ---------------------------------------------------------------------------
inline void buzzer_test() {
    buzzer_beep(2);
    delay(200);
    buzzer_tone(1000, 300);
}

#endif // BUZZER_H
