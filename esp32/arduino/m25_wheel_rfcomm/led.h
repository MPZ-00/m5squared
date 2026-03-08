/**
 * led.h - LED visual feedback.
 *
 * One function per task:
 *   led_init()              - configure pins as outputs, run startup test
 *   led_show_battery(level) - light RED / YELLOW / GREEN based on charge
 *   led_set_connecting()    - fast-blink WHITE: looking for connection
 *   led_set_connected()     - solid WHITE: client connected
 *   led_update()            - drive time-based blink animations; call each loop()
 */

#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// LED mode for the status (white) LED
// ---------------------------------------------------------------------------
typedef enum {
    LED_MODE_ADVERTISING,   // Fast blink
    LED_MODE_CONNECTED,     // Solid
} LedMode;

static LedMode         _ledMode          = LED_MODE_ADVERTISING;
static unsigned long   _ledLastToggle    = 0;
static bool            _ledWhiteState    = false;
static bool            _ledSpeedEnabled  = true;   // CLI toggles this

// ---------------------------------------------------------------------------
// led_init - initialise all LED pins and run a brief startup test.
// ---------------------------------------------------------------------------
inline void led_init() {
    const int pins[] = {
        PIN_LED_RED, PIN_LED_YELLOW, PIN_LED_GREEN,
        PIN_LED_WHITE, PIN_LED_BLUE
    };
    for (int p : pins) {
        pinMode(p, OUTPUT);
        digitalWrite(p, HIGH);
    }
    delay(400);
    for (int p : pins) digitalWrite(p, LOW);
}

// ---------------------------------------------------------------------------
// led_show_battery - light the appropriate battery indicator LED.
// ---------------------------------------------------------------------------
inline void led_show_battery(int level) {
    digitalWrite(PIN_LED_RED,    LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN,  LOW);

    if      (level > 66) digitalWrite(PIN_LED_GREEN,  HIGH);
    else if (level > 33) digitalWrite(PIN_LED_YELLOW, HIGH);
    else                 digitalWrite(PIN_LED_RED,    HIGH);
}

// ---------------------------------------------------------------------------
// led_set_advertising - switch to fast-blink mode (searching for client).
// ---------------------------------------------------------------------------
inline void led_set_advertising() {
    _ledMode = LED_MODE_ADVERTISING;
    digitalWrite(PIN_LED_WHITE, LOW);
    _ledWhiteState = false;
}

// ---------------------------------------------------------------------------
// led_set_connected - solid WHITE to indicate active connection.
// ---------------------------------------------------------------------------
inline void led_set_connected() {
    _ledMode = LED_MODE_CONNECTED;
    digitalWrite(PIN_LED_WHITE, HIGH);
    _ledWhiteState = true;
}

// ---------------------------------------------------------------------------
// led_set_speed_indicator - pulse BLUE LED proportional to speed.
//   Call with raw speed; will decide whether to blink and at what rate.
// ---------------------------------------------------------------------------
inline void led_set_speed_indicator(int16_t speed) {
    static unsigned long lastToggle = 0;
    const bool moving = abs(speed) > 5;
    if (!moving) {
        digitalWrite(PIN_LED_BLUE, LOW);
        return;
    }
    // Blink faster as speed increases
    const float   pct      = abs(speed) / 2.5f;       // 0 - 100%
    const uint16_t period  = (uint16_t)(500 - pct * 4); // 500ms -> 100ms
    if (millis() - lastToggle >= period) {
        lastToggle = millis();
        digitalWrite(PIN_LED_BLUE, !digitalRead(PIN_LED_BLUE));
    }
}

// ---------------------------------------------------------------------------
// led_update - drive time-based animations; call once per loop() iteration.
// ---------------------------------------------------------------------------
inline void led_update() {
    if (_ledMode != LED_MODE_ADVERTISING) return;

    // Fast blink: 150 ms on, 150 ms off
    if (millis() - _ledLastToggle >= 150) {
        _ledLastToggle = millis();
        _ledWhiteState = !_ledWhiteState;
        digitalWrite(PIN_LED_WHITE, _ledWhiteState ? HIGH : LOW);
    }
}

#endif // LED_H
