/**
 * state.h - Simulated wheel state.
 *
 * Pure data + helpers that operate only on that data.
 * No hardware access, no Serial prints for production paths.
 * Printing functions exist solely for the CLI / debug output.
 */

#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Wheel state struct - single source of truth for everything the wheel tracks
// ---------------------------------------------------------------------------
struct WheelState {
    int16_t  speed;          // Current speed in raw units (-32768 .. +32767)
    int16_t  lastSpeed;      // Previous speed (direction-change detection)
    int      battery;        // Battery percentage 0-100
    uint8_t  assistLevel;    // Assist level 0-2
    bool     hillHold;       // Hill-hold active
    uint8_t  driveProfile;   // Drive profile 0-5
    int16_t  cruiseSpeed;    // Cruise control threshold (raw units)
    uint8_t  autoShutoffMin; // Idle auto-shutoff timeout in minutes
    long     rotations;          // Total simulated wheel rotations
    float    distance;           // Approx distance in metres (2 m/rotation)
    unsigned long lastCmdMs;     // millis() of last received REMOTE_SPEED command
    unsigned long lastSpeedUpdate; // millis() of last rotation simulation tick
};

// ---------------------------------------------------------------------------
// Initialise wheel state to safe defaults
// ---------------------------------------------------------------------------
inline void state_init(WheelState* s) {
    s->speed          = 0;
    s->lastSpeed      = 0;
    s->battery        = 100;
    s->assistLevel    = 1;
    s->hillHold       = false;
    s->driveProfile   = 0;
    s->cruiseSpeed    = CRUISE_SPEED_DEFAULT;
    s->autoShutoffMin = AUTO_SHUTOFF_MIN_DEFAULT;
    s->rotations        = 0;
    s->distance         = 0.0f;
    s->lastCmdMs        = 0;
    s->lastSpeedUpdate  = 0;
}

// ---------------------------------------------------------------------------
// Simulate wheel rotation - updates rotations + distance + drains battery
// ---------------------------------------------------------------------------
inline void state_simulate_rotation(WheelState* s, int count) {
    s->rotations += count;
    s->distance  += count * 2.0f;   // ~2 m per rotation

    // Small battery drain every 100 rotations
    if (s->battery > 0 && (s->rotations % 100) < count) {
        s->battery--;
    }
}

// ---------------------------------------------------------------------------
// Simulate one battery drain tick; call from a time-gated block in loop()
// ---------------------------------------------------------------------------
inline void state_drain_battery(WheelState* s) {
    if (s->battery > 5) {
        s->battery -= 2;
    } else if (s->battery > 0) {
        s->battery--;
    }
}

// ---------------------------------------------------------------------------
// Check whether the command watchdog has expired
// ---------------------------------------------------------------------------
inline bool state_cmd_timed_out(const WheelState* s) {
    if (s->speed == 0 || s->lastCmdMs == 0) return false;
    return (millis() - s->lastCmdMs) > CMD_TIMEOUT_MS;
}

// ---------------------------------------------------------------------------
// Apply command timeout: zero speed
// ---------------------------------------------------------------------------
inline void state_apply_timeout(WheelState* s) {
    s->speed     = 0;
    s->lastCmdMs = 0;
}

// ---------------------------------------------------------------------------
// Debug print - only call from CLI code
// ---------------------------------------------------------------------------
inline void state_print(const WheelState* s) {
    Serial.println(F("\n=== Wheel State ==="));
    Serial.printf("  Speed:         %d raw (%.1f%%)\n", s->speed, s->speed / 2.5f);
    Serial.printf("  Battery:       %d%%\n",      s->battery);
    Serial.printf("  Assist level:  %d\n",        s->assistLevel);
    Serial.printf("  Drive profile: %d\n",        s->driveProfile);
    Serial.printf("  Hill hold:     %s\n",        s->hillHold ? "ON" : "OFF");
    Serial.printf("  Cruise speed:  %d raw\n",    s->cruiseSpeed);
    Serial.printf("  Auto shutoff:  %d min\n",    s->autoShutoffMin);
    Serial.printf("  Rotations:     %ld\n",       s->rotations);
    Serial.printf("  Distance:      %.1f m\n",    s->distance);
    Serial.println(F("===================\n"));
}

#endif // STATE_H
