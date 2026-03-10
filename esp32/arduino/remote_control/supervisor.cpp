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
    , _activateHoldStartMs(0)
    , _idleHoldStartMs(0)
    , _armedEntryMs(0)
    , _partialReconnect(false)
    , _connectionRequested(false)
    , _lastLeftConnected(false)
    , _lastRightConnected(false)
    , _callbackCount(0)
    , _connectTask(nullptr)
    , _connectDone(false)
    , _connectAbort(false)
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
        if (control.deadman && !control.isNeutral()) {
            // Joystick is out of deadzone: start or check the activate hold timer.
            // Require JS_ACTIVATE_HOLD_MS of continuous out-of-deadzone before
            // transitioning to DRIVING, so ADC noise at the boundary can't jiggle the state.
            if (_activateHoldStartMs == 0) {
                _activateHoldStartMs = millis();
            } else if (millis() - _activateHoldStartMs >= JS_ACTIVATE_HOLD_MS) {
                _activateHoldStartMs = 0;
                _idleHoldStartMs     = 0;
                transitionTo(SUPERVISOR_DRIVING);
            }
            // Not yet held long enough - send stop and wait
            sendStop();
        } else {
            // Back in deadzone: reset the activate timer
            _activateHoldStartMs = 0;
            sendStop();
        }
    }
    else if (_state == SUPERVISOR_DRIVING) {
        // Check if user released controls
        if (!control.deadman || control.isNeutral()) {
            // Joystick returned to deadzone: start or check the idle hold timer.
            // Require JS_IDLE_HOLD_MS of continuous in-deadzone before returning to ARMED,
            // so a brief bump at center doesn't drop out of DRIVING.
            if (_idleHoldStartMs == 0) {
                _idleHoldStartMs = millis();
            }
            sendStop();
            if (millis() - _idleHoldStartMs >= JS_IDLE_HOLD_MS) {
                if (debugFlags & DBG_STATE) {
                    Serial.println("[Supervisor] User released controls, returning to ARMED");
                }
                _idleHoldStartMs     = 0;
                _activateHoldStartMs = 0;
                transitionTo(SUPERVISOR_ARMED);
            }
            return;
        }

        // Joystick still active: reset the idle timer
        _idleHoldStartMs = 0;
        
        // Map to command
        CommandFrame cmd;
        bool valid = _mapper.map(control, cmd);
        
        // Mapper enforces safety - check if command is valid
        if (!valid || !control.isSafe()) {
            if (debugFlags & DBG_STATE) {
                Serial.println("[Supervisor] Mapper rejected input (safety violation)");
            }
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

// ---------------------------------------------------------------------------
// Connect Task - runs on Core 0 so blocking BLE ops don't freeze loop()
// ---------------------------------------------------------------------------
void Supervisor::_sConnectTask(void* pv) {
    static_cast<Supervisor*>(pv)->_connectTaskBody();
}

void Supervisor::_connectTaskBody() {
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (_connectAbort)                                     break;
        if (!_wheelActive(i))                                  continue;
        if (bleIsConnected(i))                                 continue;
        if (_wheelRetries[i] >= _config.maxReconnectAttempts)  continue;

        const char* wName = (i == WHEEL_LEFT) ? "left" : "right";
        Serial.printf("[Supervisor] Connecting %s wheel (attempt %d/%d)\n",
                      wName, _wheelRetries[i] + 1, _config.maxReconnectAttempts);

        bool ok = bleConnectWheel(i);

        if (!ok) {
            _wheelRetries[i]++;
            Serial.printf("[Supervisor] %s wheel failed (retry %d/%d)\n",
                          wName, _wheelRetries[i], _config.maxReconnectAttempts);
            bleResetWheel(i);
        }

        // Let the BLE stack settle between wheels
        if (!_connectAbort && i < WHEEL_COUNT - 1 &&
            _wheelActive(i + 1) && !bleIsConnected(i + 1)) {
            vTaskDelay(pdMS_TO_TICKS(BLE_INTER_WHEEL_DELAY_MS));
        }
    }

    // Allow async disconnect callbacks to settle
    if (!_connectAbort) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Signal done - Core 1 will clear _connectTask (don't touch it here to
    // avoid the race window between nulling the handle and signalling done).
    _connectDone = true;
    vTaskDelete(nullptr);
}

void Supervisor::handleConnecting() {
    // -----------------------------------------------------------------------
    // Task just finished? Process result first (done flag is the authority).
    // Core 1 owns clearing _connectTask so there is no "not running, not done"
    // race window that could cause a second task to be spawned prematurely.
    // -----------------------------------------------------------------------
    if (_connectDone) {
        _connectTask = nullptr;   // task has already self-deleted; safe to clear
        _connectDone = false;

        // Check if any wheel still has retry budget remaining
        bool allDone = true;
        for (int i = 0; i < WHEEL_COUNT; i++) {
            if (!_wheelActive(i)) continue;
            if (!bleIsConnected(i) && _wheelRetries[i] < _config.maxReconnectAttempts) {
                allDone = false;
                break;
            }
        }

        if (!allDone) {
            // Still have retry budget - task will be re-spawned after delay
            return;
        }

        // All budgets consumed (or all wheels connected). Decide outcome.
        if (bleAllConnected()) {
            Serial.println("[Supervisor] Connected successfully (all wheels)");
            _partialReconnect = false;
            memset(_wheelRetries, 0, sizeof(_wheelRetries));
            _lastLeftConnected  = bleIsConnected(WHEEL_LEFT);
            _lastRightConnected = bleIsConnected(WHEEL_RIGHT);
            transitionTo(SUPERVISOR_PAIRED);
            _lastLinkTimeMs = millis();
            return;
        }

        if (_partialReconnect) {
            // The dropped wheel couldn't be recovered - escalate to a full
            // disconnect + reconnect of all wheels.
            Serial.println("[Supervisor] Partial reconnect failed - doing full reconnect");
            bleDisconnect();
            _partialReconnect = false;
            memset(_wheelRetries, 0, sizeof(_wheelRetries));
            _connectAttemptMs = 0;   // spawn next task immediately
            return;                  // stay in CONNECTING
        }

        // Not a partial reconnect - accept whatever we got
        if (bleAnyConnected()) {
            Serial.println("[Supervisor] Connected successfully (partial)");
            memset(_wheelRetries, 0, sizeof(_wheelRetries));
            _lastLeftConnected  = bleIsConnected(WHEEL_LEFT);
            _lastRightConnected = bleIsConnected(WHEEL_RIGHT);
            transitionTo(SUPERVISOR_PAIRED);
            _lastLinkTimeMs = millis();
        } else {
            Serial.println("[Supervisor] All per-wheel retry budgets exhausted - giving up");
            memset(_wheelRetries, 0, sizeof(_wheelRetries));
            transitionTo(SUPERVISOR_DISCONNECTED);
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Task still in flight? Wait - Core 1 returns immediately.
    // -----------------------------------------------------------------------
    if (_connectTask != nullptr) {
        return;
    }

    // -----------------------------------------------------------------------
    // Rate-limit between task spawns
    // -----------------------------------------------------------------------
    uint32_t now = millis();
    if (now - _connectAttemptMs < _config.reconnectDelayMs) {
        return;
    }
    _connectAttemptMs = now;

    // Check if any wheel still needs an attempt
    bool anyPending = false;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        if (!_wheelActive(i)) continue;
        if (!bleIsConnected(i) && _wheelRetries[i] < _config.maxReconnectAttempts) {
            anyPending = true;
            break;
        }
    }
    if (!anyPending) {
        // All budgets gone but we arrived here without a done signal - clean up
        bool anyConnected = bleAnyConnected();
        memset(_wheelRetries, 0, sizeof(_wheelRetries));
        if (anyConnected) {
            transitionTo(SUPERVISOR_PAIRED);
            _lastLinkTimeMs = millis();
        } else {
            transitionTo(SUPERVISOR_DISCONNECTED);
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Spawn connect task on Core 0 (BLE stack core)
    // -----------------------------------------------------------------------
    _connectAbort = false;
    _connectDone  = false;
    BaseType_t rc = xTaskCreatePinnedToCore(
        _sConnectTask, "ble_connect", 8192, this, 4, &_connectTask, 0);
    if (rc != pdPASS) {
        Serial.println("[Supervisor] ERROR: failed to spawn connect task");
        _connectTask = nullptr;
    } else {
        if (debugFlags & DBG_BLE) {
            Serial.println("[Supervisor] Connect task spawned on Core 0");
        }
    }
}

void Supervisor::_triggerPartialReconnect() {
    memset(_wheelRetries, 0, sizeof(_wheelRetries));
    _partialReconnect = true;
    _connectAttemptMs = 0;  // reconnect immediately, no rate-limit delay
    transitionTo(SUPERVISOR_CONNECTING);
}

void Supervisor::handlePaired() {
    // Monitor individual wheel connection state changes
    bool leftConnected  = bleIsConnected(WHEEL_LEFT);
    bool rightConnected = bleIsConnected(WHEEL_RIGHT);

    bool leftDrop  = _wheelActive(WHEEL_LEFT)  && _lastLeftConnected  && !leftConnected;
    bool rightDrop = _wheelActive(WHEEL_RIGHT) && _lastRightConnected && !rightConnected;

    if (leftDrop || rightDrop) {
        if (leftDrop)  Serial.println("[Supervisor] Left wheel dropped in PAIRED - reconnecting");
        if (rightDrop) Serial.println("[Supervisor] Right wheel dropped in PAIRED - reconnecting");
        _triggerPartialReconnect();
        return;
    }

    // Detect and log wheel reconnections
    if (!_lastLeftConnected && leftConnected)
        Serial.println("[Supervisor] Left wheel reconnected in PAIRED state");
    if (!_lastRightConnected && rightConnected)
        Serial.println("[Supervisor] Right wheel reconnected in PAIRED state");

    _lastLeftConnected  = leftConnected;
    _lastRightConnected = rightConnected;

    // Full loss (both gone) - reconnect covers this too but log it explicitly
    if (!bleAnyConnected()) {
        Serial.println("[Supervisor] Lost all connections in PAIRED state");
        _triggerPartialReconnect();
        return;
    }

    pollTelemetry();

    // Without AUTO_ARM_ON_CONNECT: wait for explicit requestArm() call
    // (serial 'arm' command or future hardware button)
}

void Supervisor::handleArmed() {
    // Per-wheel drop detection - reconnect the dropped wheel automatically
    bool leftOk  = !_wheelActive(WHEEL_LEFT)  || bleIsConnected(WHEEL_LEFT);
    bool rightOk = !_wheelActive(WHEEL_RIGHT) || bleIsConnected(WHEEL_RIGHT);
    if (!leftOk || !rightOk) {
        if (!leftOk)  Serial.println("[Supervisor] Left wheel dropped in ARMED - reconnecting");
        if (!rightOk) Serial.println("[Supervisor] Right wheel dropped in ARMED - reconnecting");
        sendStop();
        _triggerPartialReconnect();
        return;
    }
    // No telemetry polling here: bleRequest*() calls _sendCommand() on Core 1
    // while the motor task (_bleMotorTask) may be writing on Core 0.  Even with
    // the mutex, isConnected() inside the stack can deadlock when rc=-1 fires.
    // Telemetry is polled safely in handlePaired() before the motor task is active.
}

void Supervisor::handleDriving() {
    // Per-wheel drop detection - stop and reconnect the dropped wheel automatically
    bool leftOk  = !_wheelActive(WHEEL_LEFT)  || bleIsConnected(WHEEL_LEFT);
    bool rightOk = !_wheelActive(WHEEL_RIGHT) || bleIsConnected(WHEEL_RIGHT);
    if (!leftOk || !rightOk) {
        if (!leftOk)  Serial.println("[Supervisor] Left wheel dropped while DRIVING - stopping and reconnecting");
        if (!rightOk) Serial.println("[Supervisor] Right wheel dropped while DRIVING - stopping and reconnecting");
        sendStop();
        _triggerPartialReconnect();
        return;
    }
    // No telemetry polling while driving ─ same reason as handleArmed().
    // Input processing happens in processInput()
    // Watchdogs are checked in main update loop
}

void Supervisor::handleFailsafe() {
    // Emergency state - send stop commands periodically.
    // Stay in FAILSAFE until the user explicitly clears with 'reset'.
    // Do NOT auto-transition to DISCONNECTED: that would race with the user
    // trying to type 'reset', and it makes E-Stop non-recoverable when the
    // BLE link drops during the stop-write flood.
    sendStop();
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

    // Input watchdog - severity depends on state:
    //   DRIVING: failsafe immediately (motor commands were flowing, now gone)
    //   ARMED:   graceful disarm to PAIRED after armIdleTimeoutMs of total idle
    if (_state == SUPERVISOR_DRIVING) {
        if (_lastInputTimeMs > 0 && now - _lastInputTimeMs > _config.inputTimeoutMs) {
            Serial.println("[Supervisor] Input watchdog timeout while DRIVING");
            enterFailsafe("Input timeout");
            return;
        }
    } else if (_state == SUPERVISOR_ARMED) {
        // Use armIdleTimeoutMs measured from arm entry (or last input, whichever
        // is more recent) so the user can idle in ARMED intentionally.
        uint32_t idleRef = (_lastInputTimeMs > _armedEntryMs) ? _lastInputTimeMs : _armedEntryMs;
        if (now - idleRef > _config.armIdleTimeoutMs) {
            Serial.println("[Supervisor] Arm idle timeout - disarming to PAIRED");
            transitionTo(SUPERVISOR_PAIRED);
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
    if (debugFlags & DBG_STATE) {
        Serial.printf("[Supervisor] State transition: %s -> %s\n",
                      supervisorStateToString(oldState),
                      supervisorStateToString(newState));
    }
    
    _state = newState;
    
    // If we're leaving CONNECTING, signal any in-flight connect task to abort
    // and clear the done flag so stale results aren't processed later.
    if (oldState == SUPERVISOR_CONNECTING) {
        _connectAbort = true;
        _connectDone  = false;
        // _connectTask handle is cleared by the task itself before it deletes.
        // If it hasn't run yet it will see _connectAbort=true and skip all wheels.
    }

    // Reset mapper state on certain transitions
    if (newState == SUPERVISOR_DISCONNECTED || newState == SUPERVISOR_FAILSAFE) {
        _mapper.reset();
    }

    // On entry to PAIRED: auto-arm once if configured (fires exactly once per
    // PAIRED entry, not every loop tick as with the old handlePaired() approach).
    if (newState == SUPERVISOR_PAIRED) {
#ifdef AUTO_ARM_ON_CONNECT
        Serial.println("[Supervisor] AUTO_ARM_ON_CONNECT: arming automatically");
        bleResetMotorWriteOk();
        transitionTo(SUPERVISOR_ARMED);
        return;  // transitionTo() will handle ARMED entry setup below
#endif
    }

    // Reset joystick hold timers on any entry to ARMED so stale timer values
    // from a previous drive session can't cause an immediate ARMED->DRIVING
    // transition (e.g. after reconnect or reset).
    if (newState == SUPERVISOR_ARMED) {
        _activateHoldStartMs = 0;
        _idleHoldStartMs     = 0;
        _armedEntryMs        = millis();
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
