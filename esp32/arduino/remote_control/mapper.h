/*
 * mapper.h - Safety-critical control input transformation
 *
 * SAFETY-CRITICAL CODE
 * 
 * The Mapper enforces:
 * - Deadman switch requirement
 * - Deadzones to prevent drift
 * - Response curves for smooth control
 * - Speed limits per mode
 * - Acceleration ramping to prevent sudden changes
 * - Differential drive kinematics with proper normalization
 * 
 * Reference: core/mapper.py (Python implementation)
 * 
 * All control inputs flow through this module before reaching the wheels.
 * This is the primary safety layer that filters unsafe inputs.
 */

#ifndef MAPPER_H
#define MAPPER_H

#include <Arduino.h>
#include <math.h>

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
        : deadzone(0.1f)
        , curve(2.0f)
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
// Mapper Class - Transforms ControlState into safe CommandFrame
// ---------------------------------------------------------------------------
class Mapper {
public:
    /**
     * Initialize mapper with configuration.
     * 
     * @param config Mapper configuration (deadzones, curves, limits)
     */
    Mapper(const MapperConfig& config = MapperConfig())
        : _config(config)
        , _lastCommand()
        , _lastTime(0)
        , _hasLastCommand(false)
    {}
    
    /**
     * Convert ControlState to CommandFrame with all safety rules applied.
     * 
     * This is the main entry point. All safety checks happen here.
     * 
     * @param state Input control state from joystick/gamepad
     * @param outCommand Output command frame (valid only if function returns true)
     * @return true if command is safe to send, false if unsafe (triggers stop)
     */
    bool map(const ControlState& state, CommandFrame& outCommand);
    
    /**
     * Reset mapper state (e.g., when connection lost).
     * Clears previous command history.
     */
    void reset();
    
    /**
     * Get current configuration.
     */
    const MapperConfig& getConfig() const { return _config; }
    
    /**
     * Update configuration.
     */
    void setConfig(const MapperConfig& config) { _config = config; }
    
    /**
     * Get last command (for heartbeat).
     * Returns stop command if no previous command exists.
     */
    CommandFrame getLastCommand() const { 
        return _hasLastCommand ? _lastCommand : CommandFrame::stop(); 
    }

private:
    MapperConfig  _config;
    CommandFrame  _lastCommand;
    uint32_t      _lastTime;
    bool          _hasLastCommand;
    
    /**
     * Apply deadzone to eliminate drift and small movements.
     * 
     * Input below deadzone threshold returns 0.
     * Input above deadzone is rescaled to maintain full range.
     * 
     * @param value Input value (-1.0 to 1.0)
     * @return Deadzone-filtered value
     */
    float applyDeadzone(float value);
    
    /**
     * Apply exponential curve for smoother control.
     * 
     * Curve > 1.0 makes the response more gradual at low inputs,
     * giving finer control at slow speeds.
     * 
     * @param value Input value (-1.0 to 1.0)
     * @return Curved value
     */
    float applyCurve(float value);
    
    /**
     * Convert forward/turn inputs to left/right wheel speeds.
     * 
     * Standard differential drive kinematics:
     * - vx (forward) adds equally to both wheels
     * - vy (turn) adds to one wheel, subtracts from other
     * - Normalizes if magnitude exceeds 1.0
     * 
     * @param vx Forward/backward (-1.0 to 1.0)
     * @param vy Left/right (-1.0 to 1.0)
     * @param outLeft Output left wheel speed (-1.0 to 1.0)
     * @param outRight Output right wheel speed (-1.0 to 1.0)
     */
    void differentialDrive(float vx, float vy, float& outLeft, float& outRight);
    
    /**
     * Clamp value to range [minVal, maxVal].
     */
    float clamp(float value, float minVal, float maxVal);
    
    /**
     * Apply ramping to prevent sudden speed changes.
     * 
     * Limits the rate of change to config.rampRate.
     * 
     * @param target Desired speed
     * @param current Current speed
     * @param dt Time delta in seconds
     * @return Ramped speed (may not reach target yet)
     */
    float applyRamp(float target, float current, float dt);
    
    /**
     * Build flag bits from control state.
     * 
     * Can be used to encode mode, special features, etc.
     * 
     * @param state Control state
     * @return Flags as integer
     */
    uint8_t buildFlags(const ControlState& state);
};

#endif // MAPPER_H
