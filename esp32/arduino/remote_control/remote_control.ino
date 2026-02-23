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

// Reconnect rate limiter
static uint32_t lastBleTickMs = 0;

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
    bleConnect();
}

static void enterReady() {
    sysState = STATE_READY;
    ledSetStatus(LED_ON);
    ledSetBle(true);
    lastActiveMs      = millis();
    watchdogWarnShown = false;
    Serial.println("[State] -> READY");
}

static void enterOperating() {
    sysState = STATE_OPERATING;
    lastActiveMs      = millis();
    watchdogWarnShown = false;
}

static void enterError(const char* reason) {
    sysState = STATE_ERROR;
    ledSetStatus(LED_BLINK_FAST);
    ledSetBle(false);
    bleSendStop();
    Serial.printf("[State] -> ERROR  reason: %s\n", reason);
}

// ---------------------------------------------------------------------------
// Power-on safety check (blocking)
// Verifies joystick is centered before enabling remote mode.
// Send "confirm" or "skip" over serial to bypass during development.
// ---------------------------------------------------------------------------
static bool powerOnSafetyCheck() {
    Serial.println("[Safety] Power-on check: center joystick, hold 2 s, then press E-stop");
    Serial.println("[Safety] (Send 'confirm' via serial to bypass)");

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

        // --- Joystick centering feedback ---
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

        // --- Confirm: joystick centered >= 2 s AND E-stop pressed ---
        bool longEnough = centered && ((millis() - centeredSince) >= 2000);
        if (longEnough && btnEstop.wasPressed()) {
            Serial.println("[Safety] Check PASSED");
            return true;
        }

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
    Serial.println("\n[Boot] M25 Remote Control starting...");

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

    // --- Serial command processing (input + live debug output) ---
    serialTick(_serialCtx);

    // --- Update input drivers ---
    buttonsTick();
    JoystickNorm js = joystickRead();

    // --- Emergency stop: highest priority, any state ---
    if (btnEstop.wasPressed()) {
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
            if (bleAllConnected()) {
                enterReady();
            } else {
                // Reconnect attempts are handled by bleTick() below
                ledSetStatus(LED_BLINK_SLOW);
            }
            break;

        // ---- READY ----
        case STATE_READY: {
            // Hill hold toggle (allowed when motors stopped)
            if (btnHillHold.wasPressed()) {
                hillHoldOn = !hillHoldOn;
                ledSetHillHold(hillHoldOn);
                bleSendHillHold(hillHoldOn);
                Serial.printf("[HillHold] %s\n", hillHoldOn ? "ON" : "OFF");
            }

            // Assist level cycle (allowed when motors stopped)
            if (btnAssist.wasPressed()) {
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

            // Joystick activated -> OPERATING
            if (!js.inDeadzone) {
                enterOperating();
                // Fall through to OPERATING to send first command immediately
            } else {
                // Keep wheels explicitly stopped (belt-and-suspenders)
                if (now - lastCommandSentMs >= COMMAND_RATE_MS) {
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
            if (btnHillHold.wasPressed()) {
                Serial.println("[HillHold] Ignored - motors active");
            }

            // Assist level NOT allowed while wheels are moving
            if (btnAssist.wasPressed()) {
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
            } else {
                // Joystick returned to center -> back to READY
                enterReady();
                bleSendStop();
                lastCommandSentMs = now;
                break;
            }

            // Watchdog warning and timeout
            uint32_t idle = now - lastActiveMs;
            if (idle >= WATCHDOG_TIMEOUT_MS) {
                enterError("Watchdog timeout - no joystick input");
                break;
            } else if (idle >= WATCHDOG_WARN_MS && !watchdogWarnShown) {
                watchdogWarnShown = true;
                Serial.println("[Watchdog] WARNING: joystick inactive > 3 s");
                // Brief fast-blink on status LED as visual warning
                ledSetStatus(LED_BLINK_FAST);
            } else if (idle < WATCHDOG_WARN_MS) {
                ledSetStatus(LED_ON);
            }

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
            // All movement blocked; E-stop handling is already at top of loop.
            // Keep sending stop at reduced rate to be sure wheels halt.
            if (now - lastCommandSentMs >= 200) {
                bleSendStop();
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
