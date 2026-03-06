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
    , _lastTelemetryPollMs(0)
    , _connectionRequested(false)
    , _lastLeftConnected(false)
    , _lastRightConnected(false)
    , _callbackCount(0)
{
    memset(_leftAddr, 0, sizeof(_leftAddr));
    memset(_rightAddr, 0, sizeof(_rightAddr));
    memset(_leftKey, 0, sizeof(_leftKey));
    memset(_rightKey, 0, sizeof(_rightKey));
    memset(_stateCallbacks, 0, sizeof(_stateCallbacks));
    memset(_wheelRetries, 0, sizeof(_wheelRetries));
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
        bleResetMotorWriteOk();   // clear any stale failure flag from prior session
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

    // Rate-limit the overall retry cycle
    if (now - _connectAttemptMs < _config.reconnectDelayMs) {
        return;
    }
    _connectAttemptMs = millis();

    if (debugFlags & DBG_BLE) {
        Serial.printf("[Supervisor] Target MACs: L=%s R=%s\n", _leftAddr, _rightAddr);
    }

    // -----------------------------------------------------------------------
    // Per-wheel connection attempts with individual retry budgets
    // Each wheel is tried independently; a failed wheel gets a hard state
    // reset (client nulled, all protocol fields cleared) before the next
    // attempt so there are no stale GATT/BLE resources carrying over.
    // -----------------------------------------------------------------------
    bool attemptedAny = false;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i))                              continue; // inactive in current WHEEL_MODE
        if (bleIsConnected(i))                             continue; // already up
        if (_wheelRetries[i] >= _config.maxReconnectAttempts) continue; // budget exhausted

        const char* wName = (i == WHEEL_LEFT) ? "left" : "right";
        Serial.printf("[Supervisor] Connecting %s wheel (attempt %d/%d)\n",
                      wName, _wheelRetries[i] + 1, _config.maxReconnectAttempts);

        bool ok = bleConnectWheel(i);

        if (!ok) {
            _wheelRetries[i]++;
            Serial.printf("[Supervisor] %s wheel failed - hard resetting state (retry %d/%d)\n",
                          wName, _wheelRetries[i], _config.maxReconnectAttempts);
            bleResetWheel(i); // hard state reset before next retry

            if (_wheelRetries[i] >= _config.maxReconnectAttempts) {
                Serial.printf("[Supervisor] %s wheel retry budget exhausted\n", wName);
            }
        }

        // Inter-wheel delay so the BLE stack can settle between connections
        if (i < WHEEL_COUNT - 1 && _wheelActive(i + 1) && !bleIsConnected(i + 1)) {
            delay(BLE_INTER_WHEEL_DELAY_MS);
        }

        attemptedAny = true;
    }

    // Allow async disconnect callbacks to complete after any attempt
    if (attemptedAny) {
        delay(100);
    }

    // -----------------------------------------------------------------------
    // Evaluate overall session outcome
    // -----------------------------------------------------------------------

    // All active wheels are either connected or have exhausted their budget.
    // Until that is true, stay in CONNECTING and let the next update handle
    // the remaining wheels.
    bool allDone = true;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        if (!bleIsConnected(i) && _wheelRetries[i] < _config.maxReconnectAttempts) {
            allDone = false;
            break;
        }
    }

    if (!allDone) {
        return; // still retries remaining for at least one wheel
    }

    bool anyConnected = bleAnyConnected();

    if (anyConnected) {
        if (bleAllConnected()) {
            Serial.println("[Supervisor] Connected successfully (all wheels)");
        } else {
            Serial.println("[Supervisor] Connected successfully (partial - some wheels unreachable)");
        }
        // Reset per-wheel budgets for the next connect session
        memset(_wheelRetries, 0, sizeof(_wheelRetries));

        // Initialize wheel connection tracking for monitoring
        _lastLeftConnected  = bleIsConnected(WHEEL_LEFT);
        _lastRightConnected = bleIsConnected(WHEEL_RIGHT);

        transitionTo(SUPERVISOR_PAIRED);
        _lastLinkTimeMs = millis();
    } else {
        Serial.println("[Supervisor] All per-wheel retry budgets exhausted - giving up");
        memset(_wheelRetries, 0, sizeof(_wheelRetries));
        transitionTo(SUPERVISOR_DISCONNECTED);
    }
}

void Supervisor::handlePaired() {
    // Monitor individual wheel connection state changes
    bool leftConnected = bleIsConnected(WHEEL_LEFT);
    bool rightConnected = bleIsConnected(WHEEL_RIGHT);
    
    // Detect and log wheel disconnections
    if (_lastLeftConnected && !leftConnected) {
        Serial.println("[Supervisor] WARNING: Left wheel disconnected in PAIRED state");
    }
    if (_lastRightConnected && !rightConnected) {
        Serial.println("[Supervisor] WARNING: Right wheel disconnected in PAIRED state");
    }
    
    // Detect and log wheel reconnections
    if (!_lastLeftConnected && leftConnected) {
        Serial.println("[Supervisor] Left wheel reconnected in PAIRED state");
    }
    if (!_lastRightConnected && rightConnected) {
        Serial.println("[Supervisor] Right wheel reconnected in PAIRED state");
    }
    
    // Update tracking
    _lastLeftConnected = leftConnected;
    _lastRightConnected = rightConnected;
    
    // Connected but not armed - just maintain connection
    // Check if we lost ALL connections (partial connectivity OK)
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections in PAIRED state");
        transitionTo(SUPERVISOR_DISCONNECTED);
        return;
    }

    pollTelemetry();

#ifdef AUTO_ARM_ON_CONNECT
    // Auto-arm: skip explicit requestArm() and go straight to ARMED.
    // The deadman check in processInput() still prevents unintended movement.
    Serial.println("[Supervisor] AUTO_ARM_ON_CONNECT: arming automatically");
    bleResetMotorWriteOk();
    transitionTo(SUPERVISOR_ARMED);
#endif
    // Without AUTO_ARM_ON_CONNECT: wait for explicit requestArm() call
    // (serial 'arm' command or future hardware button)
}

void Supervisor::handleArmed() {
    // Ready to drive, waiting for input
    // Check if we lost ALL connections (partial connectivity OK)
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections in ARMED state");
        enterFailsafe("Connection lost");
        return;
    }
    pollTelemetry();
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
    pollTelemetry();
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
// Telemetry Polling
// ---------------------------------------------------------------------------
void Supervisor::pollTelemetry() {
    uint32_t now = millis();
    if (now - _lastTelemetryPollMs < _config.telemetryPollIntervalMs) return;
    _lastTelemetryPollMs = now;

    // Fire async requests - responses arrive via BLE notify callbacks
    bleRequestSOC();
    bleRequestFirmwareVersion();
    bleRequestCruiseValues();

    // Pull whatever the cache already has into VehicleState
    _vehicleState.batteryLeft  = bleGetBattery(WHEEL_LEFT);
    _vehicleState.batteryRight = bleGetBattery(WHEEL_RIGHT);

    float dist = bleGetDistanceKm(WHEEL_LEFT);
    if (dist < 0.0f) dist = bleGetDistanceKm(WHEEL_RIGHT); // fallback
    if (dist >= 0.0f) _vehicleState.distanceKm = dist;

    _vehicleState.timestamp = now;

    // Low battery check
    int minBatt = _vehicleState.batteryMin();
    bool wasLow = _vehicleState.lowBattery;
    _vehicleState.lowBattery = (minBatt >= 0 && minBatt < (int)_config.lowBatteryThreshold);
    if (_vehicleState.lowBattery && !wasLow) {
        Serial.printf("[Supervisor] LOW BATTERY WARNING: %d%% (threshold: %d%%)\n",
                      minBatt, _config.lowBatteryThreshold);
    } else if (!_vehicleState.lowBattery && wasLow) {
        Serial.println("[Supervisor] Battery level restored above threshold.");
    }
}

// ---------------------------------------------------------------------------
// Watchdogs
// ---------------------------------------------------------------------------
void Supervisor::checkWatchdogs() {
    uint32_t now = millis();

    // BLE motor write failure (async feedback from motor task)
    // Only check in DRIVING state - spurious failures during arm transition
    // should not immediately trigger failsafe.
    if (_state == SUPERVISOR_DRIVING && !bleLastMotorWriteOk()) {
        Serial.println("[Supervisor] Motor write failure detected by watchdog");
        enterFailsafe("BLE write error");
        return;
    }

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
    // Post to the motor write task queue (non-blocking; always returns true).
    // Write failures are reported asynchronously via bleLastMotorWriteOk()
    // and caught by checkWatchdogs() on the next update cycle.
    bleSendMotorCommand((float)cmd.leftSpeed, (float)cmd.rightSpeed);
    _lastLinkTimeMs = millis();
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
    // Only attempt a stop write if we actually have a connection - avoids
    // blocking/re-entering on a dead BLE link that just caused the failsafe.
    if (bleAnyConnected()) {
        sendStop();
    }
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
