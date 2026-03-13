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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "types.h"
#include "mapper.h"
#include "m25_ble.h"

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
     * Request forced reconnection (disconnect then reconnect)
     * Works from any state except OFF
     */
    void requestReconnect();
    
    /**
     * Request emergency stop (enter FAILSAFE state)
     * 
     * @param reason  Reason for emergency stop
     */
    void requestEmergencyStop(const char* reason = "Emergency stop");
    
    /**
     * Request transition to ARMED state (ready to drive)
     * Only valid from PAIRED state
     */
    void requestArm();
    
    /**
     * Request disarm (ARMED or DRIVING -> PAIRED)
     * Sends stop first, then transitions back to PAIRED.
     * Safe to call from serial command handler at any time.
     */
    void requestDisarm();
    
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
     * Notify supervisor that a connection state changed
     * Call this when a wheel connects or disconnects
     */
    void notifyConnectionChange();
    
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
    const SupervisorConfig& getConfig() const { return _config; }
    bool isConnected() const;
    bool isDriving() const { return _state == SUPERVISOR_DRIVING; }
    // Returns ms remaining until auto-disarm fires (0 if not in ARMED or already expired)
    uint32_t getArmedIdleRemainingMs() const;
    // Returns the highest per-wheel retry count consumed in the current connect session
    uint8_t getReconnectAttempts() const {
        uint8_t m = 0;
        for (int i = 0; i < WHEEL_COUNT; i++) {
            if (_wheelRetries[i] > m) m = _wheelRetries[i];
        }
        return m;
    }
    
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
    uint32_t _lastTelemetryPollMs;  // Timestamp of last telemetry request burst
    // Joystick hold timers for ARMED<->DRIVING hysteresis (JS_ACTIVATE_HOLD_MS / JS_IDLE_HOLD_MS)
    uint32_t _activateHoldStartMs;  // millis() when joystick first moved out of deadzone in ARMED
    uint32_t _idleHoldStartMs;      // millis() when joystick first returned to deadzone in DRIVING
    uint32_t _armedEntryMs;         // millis() when ARMED state was entered (idle disarm timer)
    uint32_t _driveEntryMs;         // millis() when DRIVING state was entered (stale-notify watchdog anchor)
    uint32_t _reconnectNotBeforeMs; // earliest millis() to spawn first connect task (post-stop settle)
    // Single-wheel reconnect tracking
    bool _partialReconnect;         // true = entered CONNECTING to recover a dropped wheel
    
    // ---------------------------------------------------------------------------
    // Connection Management
    // ---------------------------------------------------------------------------
    uint8_t _wheelRetries[WHEEL_COUNT]; // Per-wheel retry budgets (independent counters)
    char    _leftAddr[18];
    char    _rightAddr[18];
    uint8_t _leftKey[16];
    uint8_t _rightKey[16];
    bool    _connectionRequested;
    bool    _lastLeftConnected;   // Track wheel connection state changes
    bool    _lastRightConnected;  // Track wheel connection state changes
    
    // ---------------------------------------------------------------------------
    // Vehicle State Cache
    // ---------------------------------------------------------------------------
    VehicleState _vehicleState;
    
    // ---------------------------------------------------------------------------
    // Callbacks
    // ---------------------------------------------------------------------------
    StateCallback _stateCallbacks[4];  // Support up to 4 callbacks
    uint8_t       _callbackCount;
    
    // Connect task (runs blocking BLE work on Core 0, away from loop())
    TaskHandle_t   _connectTask;       // nullptr when idle
    volatile bool  _connectDone;       // set by task when finished
    volatile bool  _connectAbort;      // set by Core 1 to cancel mid-run

    static void _sConnectTask(void* pv);   // FreeRTOS entry point
    void        _connectTaskBody();        // actual blocking connect logic

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
    // Initiate a partial reconnect (dropped wheel while operating).
    // Resets per-wheel retry counters, sets _partialReconnect, and transitions
    // to CONNECTING.  CONNECTING will escalate to a full reconnect if the
    // dropped wheel cannot be recovered within its retry budget.
    void _triggerPartialReconnect();
    
    // ---------------------------------------------------------------------------
    // Watchdogs
    // ---------------------------------------------------------------------------
    void checkWatchdogs();
    
    // ---------------------------------------------------------------------------
    // Telemetry Polling
    // ---------------------------------------------------------------------------
    void pollTelemetry(); // Fire BLE requests + pull cache into _vehicleState
    
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
    // Push the currently stored connection credentials into BLE runtime state.
    void applyRuntimeWheelConfig();
};

#endif // SUPERVISOR_H
