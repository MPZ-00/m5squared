/**
 * command.h - Parse decoded SPP bytes and update wheel state.
 *
 * One function: command_apply().
 * It reads the service/param IDs from the decrypted SPP payload, mutates
 * the WheelState accordingly, and tells the caller whether to send an ACK.
 *
 * No transport, no crypto, no Serial prints outside debug path.
 */

#ifndef COMMAND_H
#define COMMAND_H

#include <Arduino.h>
#include "state.h"

// ---------------------------------------------------------------------------
// M25 SPP protocol identifiers (from m25_protocol_data.py)
// ---------------------------------------------------------------------------
#define SPP_SERVICE_APP_MGMT    0x01

#define SPP_PARAM_SYSTEM_MODE   0x10
#define SPP_PARAM_DRIVE_MODE    0x20
#define SPP_PARAM_REMOTE_SPEED  0x30
#define SPP_PARAM_ASSIST_LEVEL  0x40

// Drive-mode flag bits
#define DRIVE_FLAG_HILL_HOLD    0x01

// Minimum SPP frame length: proto(1) telegram(1) src(1) dst(1) svc(1) param(1) = 6
#define SPP_MIN_LEN 6

// Result returned by command_apply()
typedef enum {
    CMD_IGNORE,        // Nothing to do
    CMD_SEND_ACK,      // Caller should send ACK response
    CMD_SPEED_UPDATE,  // Speed updated (ACK usually skipped for high-rate cmds)
} CmdResult;

// ---------------------------------------------------------------------------
// command_apply - parse SPP payload and update state.
//
//   spp / sppLen : decrypted SPP bytes
//   s            : wheel state to update (modified in place)
//   debug        : if true, print command details to Serial
//
//   Returns a CmdResult so the caller can decide whether to send an ACK.
// ---------------------------------------------------------------------------
inline CmdResult command_apply(const uint8_t* spp, size_t sppLen,
                                WheelState* s, bool debug) {
    if (sppLen < SPP_MIN_LEN) return CMD_IGNORE;

    const uint8_t serviceId = spp[4];
    const uint8_t paramId   = spp[5];

    if (serviceId != SPP_SERVICE_APP_MGMT) {
        if (debug) Serial.printf("[CMD] Unknown service 0x%02X, param 0x%02X\n",
                                  serviceId, paramId);
        return CMD_IGNORE;
    }

    switch (paramId) {

        case SPP_PARAM_SYSTEM_MODE:
            if (debug) {
                Serial.print("[CMD] SYSTEM_MODE");
                if (sppLen > SPP_MIN_LEN) Serial.printf(" = 0x%02X\n", spp[6]);
                else                       Serial.println();
            }
            return CMD_SEND_ACK;

        case SPP_PARAM_DRIVE_MODE:
            if (sppLen > SPP_MIN_LEN) {
                const uint8_t mode = spp[6];
                s->hillHold = (mode & DRIVE_FLAG_HILL_HOLD) != 0;
                if (debug) {
                    Serial.printf("[CMD] DRIVE_MODE = 0x%02X [", mode);
                    if (mode & DRIVE_FLAG_HILL_HOLD) Serial.print("HILL_HOLD ");
                    if (mode & 0x02)                 Serial.print("CRUISE ");
                    if (mode & 0x04)                 Serial.print("REMOTE");
                    Serial.println("]");
                }
            }
            return CMD_SEND_ACK;

        case SPP_PARAM_REMOTE_SPEED:
            if (sppLen >= SPP_MIN_LEN + 2) {
                const int16_t speed = ((int16_t)spp[6] << 8) | spp[7];
                s->lastSpeed    = s->speed;
                s->speed        = speed;
                s->lastCmdMs    = millis();

                // Simulate rotation when moving
                const unsigned long now = millis();
                if (abs(speed) > 5 && (now - s->lastSpeedUpdate) > 5000) {
                    const int rotations = abs(speed) / 50;
                    if (rotations > 0) state_simulate_rotation(s, rotations);
                    s->lastSpeedUpdate = now;
                }

                if (debug) {
                    Serial.printf("[CMD] REMOTE_SPEED = %d raw (%.1f%%)\n",
                                  speed, speed / 2.5f);
                }
            }
            return CMD_SPEED_UPDATE;   // Caller skips ACK for speed (high rate)

        case SPP_PARAM_ASSIST_LEVEL:
            if (sppLen > SPP_MIN_LEN) {
                const uint8_t level = spp[6];
                if (level < 3) s->assistLevel = level;
                if (debug) {
                    const char* names[] = { "INDOOR", "OUTDOOR", "LEARNING" };
                    Serial.printf("[CMD] ASSIST_LEVEL = %d (%s)\n",
                                  level, level < 3 ? names[level] : "UNKNOWN");
                }
            }
            return CMD_SEND_ACK;

        default:
            if (debug) Serial.printf("[CMD] Unknown param 0x%02X\n", paramId);
            return CMD_IGNORE;
    }
}

#endif // COMMAND_H
