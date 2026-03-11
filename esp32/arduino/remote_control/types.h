/*
 * types.h - Core data types for M25 remote control system
 *
 * Shared types used across mapper, supervisor, and transport components.
 * Consolidates definitions to avoid duplication.
 * 
 * Reference: core/types.py (Python implementation)
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Drive Mode Enumeration
// ---------------------------------------------------------------------------
enum DriveMode {
    DRIVE_MODE_STOP   = 0,  // Emergency stop / parked
    DRIVE_MODE_SLOW   = 1,  // Reduced speed for indoor/tight spaces
    DRIVE_MODE_NORMAL = 2,  // Standard outdoor speed
    DRIVE_MODE_FAST   = 3   // Maximum speed (use with caution)
};

// ---------------------------------------------------------------------------
// Supervisor State Enumeration
// ---------------------------------------------------------------------------
enum SupervisorState {
    SUPERVISOR_DISCONNECTED,  // No connection to vehicles
    SUPERVISOR_CONNECTING,    // Attempting to connect
    SUPERVISOR_PAIRED,        // Connected but not ready to drive
    SUPERVISOR_ARMED,         // Ready to drive, waiting for input
    SUPERVISOR_DRIVING,       // Actively controlling vehicles
    SUPERVISOR_FAILSAFE       // Emergency state, sending stop commands
};

// State to String Helper
inline const char* supervisorStateToString(SupervisorState state) {
    switch (state) {
        case SUPERVISOR_DISCONNECTED: return "DISCONNECTED";
        case SUPERVISOR_CONNECTING:   return "CONNECTING";
        case SUPERVISOR_PAIRED:       return "PAIRED";
        case SUPERVISOR_ARMED:        return "ARMED";
        case SUPERVISOR_DRIVING:      return "DRIVING";
        case SUPERVISOR_FAILSAFE:     return "FAILSAFE";
        default:                      return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Control State - Normalized input from joystick/gamepad
// ---------------------------------------------------------------------------
struct ControlState {
    float     vx;          // Forward/backward: -1.0 (back) to 1.0 (forward)
    float     vy;          // Left/right: -1.0 (left) to 1.0 (right)
    bool      deadman;     // Hold-to-drive safety switch
    DriveMode mode;        // Current drive mode
    uint32_t  timestamp;   // Milliseconds since boot
    
    ControlState()
        : vx(0.0f)
        , vy(0.0f)
        , deadman(false)
        , mode(DRIVE_MODE_STOP)
        , timestamp(0)
    {}
    
    // Check if joystick is in neutral position
    bool isNeutral() const {
        return fabsf(vx) < 0.01f && fabsf(vy) < 0.01f;
    }
    
    // Check if control state is safe to execute
    bool isSafe() const {
        return deadman && mode != DRIVE_MODE_STOP;
    }
};

// ---------------------------------------------------------------------------
// Command Frame - Output to send to wheels
// ---------------------------------------------------------------------------
struct CommandFrame {
    int      leftSpeed;    // Left wheel speed: -100 to 100
    int      rightSpeed;   // Right wheel speed: -100 to 100
    uint8_t  flags;        // Additional flags (mode bits, etc.)
    uint32_t timestamp;    // Milliseconds since boot
    
    CommandFrame()
        : leftSpeed(0)
        , rightSpeed(0)
        , flags(0)
        , timestamp(0)
    {}
    
    CommandFrame(int left, int right, uint8_t f = 0)
        : leftSpeed(left)
        , rightSpeed(right)
        , flags(f)
        , timestamp(millis())
    {}
    
    // Check if this is a stop command
    bool isStop() const {
        return leftSpeed == 0 && rightSpeed == 0;
    }
    
    // Create a stop command
    static CommandFrame stop() {
        return CommandFrame(0, 0, 0);
    }
};

// ---------------------------------------------------------------------------
// Vehicle State - Cached telemetry from wheels
// ---------------------------------------------------------------------------
struct VehicleState {
    int      batteryLeft;      // Left wheel battery % (0-100), -1 if unknown
    int      batteryRight;     // Right wheel battery % (0-100), -1 if unknown
    float    speedKmh;         // Current speed in km/h, -1.0 if unknown
    float    distanceKm;       // Total distance traveled, -1.0 if unknown
    bool     connected;        // Connection status
    bool     hasErrors;        // Whether there are any errors
    bool     lowBattery;       // True when either wheel is below lowBatteryThreshold
    uint32_t timestamp;        // Milliseconds since boot
    
    VehicleState()
        : batteryLeft(-1)
        , batteryRight(-1)
        , speedKmh(-1.0f)
        , distanceKm(-1.0f)
        , connected(false)
        , hasErrors(false)
        , lowBattery(false)
        , timestamp(0)
    {}
    
    // Get minimum battery level (limiting factor)
    int batteryMin() const {
        if (batteryLeft < 0 && batteryRight < 0) return -1;
        if (batteryLeft < 0) return batteryRight;
        if (batteryRight < 0) return batteryLeft;
        return min(batteryLeft, batteryRight);
    }
    
    // Check if vehicle is healthy and ready
    bool isHealthy() const {
        return connected && !hasErrors;
    }
};

// ---------------------------------------------------------------------------
// Mapper Configuration
// ---------------------------------------------------------------------------
struct MapperConfig {
    float   deadzone;           // Ignore inputs below this threshold (0.0-1.0)
    float   curve;              // Exponential curve (1.0 = linear, >1 = more gradual at low inputs)
    int     maxSpeedSlow;       // Max speed in SLOW mode (0-100)
    int     maxSpeedNormal;     // Max speed in NORMAL mode (0-100)
    int     maxSpeedFast;       // Max speed in FAST mode (0-100)
    float   rampRate;           // Max speed change per second (units/sec)
    
    // Default constructor with safe defaults
    MapperConfig()
        : deadzone(0.05f)  // joystick.h already removes the hardware deadzone;
                           // keep a small mapper deadzone only for residual ADC drift
        , curve(1.0f)      // linear response - quadratic (2.0) made wheels silent
                           // until ~1200 raw ADC past center (35 % deflection)
        , maxSpeedSlow(30)
        , maxSpeedNormal(60)
        , maxSpeedFast(100)
        , rampRate(50.0f)
    {}
    
    // Get max speed for a given drive mode
    int getMaxSpeed(DriveMode mode) const {
        switch (mode) {
            case DRIVE_MODE_STOP:   return 0;
            case DRIVE_MODE_SLOW:   return maxSpeedSlow;
            case DRIVE_MODE_NORMAL: return maxSpeedNormal;
            case DRIVE_MODE_FAST:   return maxSpeedFast;
            default:                return maxSpeedNormal;
        }
    }
};

// ---------------------------------------------------------------------------
// Supervisor Configuration
// ---------------------------------------------------------------------------
struct SupervisorConfig {
    uint32_t loopIntervalMs;         // Main loop interval (default: 50ms = 20Hz)
    uint32_t inputTimeoutMs;         // Max time without input in DRIVING before failsafe (default: 500ms)
    uint32_t armIdleTimeoutMs;       // Max time idle in ARMED before graceful disarm to PAIRED (default: 60000ms)
    uint32_t linkTimeoutMs;          // Max time without successful command before failsafe (default: 2000ms)
    uint32_t heartbeatIntervalMs;    // Send heartbeat every N milliseconds (default: 1000ms)
    uint32_t reconnectDelayMs;       // Delay between reconnection attempts (default: 2000ms)
    uint8_t  maxReconnectAttempts;   // Max reconnection attempts before giving up (default: 5)
    uint32_t telemetryPollIntervalMs;// How often to poll battery/firmware/odometer (default: 10000ms)
    uint8_t  lowBatteryThreshold;    // Battery % below which low-battery limiting applies (default: 20)
    // Stale-notify watchdog: if the wheel was sending notify responses during DRIVING
    // but stops (because BLE writes silently fail, e.g. GATT rc=-1), declare the link
    // dead and enter FAILSAFE.  The watchdog is self-disabling: it only activates
    // after at least one notify has been seen since DRIVING was entered, so it will
    // not false-positive if the wheel does not ACK motor commands.
    // Set to 0 to disable.
    uint32_t notifyStaleTimeoutMs;   // (default: 2000 ms)
    
    SupervisorConfig()
        : loopIntervalMs(50)           // 20 Hz
        , inputTimeoutMs(500)          // 0.5 seconds (DRIVING only)
        , armIdleTimeoutMs(60000)      // 60 seconds idle before auto-disarm
        , linkTimeoutMs(2000)          // 2 seconds
        , heartbeatIntervalMs(1000)    // 1 second
        , reconnectDelayMs(2000)       // 2 seconds
        , maxReconnectAttempts(5)      // 5 attempts
        , telemetryPollIntervalMs(10000) // 10 seconds
        , lowBatteryThreshold(20)      // 20 %
        , notifyStaleTimeoutMs(2000)   // 2 seconds
    {}
};

#endif // TYPES_H
