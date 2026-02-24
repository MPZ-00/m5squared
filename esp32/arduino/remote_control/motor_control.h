/*
 * motor_control.h - Joystick to differential wheel-speed mapping
 *
 * Converts normalized joystick values ([-1, +1]) and the current assist level
 * into left/right wheel speed commands ([-100, +100] percent of hardware max).
 *
 * Sign convention:
 *   positive speed -> forward rotation
 *   negative speed -> backward rotation
 *
 * Differential steering rules (from spec):
 *
 *   Quadrant: forward center   -> left = +Vmax,          right = +Vmax
 *   Quadrant: forward right    -> left = +Vmax,          right = +Vmax * (1 - TURN_REDUCTION)
 *   Quadrant: forward left     -> left = +Vmax * (1-TR), right = +Vmax
 *   Quadrant: backward center  -> left = -Vmax_rev,      right = -Vmax_rev
 *   Quadrant: backward right   -> left = -Vmax_rev,      right = -Vmax_rev * (1 - TR)
 *   Quadrant: backward left    -> left = -Vmax_rev*(1-TR), right = -Vmax_rev
 *   Center (in deadzone)       -> left = 0,              right = 0
 *
 * TURN_REDUCTION is defined in device_config.h (default 0.5, i.e. inner wheel
 * gets 50 % of outer wheel speed at full X deflection).
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include "device_config.h"
#include "joystick.h"

// ---------------------------------------------------------------------------
// Assist level configuration table
// ---------------------------------------------------------------------------
struct AssistConfig {
    const char* name;
    int         vmaxForward;   // 0-100 percent
    int         vmaxReverse;   // 0-100 percent (derived from forward * ratio)
};

static const AssistConfig assistConfigs[ASSIST_COUNT] = {
    /* ASSIST_INDOOR   */ { "Indoor",   VMAX_INDOOR,
                            (int)(VMAX_INDOOR  * VMAX_REVERSE_RATIO) },
    /* ASSIST_OUTDOOR  */ { "Outdoor",  VMAX_OUTDOOR,
                            (int)(VMAX_OUTDOOR * VMAX_REVERSE_RATIO) },
    /* ASSIST_LEARNING */ { "Learning", VMAX_LEARNING,
                            (int)(VMAX_LEARNING * VMAX_REVERSE_RATIO) },
};

// ---------------------------------------------------------------------------
// Motor command output
// ---------------------------------------------------------------------------
struct MotorCommand {
    float leftSpeed;    // -100.0 ... +100.0 (percent, signed for direction)
    float rightSpeed;
    bool  isStop;       // true when both are 0 (joystick in deadzone)
};

// ---------------------------------------------------------------------------
// Differential steering calculation
//
//  x_norm: -1.0 (left) ... +1.0 (right)
//  y_norm: -1.0 (back) ... +1.0 (forward)
//  assistLevel: ASSIST_INDOOR / ASSIST_OUTDOOR / ASSIST_LEARNING
// ---------------------------------------------------------------------------
inline MotorCommand calculateMotorCommand(float x_norm, float y_norm,
                                          uint8_t assistLevel) {
    MotorCommand cmd = { 0.0f, 0.0f, true };

    if (y_norm == 0.0f && x_norm == 0.0f) {
        // Both axes in deadzone -> full stop
        return cmd;
    }

    const AssistConfig &cfg = assistConfigs[assistLevel];

    // Choose forward or reverse Vmax based on Y direction
    float vmax;
    if (y_norm >= 0.0f) {
        vmax = (float)cfg.vmaxForward;
    } else {
        vmax = -(float)cfg.vmaxReverse;   // negative for reverse
    }

    // Signed base speed for both wheels
    float baseSigned = y_norm * ((y_norm >= 0.0f)
                                 ? (float)cfg.vmaxForward
                                 : (float)cfg.vmaxReverse);
    // For reverse: y_norm is negative, vmaxReverse is positive -> result negative
    if (y_norm < 0.0f) {
        baseSigned = y_norm * (float)cfg.vmaxReverse;
    }

    // Differential mixing: the inner wheel of the turn is slowed down.
    //   x > 0 (right turn): right wheel is inner -> right_factor < 1
    //   x < 0 (left  turn): left  wheel is inner -> left_factor  < 1
    float xAbs = (x_norm > 0.0f) ? x_norm : -x_norm;
    float reduction = xAbs * TURN_REDUCTION;

    float leftFactor  = 1.0f;
    float rightFactor = 1.0f;

    if (x_norm > 0.0f) {
        // Turning right: slow down right wheel
        rightFactor = 1.0f - reduction;
    } else if (x_norm < 0.0f) {
        // Turning left: slow down left wheel
        leftFactor = 1.0f - reduction;
    }

    cmd.leftSpeed  = baseSigned * leftFactor;
    cmd.rightSpeed = baseSigned * rightFactor;

    // If the calculation produces zero on both axes (e.g. Y=0 with X-only input)
    // mark as stop so the caller uses bleSendStop() instead of sending an
    // encrypted speed-0 motor packet at 20 Hz unnecessarily.
    if (cmd.leftSpeed == 0.0f && cmd.rightSpeed == 0.0f) {
        cmd.isStop = true;
        return cmd;
    }
    cmd.isStop     = false;

    // Safety clamp to [-100, +100]
    if (cmd.leftSpeed  >  100.0f) cmd.leftSpeed  =  100.0f;
    if (cmd.leftSpeed  < -100.0f) cmd.leftSpeed  = -100.0f;
    if (cmd.rightSpeed >  100.0f) cmd.rightSpeed =  100.0f;
    if (cmd.rightSpeed < -100.0f) cmd.rightSpeed = -100.0f;

    return cmd;
}

// ---------------------------------------------------------------------------
// Convenience wrapper using JoystickNorm struct
// ---------------------------------------------------------------------------
inline MotorCommand joystickToMotorCommand(const JoystickNorm &js,
                                           uint8_t assistLevel) {
    return calculateMotorCommand(js.x, js.y, assistLevel);
}

// ---------------------------------------------------------------------------
// Debug print helper
// ---------------------------------------------------------------------------
inline void printMotorCommand(const MotorCommand &cmd) {
    if (cmd.isStop) {
        Serial.println("[Motor] STOP");
    } else {
        Serial.printf("[Motor] L=%+6.1f%%  R=%+6.1f%%\n",
                      cmd.leftSpeed, cmd.rightSpeed);
    }
}

#endif // MOTOR_CONTROL_H
