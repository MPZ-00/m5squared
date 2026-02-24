/*
 * remote_control.ino - ESP32 M25 Remote Control (Test Setup)
 *
 * One-handed remote control for Alber e-motion M25 power-assist wheels.
 * Test-setup variant: simple push buttons, single-color LEDs, no display.
 * Pin assignments and wiring details: device_config.h
 *
 * State machine:
 *   BOOT       -> calibrate joystick, self-test LEDs, power-on safety check
 *   CONNECTING -> attempt BLE connection to both wheels
 *   READY      -> connected, joystick in deadzone, no motor commands
 *   OPERATING  -> joystick active, sending motor commands at 20 Hz
 *   ERROR      -> E-stop pressed / watchdog timeout / connection lost
 *                 Press E-stop button again to reset back to CONNECTING.
 */

#include <Arduino.h>
#include "device_config.h"
#include "joystick.h"
#include "led_control.h"
#include "button.h"
#include "motor_control.h"
#include "m25_ble.h"
#include "serial_commands.h"

// SystemState enum is defined in device_config.h
static SystemState sysState = STATE_BOOT;

// ---------------------------------------------------------------------------
// Persistent control state
// ---------------------------------------------------------------------------
static uint8_t assistLevel  = ASSIST_INDOOR;   // boot default: indoor
static bool    hillHoldOn   = false;
#ifdef ENABLE_BATTERY_MONITOR
static int     batteryPct   = 100;
#endif

// Watchdog: tracks last non-zero joystick event
static uint32_t lastActiveMs      = 0;
static bool     watchdogWarnShown = false;

// Command rate limiter
static uint32_t lastCommandSentMs = 0;

// Joystick transition hold timers (hysteresis, see JS_ACTIVATE/IDLE_HOLD_MS)
static uint32_t jsActiveSinceMs = 0;   // when joystick first left deadzone
static uint32_t jsIdleSinceMs   = 0;   // when joystick first entered deadzone

// Reconnect rate limiter
static uint32_t lastBleTickMs = 0;

// Loop heartbeat (debug - shows loop() is running)
static uint32_t lastHeartbeatMs = 0;
static uint32_t loopCounter = 0;

// ERROR state: limit BLE stop retries so writeValue() can't block loop() forever.
// We send up to BLE_ERROR_STOP_TRIES stops after entering ERROR, then give up.
// The wheel's own remote-control watchdog will cut power if it hears nothing.
#define BLE_ERROR_STOP_TRIES 3
static uint8_t _errorStopsSent = 0;

#ifdef ENABLE_BATTERY_MONITOR
// Battery read interval
static uint32_t lastBatteryMs = 0;
#define BATTERY_READ_INTERVAL_MS 10000
#endif

#ifdef ENABLE_BATTERY_MONITOR
// Battery voltage measurement
// Full formula: V_bat = V_adc * 2  (1:1 divider, 3.3 V ref)
//               pct   = (adc - BATT_ADC_EMPTY) * 100
//                        / (BATT_ADC_FULL - BATT_ADC_EMPTY)
static int readBatteryPct() {
    int raw = analogRead(BATTERY_ADC_PIN);
    int pct = (int)(
        (float)(raw - BATT_ADC_EMPTY) * 100.0f
        / (float)(BATT_ADC_FULL - BATT_ADC_EMPTY)
    );
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}
#endif

// ---------------------------------------------------------------------------
// Transition helpers
// ---------------------------------------------------------------------------
static void enterConnecting() {
    sysState = STATE_CONNECTING;
    ledSetStatus(LED_BLINK_SLOW);
    ledSetBle(false);
    Serial.println("[State] -> CONNECTING");
    if (debugFlags & DBG_STATE) {
        Serial.println("[State] Initiating BLE connection sequence...");
    }
    bleConnect();
}

static void enterReady() {
    sysState = STATE_READY;
    ledSetStatus(LED_OFF);
    ledSetBle(true);
    lastActiveMs      = millis();
    watchdogWarnShown = false;
    bleSendStop();   // one explicit stop on transition; resets the keepalive timer
    lastCommandSentMs = millis();
    Serial.println("[State] -> READY");
    if (debugFlags & DBG_STATE) {
        Serial.println("[State] Wheels ready, joystick monitoring active");
    }
}

static void enterOperating() {
    sysState = STATE_OPERATING;
    lastActiveMs      = millis();
    watchdogWarnShown = false;
    Serial.println("[State] -> OPERATING");
    if (debugFlags & DBG_STATE) {
        Serial.println("[State] Joystick active, motor commands enabled");
    }
}

static void enterError(const char* reason) {
    sysState = STATE_ERROR;
    jsActiveSinceMs = 0;
    jsIdleSinceMs   = 0;
    _errorStopsSent = 0;   // reset retry counter
    ledSetStatus(LED_BLINK_FAST);
    ledSetBle(false);
    bleSendStop();
    _errorStopsSent++;
    Serial.printf("[State] -> ERROR  reason: %s\n", reason);
    if (debugFlags & DBG_STATE) {
        Serial.println("[State] Motors stopped, press E-stop to reset");
    }
}

// ---------------------------------------------------------------------------
// Power-on safety check (blocking)
// Verifies joystick is centered before enabling remote mode.
// Send "confirm" or "skip" over serial to bypass during development.
// ---------------------------------------------------------------------------
static bool powerOnSafetyCheck() {
#ifdef NO_JOYSTICK
    Serial.println("[Safety] NO_JOYSTICK: press E-stop to confirm, or send 'confirm' via serial");
#else
    Serial.println("[Safety] Power-on check: center joystick, hold 2 s, then press E-stop");
    Serial.println("[Safety] (Send 'confirm' via serial to bypass)");
#endif

    uint32_t centeredSince = 0;
    bool     centered      = false;
    char     scBuf[16];
    uint8_t  scLen         = 0;

    while (true) {
        buttonsTick();
        JoystickNorm js = joystickRead();

        // --- Serial bypass ---
        while (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                scBuf[scLen] = '\0';
                if (strcmp(scBuf, "confirm") == 0 || strcmp(scBuf, "skip") == 0) {
                    Serial.println("[Safety] Check BYPASSED via serial");
                    return true;
                }
                scLen = 0;
            } else if (scLen < (uint8_t)(sizeof(scBuf) - 1)) {
                scBuf[scLen++] = c;
            }
        }

#ifndef NO_JOYSTICK
        // --- Joystick centering feedback (skipped when no joystick) ---
        if (js.inDeadzone) {
            if (!centered) {
                centered      = true;
                centeredSince = millis();
                Serial.println("[Safety] Joystick centered - hold 2 s then press E-stop");
            }
        } else {
            if (centered) {
                centered = false;
                Serial.println("[Safety] Joystick off-center, re-center and hold");
            }
        }
        bool longEnough = centered && ((millis() - centeredSince) >= 2000);
        if (longEnough && btnEstop.wasPressed()) {
            Serial.println("[Safety] Check PASSED");
            return true;
        }
#else
        // NO_JOYSTICK: just wait for a single E-stop press
        if (btnEstop.wasPressed()) {
            Serial.println("[Safety] Check PASSED (no-joystick mode)");
            return true;
        }
#endif

        ledTick();
        delay(10);
    }
    return false; // unreachable
}

// ---------------------------------------------------------------------------
// Serial command context
// Fill in after all static variables and helper functions are declared.
// ---------------------------------------------------------------------------
static SerialContext _serialCtx = {
    &sysState,
    &assistLevel,
    &hillHoldOn,
    enterConnecting,
    enterError,
    joystickRecalibrate,
#ifdef ENABLE_BATTERY_MONITOR
    &batteryPct,
#endif
};

// ---------------------------------------------------------------------------
// Arduino setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n[Boot] M25 Remote Control starting...");

    // Peripheral init
    ledInit();
    ledStartupTest();

    buttonsInit();
    joystickInit();

#ifdef ENABLE_BATTERY_MONITOR
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    batteryPct = readBatteryPct();
    ledSetBattery(batteryPct);
    Serial.printf("[Boot] Battery: %d %%\n", batteryPct);
#endif

    // Block on safety check before BLE init (saves radio power during boot)
    sysState = STATE_BOOT;
    powerOnSafetyCheck();

    // BLE init and connect
    bleInit("M25-Remote");
    ledSetAssistLevel(assistLevel);
    ledSetHillHold(hillHoldOn);

    enterConnecting();
    serialInit(_serialCtx);
}

// ---------------------------------------------------------------------------
// Arduino loop
// ---------------------------------------------------------------------------
void loop() {
    uint32_t now = millis();
    loopCounter++;
    
    const char* stateName = (sysState == STATE_BOOT) ? "BOOT" :
                       (sysState == STATE_CONNECTING) ? "CONNECTING" :
                       (sysState == STATE_READY) ? "READY" :
                       (sysState == STATE_OPERATING) ? "OPERATING" :
                       (sysState == STATE_ERROR) ? "ERROR" : "?";

    // --- Heartbeat (every 5 seconds, proves loop is running) ---
    if (now - lastHeartbeatMs >= 5000 && debugFlags & DBG_HEARTBEAT) {
        lastHeartbeatMs = now;
        Serial.printf("[Heartbeat] loop running, count=%u  state=%s\n", loopCounter, stateName);
    }

    // --- Serial command processing (input + live debug output) ---
    serialTick(_serialCtx);

    // --- Update input drivers ---
    buttonsTick();
    JoystickNorm js = joystickRead();

    // --- Button press detection & logging (before action, so we see everything) ---
    bool estopPressed    = btnEstop.wasPressed();
    bool hillHoldPressed = btnHillHold.wasPressed();
    bool assistPressed   = btnAssist.wasPressed();

    if (debugFlags & DBG_BUTTONS) {
        if (estopPressed) {
            Serial.printf("[Button] E-STOP pressed  (state=%s)\n", stateName);
        }
        if (hillHoldPressed) {
            Serial.printf("[Button] HILL-HOLD pressed  (state=%s)\n", stateName);
        }
        if (assistPressed) {
            Serial.printf("[Button] ASSIST pressed  (state=%s)\n", stateName);
        }
    }

    // --- Emergency stop: highest priority, any state ---
    if (estopPressed) {
        if (sysState == STATE_ERROR) {
            // Second press: reset from error
            Serial.println("[E-Stop] Reset: reconnecting...");
            enterConnecting();
        } else {
            enterError("E-stop pressed");
        }
        // Do not process other inputs this iteration
        ledTick();
        return;
    }

    // --- State-specific logic ---
    switch (sysState) {

        // ---- CONNECTING ----
        case STATE_CONNECTING:
            // Buttons ignored during connection attempt
            if (hillHoldPressed || assistPressed) {
                Serial.println("[Button] Ignored - still connecting to wheels");
            }

            if (bleAllConnected()) {
                enterReady();
            } else {
                // Reconnect attempts are handled by bleTick() below
                ledSetStatus(LED_BLINK_SLOW);
            }
            break;

        // ---- READY ----
        case STATE_READY: {
            // Dual button press: force BLE reconnect (hill-hold + assist simultaneously)
            if (hillHoldPressed && assistPressed) {
                Serial.println("[Button] Dual press: forcing BLE reconnect...");
                enterConnecting();
                break;
            }

            // Hill hold toggle (allowed when motors stopped)
            if (hillHoldPressed) {
                hillHoldOn = !hillHoldOn;
                ledSetHillHold(hillHoldOn);
                bleSendHillHold(hillHoldOn);
                Serial.printf("[HillHold] %s\n", hillHoldOn ? "ON" : "OFF");
            }

            // Assist level cycle (allowed when motors stopped)
            if (assistPressed) {
                assistLevel = (assistLevel + 1) % ASSIST_COUNT;
                ledSetAssistLevel(assistLevel);
                bleSendAssistLevel(assistLevel);
                Serial.printf("[Assist] -> %s  (Vmax fwd %d %%)\n",
                              assistConfigs[assistLevel].name,
                              assistConfigs[assistLevel].vmaxForward);
            }

            // Connection loss check
            if (!bleAnyConnected()) {
                enterError("Connection lost in READY state");
                break;
            }

            // Joystick activated -> OPERATING (hold JS_ACTIVATE_HOLD_MS outside deadzone)
            if (!js.inDeadzone) {
                if (jsActiveSinceMs == 0) jsActiveSinceMs = now;
                if (now - jsActiveSinceMs >= JS_ACTIVATE_HOLD_MS) {
                    jsActiveSinceMs = 0;
                    jsIdleSinceMs   = 0;
                    enterOperating();
                    // Fall through to OPERATING to send first command immediately
                } else {
                    break;   // hold not yet met, stay in READY
                }
            } else {
                jsActiveSinceMs = 0;   // joystick back in deadzone, reset hold timer
                // Periodic keepalive stop in READY state.
                // Rate is deliberately slow (500 ms) to avoid saturating the
                // BLE TX queue, which would block writeValue() and freeze loop().
                // The initial stop is sent by enterReady() on state entry.
                if (now - lastCommandSentMs >= 500) {
                    bleSendStop();
                    lastCommandSentMs = now;
                }
                break;
            }
            // intentional fall-through to OPERATING on first active frame
        }

        // ---- OPERATING ----
        case STATE_OPERATING: {
            // Hill hold NOT allowed while wheels are moving
            if (hillHoldPressed) {
                Serial.println("[HillHold] Ignored - motors active");
            }

            // Assist level NOT allowed while wheels are moving
            if (assistPressed) {
                Serial.println("[Assist] Ignored - motors active, stop first");
            }

            // Connection loss check
            if (!bleAnyConnected()) {
                enterError("Connection lost while OPERATING");
                break;
            }

            // Watchdog: track last non-zero input
            if (!js.inDeadzone) {
                lastActiveMs      = now;
                watchdogWarnShown = false;
                jsIdleSinceMs     = 0;   // joystick active again, reset hold timer
            } else {
                // Joystick returned to center -> back to READY after hold period
                if (jsIdleSinceMs == 0) jsIdleSinceMs = now;
                if (now - jsIdleSinceMs >= JS_IDLE_HOLD_MS) {
                    jsIdleSinceMs   = 0;
                    jsActiveSinceMs = 0;
                    enterReady();
                    break;
                }
            }

            // Watchdog warning and timeout
            // Only active when ENABLE_IDLE_WATCHDOG is set (self-centering joystick).
            // With a potentiometer the user holds positions indefinitely - the watchdog
            // would fire every 5 s, so it is disabled by default.
#ifdef ENABLE_IDLE_WATCHDOG
            uint32_t idle = now - lastActiveMs;
            if (idle >= WATCHDOG_TIMEOUT_MS) {
                enterError("Watchdog timeout - no joystick input");
                break;
            } else if (idle >= WATCHDOG_WARN_MS && !watchdogWarnShown) {
                watchdogWarnShown = true;
                Serial.println("[Watchdog] WARNING: joystick inactive > 3 s");
                ledSetStatus(LED_BLINK_FAST);
            } else if (idle < WATCHDOG_WARN_MS) {
                ledSetStatus(LED_OFF);
            }
#else
            ledSetStatus(LED_OFF);
#endif

            // Send motor commands at 20 Hz
            if (now - lastCommandSentMs >= COMMAND_RATE_MS) {
                MotorCommand cmd = joystickToMotorCommand(js, assistLevel);
                if (cmd.isStop) {
                    bleSendStop();
                } else {
                    bleSendMotorCommand(cmd.leftSpeed, cmd.rightSpeed);
                    if (debugFlags & DBG_MOTOR) {
                        printMotorCommand(cmd);
                    }
                }
                lastCommandSentMs = now;
            }
            break;
        }

        // ---- ERROR ----
        case STATE_ERROR:
            // E-stop handling already at top of loop.
            // Send a few more stop retries in case the first one was lost,
            // then stop trying - repeated writeValue() calls can block loop()
            // if the BLE TX queue fills or the connection is stressed.
            // The wheel's own remote-control watchdog will cut power independently.
            if (_errorStopsSent < BLE_ERROR_STOP_TRIES &&
                now - lastCommandSentMs >= 200) {
                if (bleAnyConnected()) bleSendStop();
                _errorStopsSent++;
                lastCommandSentMs = now;
            }
            break;

        // ---- BOOT (shouldn't reach here in loop) ----
        case STATE_BOOT:
        default:
            break;
    }

    // --- BLE reconnect tick (runs every BLE_RECONNECT_DELAY_MS) ---
    if (now - lastBleTickMs >= 1000) {
        lastBleTickMs = now;
        if (sysState == STATE_CONNECTING) {
            bleTick();
        }
    }

    // --- Battery monitoring (every 10 s) ---
#ifdef ENABLE_BATTERY_MONITOR
    if (now - lastBatteryMs >= BATTERY_READ_INTERVAL_MS) {
        lastBatteryMs = now;
        batteryPct    = readBatteryPct();
        ledSetBattery(batteryPct);
        Serial.printf("[Battery] %d %%\n", batteryPct);

        // Critical battery: force safe shutdown
        if (batteryPct <= BATT_AUTO_OFF_PCT && sysState != STATE_ERROR) {
            Serial.println("[Battery] CRITICAL - forcing disconnect");
            bleSendStop();
            bleDisconnect();
            enterError("Battery critical - auto shutdown");
        }
    }
#endif

    // --- LED tick (handles blinking) ---
    ledTick();
}
