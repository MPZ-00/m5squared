/**
 * safety.h - Safety checks and time-gated housekeeping.
 *
 * One function per task:
 *   safety_cmd_timeout(state) - zero speed if no REMOTE_SPEED received recently
 *   safety_battery_tick(state, interval) - drain battery on a time gate
 *   safety_button_poll(state, advertise_fn) - debounced button handler
 */

#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>
#include "config.h"
#include "state.h"

// ---------------------------------------------------------------------------
// safety_cmd_timeout - auto-stop if REMOTE_SPEED command has not arrived
//   within CMD_TIMEOUT_MS.  Only triggers when speed is non-zero.
//   Returns true if the timeout fired (useful for logging in main sketch).
// ---------------------------------------------------------------------------
inline bool safety_cmd_timeout(WheelState* s) {
    if (!state_cmd_timed_out(s)) return false;
    Serial.println(F("[Safety] Command timeout - auto stop"));
    state_apply_timeout(s);
    return true;
}

// ---------------------------------------------------------------------------
// safety_battery_tick - simulate battery drain on a configurable interval.
//   Call inside a time-gated block:
//
//     static unsigned long last = 0;
//     if (millis() - last > safety_battery_interval(&state)) {
//         last = millis();
//         safety_battery_tick(&state);
//     }
// ---------------------------------------------------------------------------
inline void safety_battery_tick(WheelState* s) {
    state_drain_battery(s);
}

// Return the appropriate drain interval based on current activity
inline unsigned long safety_battery_interval(const WheelState* s) {
    return (abs(s->speed) > 5) ? BATTERY_DRAIN_ACTIVE_MS : BATTERY_DRAIN_IDLE_MS;
}

// ---------------------------------------------------------------------------
// safety_button_poll - check the force-advertise button with debounce.
//   advertise_fn : callback that (re)starts advertising (transport-agnostic)
//   Returns true if the button was pressed this call.
// ---------------------------------------------------------------------------
inline bool safety_button_poll(void (*advertise_fn)()) {
    static int           _lastState    = HIGH;
    static unsigned long _lastDebounce = 0;
    static bool          _handled      = false;   // Only fire once per press

    const int reading = digitalRead(PIN_BUTTON);

    if (reading != _lastState) {
        _lastDebounce = millis();
        _lastState    = reading;
        _handled      = false;
    }

    if (!_handled &&
        reading == LOW &&
        (millis() - _lastDebounce) > BUTTON_DEBOUNCE_MS) {
        _handled = true;
        Serial.println(F("[BTN] Force advertise"));
        if (advertise_fn) advertise_fn();
        return true;
    }
    return false;
}

#endif // SAFETY_H
