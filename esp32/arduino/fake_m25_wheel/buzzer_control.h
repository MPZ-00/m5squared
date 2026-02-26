/**
 * Buzzer Control
 * 
 * Audio feedback using passive and active buzzers
 */

#ifndef BUZZER_CONTROL_H
#define BUZZER_CONTROL_H

#include <Arduino.h>
#include "device_config.h"

// Buzzer pins (from device_config.h or defaults)
#ifndef BUZZER_PASSIVE
#define BUZZER_PASSIVE 23   // Passive buzzer (PWM capable pin)
#endif

#ifndef BUZZER_ACTIVE
#define BUZZER_ACTIVE 22    // Active buzzer
#endif

// Audio feedback state
static bool audioFeedbackEnabled = true;

/**
 * Initialize buzzer hardware
 */
static bool initBuzzers() {
    // Initialize active buzzer
    pinMode(BUZZER_ACTIVE, OUTPUT);
    digitalWrite(BUZZER_ACTIVE, LOW);
    
    // Initialize passive buzzer with LEDC
    double freq = ledcAttach(BUZZER_PASSIVE, 2000, 8);  // Pin, 2kHz frequency, 8-bit resolution
    if (freq == 0) {
        Serial.println("ERROR: Failed to initialize passive buzzer");
        return false;
    } else {
        Serial.printf("Passive buzzer initialized at %.2f Hz\n", freq);
        return true;
    }
}

/**
 * Play tone on passive buzzer
 */
static void playTone(uint16_t frequency, uint16_t duration) {
    if (!audioFeedbackEnabled || frequency == 0) {
        if (duration > 0) delay(duration);
        return;
    }
    
    // Set frequency and write 50% duty cycle (128 out of 255)
    double actualFreq = ledcWriteTone(BUZZER_PASSIVE, frequency);
    if (actualFreq > 0) {
        ledcWrite(BUZZER_PASSIVE, 128);  // 50% duty cycle
    }
    
    if (duration > 0) {
        delay(duration);
        ledcWrite(BUZZER_PASSIVE, 0);  // Stop tone
    }
}

/**
 * Play beep(s) on active buzzer
 */
static void playBeep(uint8_t count) {
    if (!audioFeedbackEnabled) return;
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(BUZZER_ACTIVE, HIGH);
        delay(100);
        digitalWrite(BUZZER_ACTIVE, LOW);
        if (i < count - 1) delay(100);
    }
}

/**
 * Stop all buzzer output
 */
static void stopBuzzers() {
    digitalWrite(BUZZER_ACTIVE, LOW);
    ledcWrite(BUZZER_PASSIVE, 0);
}

/**
 * Test buzzers at startup
 */
static void testBuzzers() {
    Serial.println("Testing buzzers...");
    playBeep(2);  // Active buzzer test
    delay(200);
    Serial.println("Testing passive buzzer (1 kHz tone, 500ms)...");
    playTone(1000, 500);  // Passive buzzer test
    Serial.println("Buzzer tests complete\n");
}

#endif // BUZZER_CONTROL_H
