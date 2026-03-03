/*
 * supervisor.cpp - State machine, watchdogs, and safety orchestration
 *
 * SAFETY-CRITICAL CODE
 * 
 * Implementation of the Supervisor class.
 * See supervisor.h for detailed documentation.
 */

#include "supervisor.h"
#include "m25_ble.h"

// External debug flags (defined in serial_commands.h)
extern uint8_t debugFlags;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Supervisor::Supervisor(Mapper& mapper, const SupervisorConfig& config)
    : _mapper(mapper)
    , _config(config)
    , _state(SUPERVISOR_DISCONNECTED)
    , _stopRequested(false)
    , _lastUpdateMs(0)
    , _lastInputTimeMs(0)
    , _lastLinkTimeMs(0)
    , _lastHeartbeatMs(0)
    , _connectAttemptMs(0)
    , _reconnectAttempts(0)
    , _connectionRequested(false)
    , _callbackCount(0)
{
    memset(_leftAddr, 0, sizeof(_leftAddr));
    memset(_rightAddr, 0, sizeof(_rightAddr));
    memset(_leftKey, 0, sizeof(_leftKey));
    memset(_rightKey, 0, sizeof(_rightKey));
    memset(_stateCallbacks, 0, sizeof(_stateCallbacks));
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
void Supervisor::begin() {
    _lastUpdateMs = millis();
    _lastHeartbeatMs = millis();
    Serial.println("[Supervisor] Initialized");
}

// ---------------------------------------------------------------------------
// Main Update Loop
// ---------------------------------------------------------------------------
bool Supervisor::update() {
    uint32_t now = millis();
    
    // Rate limiting based on loop interval
    if (now - _lastUpdateMs < _config.loopIntervalMs) {
        return false;
    }
    _lastUpdateMs = now;
    
    // Handle stop request
    if (_stopRequested) {
        handleStopRequest();
        return true;
    }
    
    // State machine
    switch (_state) {
        case SUPERVISOR_DISCONNECTED:
            handleDisconnected();
            break;
        
        case SUPERVISOR_CONNECTING:
            handleConnecting();
            break;
        
        case SUPERVISOR_PAIRED:
            handlePaired();
            break;
        
        case SUPERVISOR_ARMED:
            handleArmed();
            break;
        
        case SUPERVISOR_DRIVING:
            handleDriving();
            break;
        
        case SUPERVISOR_FAILSAFE:
            handleFailsafe();
            break;
    }
    
    // Watchdogs (active in ARMED and DRIVING states)
    if (_state == SUPERVISOR_ARMED || _state == SUPERVISOR_DRIVING) {
        checkWatchdogs();
    }
    
    return true;
}

// ---------------------------------------------------------------------------
// Connection Management
// ---------------------------------------------------------------------------
void Supervisor::requestConnect(const char* leftAddr, const char* rightAddr,
                                const uint8_t* leftKey, const uint8_t* rightKey) {
    if (!leftAddr || !rightAddr || !leftKey || !rightKey) {
        Serial.println("[Supervisor] ERROR: NULL connection parameters provided");
        return;
    }
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[Supervisor] requestConnect: L=%s R=%s\n", leftAddr, rightAddr);
    }
    
    strncpy(_leftAddr, leftAddr, sizeof(_leftAddr) - 1);
    _leftAddr[sizeof(_leftAddr) - 1] = '\0';
    strncpy(_rightAddr, rightAddr, sizeof(_rightAddr) - 1);
    _rightAddr[sizeof(_rightAddr) - 1] = '\0';
    memcpy(_leftKey, leftKey, 16);
    memcpy(_rightKey, rightKey, 16);
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[Supervisor] Stored: L=%s R=%s\n", _leftAddr, _rightAddr);
    }
    
    _connectionRequested = true;
    
    if (_state == SUPERVISOR_DISCONNECTED) {
        transitionTo(SUPERVISOR_CONNECTING);
    }
}

void Supervisor::requestDisconnect() {
    if (_state != SUPERVISOR_DISCONNECTED) {
        _stopRequested = true;
    }
}

void Supervisor::requestReconnect() {
    if (debugFlags & DBG_STATE) {
        Serial.println("[Supervisor] Forced reconnect requested");
    }
    
    // Request disconnect first
    _stopRequested = true;
    
    // Connection will be re-initiated after disconnect completes
    _connectionRequested = true;
}

void Supervisor::requestEmergencyStop(const char* reason) {
    if (debugFlags & DBG_STATE) {
        Serial.printf("[Supervisor] Emergency stop requested: %s\n", reason);
    }
    enterFailsafe(reason);
}

void Supervisor::requestArm() {
    if (_state == SUPERVISOR_PAIRED) {
        transitionTo(SUPERVISOR_ARMED);
    }
}

// ---------------------------------------------------------------------------
// Input Processing
// ---------------------------------------------------------------------------
void Supervisor::processInput(const ControlState& control) {
    _lastInputTimeMs = millis();
    
    // Different behavior depending on state
    if (_state == SUPERVISOR_ARMED) {
        // Check if user wants to drive (deadman + movement)
        if (control.deadman && !control.isNeutral()) {
            transitionTo(SUPERVISOR_DRIVING);
            // Process this input in DRIVING state on next update
        } else {
            // User not ready to drive yet - send stop to be safe
            sendStop();
        }
    }
    else if (_state == SUPERVISOR_DRIVING) {
        // Check if user released controls
        if (!control.deadman || control.isNeutral()) {
            Serial.println("[Supervisor] User released controls, returning to ARMED");
            sendStop();
            transitionTo(SUPERVISOR_ARMED);
            return;
        }
        
        // Map to command
        CommandFrame cmd;
        bool valid = _mapper.map(control, cmd);
        
        // Mapper enforces safety - check if command is valid
        if (!valid || !control.isSafe()) {
            Serial.println("[Supervisor] Mapper rejected input (safety violation)");
            sendStop();
            return;
        }
        
        // Send command
        sendCommand(cmd);
    }
    // In other states, input is ignored
}

// ---------------------------------------------------------------------------
// Vehicle State Update
// ---------------------------------------------------------------------------
void Supervisor::updateVehicleState(const VehicleState& state) {
    _vehicleState = state;
    _vehicleState.timestamp = millis();
    
    // Check for errors
    if (state.hasErrors) {
        enterFailsafe("Vehicle error detected");
    }
}

// ---------------------------------------------------------------------------
// State Callback Registration
// ---------------------------------------------------------------------------
void Supervisor::addStateCallback(StateCallback callback) {
    if (_callbackCount < 4) {
        _stateCallbacks[_callbackCount++] = callback;
    }
}

// ---------------------------------------------------------------------------
// State Handlers
// ---------------------------------------------------------------------------
void Supervisor::handleDisconnected() {
    // Nothing to do, waiting for connection request
    // Connection request will trigger transition to CONNECTING
}

void Supervisor::handleConnecting() {
    uint32_t now = millis();
    
    // Rate limit connection attempts
    if (now - _connectAttemptMs < _config.reconnectDelayMs) {
        return;
    }
    
    Serial.printf("[Supervisor] Connecting to vehicles (attempt %d/%d)\n",
                  _reconnectAttempts + 1, _config.maxReconnectAttempts);
    
    if (debugFlags & DBG_BLE) {
        Serial.printf("[Supervisor] Target MACs: L=%s R=%s\n", _leftAddr, _rightAddr);
    }
    
    // Note: MACs and keys are already configured in bleInit() from device_config.h
    // Only call bleSetMac/bleSetKey when user explicitly overrides via serial commands,
    // not during connection attempts (they disconnect wheels as side effect)
    
    if (debugFlags & DBG_BLE) {
        Serial.println("[Supervisor] Calling bleConnect()...");
    }
    
    // Attempt connection
    bleConnect();
    
    // Update timestamp AFTER bleConnect() completes to properly enforce reconnectDelayMs
    // (bleConnect is blocking and can take several seconds)
    _connectAttemptMs = millis();
    
    // Check if connection was successful
    // Use bleAnyConnected() to allow partial connectivity in dual mode
    // (one wheel working while other is off/dead/out of range)
    // Background reconnection via bleTick() will keep trying to connect missing wheels
    bool success = bleAnyConnected();
    
    if (success) {
        if (bleAllConnected()) {
            Serial.println("[Supervisor] Connected successfully (all wheels)");
        } else {
            Serial.println("[Supervisor] Connected successfully (partial - some wheels unreachable)");
        }
        _reconnectAttempts = 0;
        transitionTo(SUPERVISOR_PAIRED);
        _lastLinkTimeMs = millis();
    } else {
        Serial.println("[Supervisor] Connection failed (no wheels responding)");
        
        // Allow time for BLE stack cleanup (disconnect callbacks, resource release)
        // before next reconnection attempt. Critical for dual-wheel mode to prevent
        // GATT connection slot contention.
        delay(500);
        
        _reconnectAttempts++;
        
        if (_reconnectAttempts >= _config.maxReconnectAttempts) {
            Serial.println("[Supervisor] Max reconnection attempts reached");
            transitionTo(SUPERVISOR_DISCONNECTED);
            _reconnectAttempts = 0;
        }
        // Otherwise stay in CONNECTING and retry on next update
    }
}

void Supervisor::handlePaired() {
    // Connected but not armed - just maintain connection
    // Check if we lost ALL connections (partial connectivity OK)
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections in PAIRED state");
        transitionTo(SUPERVISOR_DISCONNECTED);
        return;
    }
    
    // Wait for explicit arm request from user
    // This is a safety feature - user must explicitly enable driving
}

void Supervisor::handleArmed() {
    // Ready to drive, waiting for input
    // Check if we lost ALL connections (partial connectivity OK)
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections in ARMED state");
        enterFailsafe("Connection lost");
        return;
    }
    // Input processing happens in processInput()
    // Just maintain heartbeat here
}

void Supervisor::handleDriving() {
    // Actively controlling vehicles
    // Check if we lost ALL connections (partial connectivity OK)
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections while driving");
        enterFailsafe("Connection lost");
        return;
    }
    // Input processing happens in processInput()
    // Watchdogs are checked in main update loop
}

void Supervisor::handleFailsafe() {
    // Emergency state - send stop commands periodically
    sendStop();
    
    // Check if we can recover
    if (!bleAnyConnected()) {
        // Lost connection
        Serial.println("[Supervisor] Connection lost in failsafe, disconnecting");
        transitionTo(SUPERVISOR_DISCONNECTED);
    }
    // Otherwise stay in failsafe until manual intervention
}

void Supervisor::handleStopRequest() {
    Serial.println("[Supervisor] Stop requested");
    sendStop();
    bleDisconnect();
    transitionTo(SUPERVISOR_DISCONNECTED);
    _stopRequested = false;
    
    // If reconnection was requested, initiate it now
    if (_connectionRequested) {
        transitionTo(SUPERVISOR_CONNECTING);
    }
}

// ---------------------------------------------------------------------------
// Watchdogs
// ---------------------------------------------------------------------------
void Supervisor::checkWatchdogs() {
    uint32_t now = millis();
    
    // Input watchdog - no input for too long
    if (_lastInputTimeMs > 0) {  // Only check if we've received input before
        if (now - _lastInputTimeMs > _config.inputTimeoutMs) {
            Serial.println("[Supervisor] Input watchdog timeout");
            enterFailsafe("Input timeout");
            return;
        }
    }
    
    // Link watchdog - no successful command for too long
    if (_lastLinkTimeMs > 0) {
        if (now - _lastLinkTimeMs > _config.linkTimeoutMs) {
            Serial.println("[Supervisor] Link watchdog timeout");
            enterFailsafe("Link timeout");
            return;
        }
    }
    
    // Heartbeat - send periodic command even if no input change
    if (now - _lastHeartbeatMs > _config.heartbeatIntervalMs) {
        sendHeartbeat();
    }
}

// ---------------------------------------------------------------------------
// Command Sending
// ---------------------------------------------------------------------------
void Supervisor::sendStop() {
    bleSendStop();
    _lastLinkTimeMs = millis();
}

void Supervisor::sendHeartbeat() {
    // Send last known good command, or stop if none
    CommandFrame cmd = _mapper.getLastCommand();
    if (cmd.isStop()) {
        sendStop();
    } else {
        sendCommand(cmd);
    }
    _lastHeartbeatMs = millis();
}

void Supervisor::sendCommand(const CommandFrame& cmd) {
    // Convert CommandFrame speeds (-100 to 100) to floats for BLE interface
    bool success = bleSendMotorCommand((float)cmd.leftSpeed, (float)cmd.rightSpeed);
    if (success) {
        _lastLinkTimeMs = millis();
    } else {
        Serial.println("[Supervisor] Failed to send command");
        // Don't failsafe immediately, will be caught by link watchdog
    }
}

// ---------------------------------------------------------------------------
// State Transitions
// ---------------------------------------------------------------------------
void Supervisor::transitionTo(SupervisorState newState) {
    if (newState == _state) {
        return;
    }
    
    SupervisorState oldState = _state;
    Serial.printf("[Supervisor] State transition: %s -> %s\n",
                  supervisorStateToString(oldState),
                  supervisorStateToString(newState));
    
    _state = newState;
    
    // Reset mapper state on certain transitions
    if (newState == SUPERVISOR_DISCONNECTED || newState == SUPERVISOR_FAILSAFE) {
        _mapper.reset();
    }
    
    // Notify callbacks
    notifyStateCallbacks(oldState, newState);
}

void Supervisor::enterFailsafe(const char* reason) {
    Serial.printf("[Supervisor] Entering FAILSAFE: %s\n", reason);
    sendStop();
    transitionTo(SUPERVISOR_FAILSAFE);
}

void Supervisor::notifyStateCallbacks(SupervisorState oldState, SupervisorState newState) {
    for (uint8_t i = 0; i < _callbackCount; i++) {
        if (_stateCallbacks[i] != nullptr) {
            _stateCallbacks[i](oldState, newState);
        }
    }
}

// ---------------------------------------------------------------------------
// Public Accessors
// ---------------------------------------------------------------------------
bool Supervisor::isConnected() const {
    // Return true if any wheel is connected (supports partial connectivity)
    return bleAnyConnected();
}

uint32_t Supervisor::getTimeSinceLastInput() const {
    if (_lastInputTimeMs == 0) return 0;
    return millis() - _lastInputTimeMs;
}

uint32_t Supervisor::getTimeSinceLastLink() const {
    if (_lastLinkTimeMs == 0) return 0;
    return millis() - _lastLinkTimeMs;
}

bool Supervisor::isInputTimeout() const {
    return _lastInputTimeMs > 0 && (millis() - _lastInputTimeMs) > _config.inputTimeoutMs;
}

bool Supervisor::isLinkTimeout() const {
    return _lastLinkTimeMs > 0 && (millis() - _lastLinkTimeMs) > _config.linkTimeoutMs;
}

void Supervisor::notifyConnectionChange() {
    // Called when a wheel connects or disconnects
    // Check if we should transition based on connection state
    // Only failsafe if ALL wheels disconnect (partial connectivity supported)
    if (_state == SUPERVISOR_PAIRED || _state == SUPERVISOR_ARMED || _state == SUPERVISOR_DRIVING) {
        if (!bleAnyConnected()) {
            Serial.println("[Supervisor] All connections lost, entering failsafe");
            enterFailsafe("All wheels disconnected");
        }
    }
}
