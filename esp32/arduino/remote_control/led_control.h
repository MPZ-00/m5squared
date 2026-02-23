/*
 * led_control.h - PWM LED control with configurable blink patterns
 *
 * All four LEDs are driven via the ESP32 LEDC peripheral (hardware PWM).
 * Blinking is managed in software by ledTick(), which must be called every
 * loop iteration.
 *
 * LED mapping and behaviour (from spec):
 *
 *  Status LED (Red, GPIO 16)
 *    OFF          : remote control is off
 *    ON solid     : remote control on, no error
 *    FAST (2 Hz)  : error, timeout, or E-stop activated
 *
 *  Battery LED (Red, GPIO 17)
 *    OFF          : battery >= 50 %
 *    ON solid     : battery < 50 %
 *    SLOW (1 Hz)  : battery < 30 %
 *    FAST (2 Hz)  : battery < 15 %
 *
 *  Hill Hold LED (Yellow, GPIO 18)
 *    OFF          : hill hold inactive
 *    ON solid     : hill hold active
 *
 *  Assist LED (Green, GPIO 19)
 *    OFF          : indoor mode  (max 6 km/h)
 *    ON solid     : outdoor mode (max 8 km/h)
 *    SLOW (1 Hz)  : learning mode (max 3 km/h)
 *
 *  BLE LED (White, GPIO 27)
 *    OFF          : not initialised
 *    SLOW (1 Hz)  : connecting / searching for wheels
 *    ON solid     : both wheels connected
 */

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>
#include "device_config.h"

// ---------------------------------------------------------------------------
// LED mode enum
// ---------------------------------------------------------------------------
enum LedMode : uint8_t {
    LED_OFF       = 0,
    LED_ON        = 1,
    LED_BLINK_SLOW = 2,   // 1 Hz, 1000 ms period
    LED_BLINK_FAST = 3    // 2 Hz,  500 ms period
};

// ---------------------------------------------------------------------------
// Per-LED runtime state
// ---------------------------------------------------------------------------
struct LedState {
    uint8_t  pin;        // GPIO pin (used by new LEDC API)
    LedMode  mode;
    uint32_t lastToggle; // millis() of last blink toggle
    bool     blinkPhase; // true = ON phase
};

// ---------------------------------------------------------------------------
// Internal state for each LED
// ---------------------------------------------------------------------------
static LedState _ledStatus   = { LED_STATUS_PIN,    LED_OFF, 0, false };
static LedState _ledBattery  = { LED_BATTERY_PIN,   LED_OFF, 0, false };
static LedState _ledHillHold = { LED_HILL_HOLD_PIN, LED_OFF, 0, false };
static LedState _ledAssist   = { LED_ASSIST_PIN,    LED_OFF, 0, false };
static LedState _ledBle      = { LED_BLE_PIN,       LED_OFF, 0, false };

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void _ledSetDuty(uint8_t pin, uint8_t duty) {
    ledcWrite(pin, duty);
}

static void _ledUpdate(LedState &led) {
    uint32_t now = millis();
    switch (led.mode) {
        case LED_OFF:
            _ledSetDuty(led.pin, LED_DUTY_OFF);
            break;
        case LED_ON:
            _ledSetDuty(led.pin, LED_DUTY_ON);
            break;
        case LED_BLINK_SLOW: {
            uint32_t half = BLINK_SLOW_MS / 2;
            if (now - led.lastToggle >= half) {
                led.blinkPhase  = !led.blinkPhase;
                led.lastToggle  = now;
                _ledSetDuty(led.pin, led.blinkPhase ? LED_DUTY_ON : LED_DUTY_OFF);
            }
            break;
        }
        case LED_BLINK_FAST: {
            uint32_t half = BLINK_FAST_MS / 2;
            if (now - led.lastToggle >= half) {
                led.blinkPhase  = !led.blinkPhase;
                led.lastToggle  = now;
                _ledSetDuty(led.pin, led.blinkPhase ? LED_DUTY_ON : LED_DUTY_OFF);
            }
            break;
        }
    }
}

static void _ledSetMode(LedState &led, LedMode newMode) {
    if (led.mode != newMode) {
        led.mode        = newMode;
        led.lastToggle  = millis();
        led.blinkPhase  = true;
        // Apply immediately so there is no one-loop delay
        if (newMode == LED_OFF) {
            _ledSetDuty(led.pin, LED_DUTY_OFF);
        } else {
            _ledSetDuty(led.pin, LED_DUTY_ON);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API - Initialization
// ---------------------------------------------------------------------------
inline void ledInit() {
    // Attach pins and configure PWM (ESP32 Arduino core v3.x API)
    ledcAttach(LED_STATUS_PIN,    LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(LED_BATTERY_PIN,   LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(LED_HILL_HOLD_PIN, LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(LED_ASSIST_PIN,    LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(LED_BLE_PIN,       LEDC_FREQ_HZ, LEDC_RESOLUTION);

    // Start with everything off
    _ledSetDuty(LED_STATUS_PIN,    LED_DUTY_OFF);
    _ledSetDuty(LED_BATTERY_PIN,   LED_DUTY_OFF);
    _ledSetDuty(LED_HILL_HOLD_PIN, LED_DUTY_OFF);
    _ledSetDuty(LED_ASSIST_PIN,    LED_DUTY_OFF);
    _ledSetDuty(LED_BLE_PIN,       LED_DUTY_OFF);
}

// ---------------------------------------------------------------------------
// Public API - Mode setters
// ---------------------------------------------------------------------------

// Status LED: solid on = normal, fast blink = error/estop
inline void ledSetStatus(LedMode mode) {
    _ledSetMode(_ledStatus, mode);
}

// Battery LED: off >= 50 %, solid < 50 %, slow < 30 %, fast < 15 %
inline void ledSetBattery(int batteryPct) {
    LedMode mode;
    if (batteryPct >= BATT_HALF_PCT) {
        mode = LED_OFF;
    } else if (batteryPct >= BATT_WARN_LOW_PCT) {
        mode = LED_ON;
    } else if (batteryPct >= BATT_WARN_CRITICAL_PCT) {
        mode = LED_BLINK_SLOW;
    } else {
        mode = LED_BLINK_FAST;
    }
    _ledSetMode(_ledBattery, mode);
}

// Hill hold LED: solid = active, off = inactive
inline void ledSetHillHold(bool active) {
    _ledSetMode(_ledHillHold, active ? LED_ON : LED_OFF);
}

// Assist LED: off = indoor, solid = outdoor, slow blink = learning
inline void ledSetAssistLevel(uint8_t level) {
    LedMode mode;
    switch (level) {
        case ASSIST_INDOOR:   mode = LED_OFF;         break;
        case ASSIST_OUTDOOR:  mode = LED_ON;          break;
        case ASSIST_LEARNING: mode = LED_BLINK_SLOW;  break;
        default:              mode = LED_OFF;         break;
    }
    _ledSetMode(_ledAssist, mode);
}

// BLE LED: slow blink = connecting/searching, solid = connected
inline void ledSetBle(bool connected) {
    _ledSetMode(_ledBle, connected ? LED_ON : LED_BLINK_SLOW);
}

// ---------------------------------------------------------------------------
// Public API - Tick (call every loop iteration)
// ---------------------------------------------------------------------------
inline void ledTick() {
    _ledUpdate(_ledStatus);
    _ledUpdate(_ledBattery);
    _ledUpdate(_ledHillHold);
    _ledUpdate(_ledAssist);
    _ledUpdate(_ledBle);
}

// ---------------------------------------------------------------------------
// Startup self-test: flash all LEDs once in sequence
// ---------------------------------------------------------------------------
inline void ledStartupTest() {
    uint8_t channels[] = {
        LEDC_CH_STATUS, LEDC_CH_BATTERY,
        LEDC_CH_HILL_HOLD, LEDC_CH_ASSIST, LEDC_CH_BLE
    };
    for (uint8_t ch : channels) {
        _ledSetDuty(ch, LED_DUTY_ON);
        delay(120);
        _ledSetDuty(ch, LED_DUTY_OFF);
        delay(60);
    }
}

#endif // LED_CONTROL_H
