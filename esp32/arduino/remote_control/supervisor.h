/*
 * supervisor.h - State machine, watchdogs, and safety orchestration
 *
 * SAFETY-CRITICAL CODE
 * 
 * The Supervisor is the main control loop coordinator. It:
 * - Manages state transitions (DISCONNECTED -> CONNECTING -> PAIRED -> ARMED -> DRIVING)
 * - Runs watchdogs (input timeout, link timeout, heartbeat keepalive)
 * - Coordinates control flow between input, mapper, and transport
 * - Ensures safety at all times through strict state machine rules
 * - Handles connection loss and auto-reconnection with exponential backoff
 * 
 * Reference: core/supervisor.py (Python implementation)
 * 
 * This is the safety orchestrator that enforces system-level rules.
 */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <Arduino.h>
#include "mapper.h"
#include "m25_ble.h"

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

// ---------------------------------------------------------------------------
// State to String Helper
// ---------------------------------------------------------------------------
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
// Vehicle State - Cached telemetry from wheels
// ---------------------------------------------------------------------------
struct VehicleState {
    int      batteryLeft;      // Left wheel battery % (0-100), -1 if unknown
    int      batteryRight;     // Right wheel battery % (0-100), -1 if unknown
    float    speedKmh;         // Current speed in km/h, -1.0 if unknown
    float    distanceKm;       // Total distance traveled, -1.0 if unknown
    bool     connected;        // Connection status
    bool     hasErrors;        // Whether there are any errors
    uint32_t timestamp;        // Milliseconds since boot
    
    VehicleState()
        : batteryLeft(-1)
        , batteryRight(-1)
        , speedKmh(-1.0f)
        , distanceKm(-1.0f)
        , connected(false)
        , hasErrors(false)
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
// Supervisor Configuration
// ---------------------------------------------------------------------------
struct SupervisorConfig {
    uint32_t loopIntervalMs;         // Main loop interval (default: 50ms = 20Hz)
    uint32_t inputTimeoutMs;         // Max time without input before failsafe (default: 500ms)
    uint32_t linkTimeoutMs;          // Max time without successful command before failsafe (default: 2000ms)
    uint32_t heartbeatIntervalMs;    // Send heartbeat every N milliseconds (default: 1000ms)
    uint32_t reconnectDelayMs;       // Delay between reconnection attempts (default: 2000ms)
    uint8_t  maxReconnectAttempts;   // Max reconnection attempts before giving up (default: 5)
    
    SupervisorConfig()
        : loopIntervalMs(50)           // 20 Hz
        , inputTimeoutMs(500)          // 0.5 seconds
        , linkTimeoutMs(2000)          // 2 seconds
        , heartbeatIntervalMs(1000)    // 1 second
        , reconnectDelayMs(2000)       // 2 seconds
        , maxReconnectAttempts(5)      // 5 attempts
    {}
};

// ---------------------------------------------------------------------------
// State Transition Callback Type
// ---------------------------------------------------------------------------
typedef void (*StateCallback)(SupervisorState oldState, SupervisorState newState);

// ---------------------------------------------------------------------------
// Supervisor Class
// ---------------------------------------------------------------------------
class Supervisor {
public:
    /**
     * Constructor
     * 
     * @param mapper       Reference to Mapper instance
     * @param config       Supervisor configuration
     */
    Supervisor(Mapper& mapper, const SupervisorConfig& config = SupervisorConfig());
    
    /**
     * Initialize supervisor (call once in setup())
     */
    void begin();
    
    /**
     * Main update loop (call in loop())
     * Returns true if update was performed (based on loop interval)
     */
    bool update();
    
    /**
     * Request connection to vehicles
     * 
     * @param leftAddr   Left wheel BLE address
     * @param rightAddr  Right wheel BLE address
     * @param leftKey    Left wheel encryption key (16 bytes)
     * @param rightKey   Right wheel encryption key (16 bytes)
     */
    void requestConnect(const char* leftAddr, const char* rightAddr,
                       const uint8_t* leftKey, const uint8_t* rightKey);
    
    /**
     * Request disconnect from vehicles
     */
    void requestDisconnect();
    
    /**
     * Request transition to ARMED state (ready to drive)
     * Only valid from PAIRED state
     */
    void requestArm();
    
    /**
     * Process control input (call when new input is available)
     * 
     * @param control  Control state from joystick/gamepad
     */
    void processInput(const ControlState& control);
    
    /**
     * Update vehicle state (call when telemetry is received)
     * 
     * @param state  Vehicle state from BLE transport
     */
    void updateVehicleState(const VehicleState& state);
    
    /**
     * Register callback for state changes
     * 
     * @param callback  Function to call on state change (oldState, newState)
     */
    void addStateCallback(StateCallback callback);
    
    // ---------------------------------------------------------------------------
    // Public Accessors
    // ---------------------------------------------------------------------------
    
    SupervisorState getState() const { return _state; }
    const VehicleState& getVehicleState() const { return _vehicleState; }
    bool isConnected() const;
    bool isDriving() const { return _state == SUPERVISOR_DRIVING; }
    uint8_t getReconnectAttempts() const { return _reconnectAttempts; }
    
    // Watchdog status
    uint32_t getTimeSinceLastInput() const;
    uint32_t getTimeSinceLastLink() const;
    bool isInputTimeout() const;
    bool isLinkTimeout() const;
    
private:
    // ---------------------------------------------------------------------------
    // Dependencies
    // ---------------------------------------------------------------------------
    Mapper&         _mapper;
    SupervisorConfig _config;
    
    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------
    SupervisorState _state;
    bool           _stopRequested;
    
    // ---------------------------------------------------------------------------
    // Timing
    // ---------------------------------------------------------------------------
    uint32_t _lastUpdateMs;
    uint32_t _lastInputTimeMs;
    uint32_t _lastLinkTimeMs;
    uint32_t _lastHeartbeatMs;
    uint32_t _connectAttemptMs;
    
    // ---------------------------------------------------------------------------
    // Connection Management
    // ---------------------------------------------------------------------------
    uint8_t _reconnectAttempts;
    char    _leftAddr[18];
    char    _rightAddr[18];
    uint8_t _leftKey[16];
    uint8_t _rightKey[16];
    bool    _connectionRequested;
    
    // ---------------------------------------------------------------------------
    // Vehicle State Cache
    // ---------------------------------------------------------------------------
    VehicleState _vehicleState;
    
    // ---------------------------------------------------------------------------
    // Callbacks
    // ---------------------------------------------------------------------------
    StateCallback _stateCallbacks[4];  // Support up to 4 callbacks
    uint8_t       _callbackCount;
    
    // ---------------------------------------------------------------------------
    // State Handlers
    // ---------------------------------------------------------------------------
    void handleDisconnected();
    void handleConnecting();
    void handlePaired();
    void handleArmed();
    void handleDriving();
    void handleFailsafe();
    void handleStopRequest();
    
    // ---------------------------------------------------------------------------
    // Watchdogs
    // ---------------------------------------------------------------------------
    void checkWatchdogs();
    
    // ---------------------------------------------------------------------------
    // Command Sending
    // ---------------------------------------------------------------------------
    void sendStop();
    void sendHeartbeat();
    void sendCommand(const CommandFrame& cmd);
    
    // ---------------------------------------------------------------------------
    // State Transitions
    // ---------------------------------------------------------------------------
    void transitionTo(SupervisorState newState);
    void enterFailsafe(const char* reason);
    
    // ---------------------------------------------------------------------------
    // Utilities
    // ---------------------------------------------------------------------------
    void notifyStateCallbacks(SupervisorState oldState, SupervisorState newState);
};

#endif // SUPERVISOR_H
