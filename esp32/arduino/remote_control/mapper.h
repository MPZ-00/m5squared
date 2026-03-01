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
#include "types.h"

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
