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
 *   OFF        -> device in deep sleep, power consumption ~10-150 µA
 *                 Press power button to wake - device will reboot.
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include "device_config.h"
#include "types.h"
#include "mapper.h"
#include "supervisor.h"
#include "joystick.h"
#include "led_control.h"
#include "button.h"
#include "motor_control.h"
#include "m25_ble.h"
#include "serial_commands.h"
#include "buzzer.h"

static SystemState sysState = STATE_BOOT;

// ---------------------------------------------------------------------------
// Persistent control state
// ---------------------------------------------------------------------------
static uint8_t assistLevel  = ASSIST_INDOOR;   // boot default: indoor
static bool    hillHoldOn   = false;
#ifdef ENABLE_BATTERY_MONITOR
static int     batteryPct   = 100;
#endif

// Watchdog: tracks last non-zero joystick event (legacy - Supervisor handles this now)
// static uint32_t lastActiveMs      = 0;
// static bool     watchdogWarnShown = false;

// Command rate limiter (legacy - Supervisor handles this now)
// static uint32_t lastCommandSentMs = 0;

// Joystick transition hold timers (legacy - Supervisor handles this now)
// static uint32_t jsActiveSinceMs = 0;   // when joystick first left deadzone
// static uint32_t jsIdleSinceMs   = 0;   // when joystick first entered deadzone

// Reconnect rate limiter (legacy - may still be needed for bleTick)
static uint32_t lastBleTickMs = 0;

// Loop heartbeat (debug - shows loop() is running)
static uint32_t lastHeartbeatMs = 0;
static uint32_t loopCounter = 0;

// ERROR state: limit BLE stop retries so writeValue() can't block loop() forever.
// We send up to BLE_ERROR_STOP_TRIES stops after entering ERROR, then give up.
// The wheel's own remote-control watchdog will cut power if it hears nothing.
#define BLE_ERROR_STOP_TRIES 3
static uint8_t _errorStopsSent = 0;

static MapperConfig mapperConfig;
static Mapper mapper(mapperConfig);
static SupervisorConfig supervisorConfig;
static Supervisor supervisor(mapper, supervisorConfig);

// ---------------------------------------------------------------------------
// Supervisor state change callback (LED and buzzer feedback)
// ---------------------------------------------------------------------------
static void onSupervisorStateChange(SupervisorState oldState, SupervisorState newState) {
    const char* newName = (newState == SUPERVISOR_DISCONNECTED) ? "DISCONNECTED" :
                          (newState == SUPERVISOR_CONNECTING) ? "CONNECTING" :
                          (newState == SUPERVISOR_PAIRED) ? "PAIRED" :
                          (newState == SUPERVISOR_ARMED) ? "ARMED" :
                          (newState == SUPERVISOR_DRIVING) ? "DRIVING" :
                          (newState == SUPERVISOR_FAILSAFE) ? "FAILSAFE" : "?";
    
    Serial.printf("[Supervisor] State change: %s\n", newName);
    
    // Update legacy sysState for serial commands compatibility
    if (newState == SUPERVISOR_DISCONNECTED || newState == SUPERVISOR_CONNECTING) {
        sysState = STATE_CONNECTING;
    } else if (newState == SUPERVISOR_PAIRED) {
        sysState = STATE_READY;
    } else if (newState == SUPERVISOR_ARMED || newState == SUPERVISOR_DRIVING) {
        sysState = STATE_OPERATING;
    } else if (newState == SUPERVISOR_FAILSAFE) {
        sysState = STATE_ERROR;
    }
    
    // LED and buzzer feedback for state transitions
    switch (newState) {
        case SUPERVISOR_DISCONNECTED:
            ledSetStatus(LED_OFF);
            ledSetBle(false);
            break;
            
        case SUPERVISOR_CONNECTING:
            ledSetStatus(LED_BLINK_SLOW);
            ledSetBle(false);
            if (oldState == SUPERVISOR_DISCONNECTED) {
                buzzerPlay(BUZZ_CONNECTING);
            }
            break;
            
        case SUPERVISOR_PAIRED:
            ledSetStatus(LED_OFF);
            ledSetBle(true);
            buzzerPlay(BUZZ_CONFIRM);
            break;
            
        case SUPERVISOR_ARMED:
            ledSetStatus(LED_OFF);
            break;
            
        case SUPERVISOR_DRIVING:
            ledSetStatus(LED_OFF);
            break;
            
        case SUPERVISOR_FAILSAFE:
            ledSetStatus(LED_BLINK_FAST);
            buzzerPlay(BUZZ_ERROR);
            ledSetBle(false);
            break;
    }
}

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
    Serial.println("[Serial] Command: enterConnecting - requesting Supervisor connect");
    static const uint8_t leftKey[] = ENCRYPTION_KEY_LEFT;
    static const uint8_t rightKey[] = ENCRYPTION_KEY_RIGHT;
    supervisor.requestConnect(LEFT_WHEEL_MAC, RIGHT_WHEEL_MAC, leftKey, rightKey);
}

static void enterReady() {
    // No longer used - Supervisor handles this automatically
    Serial.println("[Serial] Command: enterReady - no-op (Supervisor manages state)");
}

static void enterOperating() {
    // No longer used - Supervisor handles this automatically
    Serial.println("[Serial] Command: enterOperating - no-op (Supervisor manages state)");
}

static void enterError(const char* reason) {
    Serial.printf("[Serial] Command: enterError - reason: %s (triggering failsafe)\n", reason);
    // Force Supervisor into failsafe by requesting disconnect
    supervisor.requestDisconnect();
}

static void enterOff() {
    sysState = STATE_OFF;
    supervisor.requestDisconnect();
    buzzerPlay(BUZZ_POWER_OFF);
    
    // Turn off all LEDs
    ledSetStatus(LED_OFF);
    ledSetBle(false);
    ledSetBattery(0);  // force off
    ledSetHillHold(false);
    ledSetAssistLevel(ASSIST_INDOOR);  // shows off
    ledTick();  // Apply LED changes immediately
    
    Serial.println("[State] -> OFF  (entering deep sleep)");
    Serial.println("[Power] Press power button to wake up");
    Serial.flush();  // Wait for serial output to complete
    
    // Wait for buzzer pattern to complete before sleep
    while (buzzerIsActive()) {
        buzzerTick();
        delay(10);
    }
    
    delay(100);  // Give BLE and serial time to finish
    
    // Configure power button (GPIO 13) as wake-up source
    // EXT0: wake on LOW level (button pressed, since active LOW with pull-up)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_POWER_PIN, 0);
    
    // Enter deep sleep - device will reboot when power button is pressed
    Serial.println("[Power] Entering deep sleep...");
    Serial.flush();
    delay(50);
    
    esp_deep_sleep_start();
    // Execution never reaches here
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
        buzzerTick();
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
    enterOff,
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
    
    // Check wake-up reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("[Boot] Wake-up from deep sleep via power button");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("[Boot] Cold boot or reset");
            break;
    }

    // Peripheral init
    ledInit();
    ledStartupTest();
    
    buzzerInit();
    buzzerPlay(BUZZ_POWER_ON);

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

    // Initialize Supervisor (this will handle state machine)
    static const uint8_t leftKey[] = ENCRYPTION_KEY_LEFT;
    static const uint8_t rightKey[] = ENCRYPTION_KEY_RIGHT;
    supervisor.begin();
    supervisor.addStateCallback(onSupervisorStateChange);
    supervisor.requestConnect(LEFT_WHEEL_MAC, RIGHT_WHEEL_MAC, leftKey, rightKey);
    
    serialInit(_serialCtx);
}

// ---------------------------------------------------------------------------
// Arduino loop
// ---------------------------------------------------------------------------
void loop() {
    uint32_t now = millis();
    loopCounter++;
    
    // Get current supervisor state for UI and logging
    SupervisorState supState = supervisor.getState();
    
    // Legacy sysState is updated by onSupervisorStateChange callback
    const char* stateName = (sysState == STATE_BOOT) ? "BOOT" :
                       (sysState == STATE_CONNECTING) ? "CONNECTING" :
                       (sysState == STATE_READY) ? "READY" :
                       (sysState == STATE_OPERATING) ? "OPERATING" :
                       (sysState == STATE_ERROR) ? "ERROR" :
                       (sysState == STATE_OFF) ? "OFF" : "?";

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

    // --- Build ControlState from joystick for Supervisor ---
    ControlState control;
    control.vx = js.y;  // Forward/backward
    control.vy = js.x;  // Left/right (strafe)
    control.deadman = !js.inDeadzone;  // Active when outside deadzone
    control.mode = js.inDeadzone ? DRIVE_MODE_STOP : DRIVE_MODE_MANUAL;
    control.timestamp = now;
    
    // --- Button press detection & logging (before action, so we see everything) ---
    bool estopPressed    = btnEstop.wasPressed();
    bool hillHoldPressed = btnHillHold.wasPressed();
    bool assistPressed   = btnAssist.wasPressed();
    bool powerPressed    = btnPower.wasPressed();

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
        if (powerPressed) {
            Serial.printf("[Button] POWER pressed  (state=%s)\n", stateName);
        }
    }

    // --- Power button: highest priority (except in BOOT) ---
    if (powerPressed && sysState != STATE_BOOT) {
        if (sysState == STATE_OFF) {
            // This case should never be reached since we enter deep sleep
            // But kept for serial command 'power on' support during development
            Serial.println("[Power] Turning ON...");
            static const uint8_t leftKey[] = ENCRYPTION_KEY_LEFT;
            static const uint8_t rightKey[] = ENCRYPTION_KEY_RIGHT;
            bleInit("M25-Remote");
            ledSetAssistLevel(assistLevel);
            ledSetHillHold(hillHoldOn);
            #ifdef ENABLE_BATTERY_MONITOR
            ledSetBattery(batteryPct);
            #endif
            supervisor.requestConnect(LEFT_WHEEL_MAC, RIGHT_WHEEL_MAC, leftKey, rightKey);
        } else {
            // Turn off from any active state -> deep sleep
            Serial.println("[Power] Turning OFF...");
            enterOff();  // This function calls esp_deep_sleep_start() and never returns
            // Execution never reaches here
        }
        // Do not process other inputs this iteration
        ledTick();
        return;
    }

    // --- Emergency stop: highest priority, any state ---
    if (estopPressed) {
        if (supState == SUPERVISOR_FAILSAFE) {
            // Second press: reset from error - disconnect and reconnect
            Serial.println("[E-Stop] Reset: requesting reconnect...");
            static const uint8_t leftKey[] = ENCRYPTION_KEY_LEFT;
            static const uint8_t rightKey[] = ENCRYPTION_KEY_RIGHT;
            supervisor.requestDisconnect();
            delay(100);  // Give BLE time to clean up
            supervisor.requestConnect(LEFT_WHEEL_MAC, RIGHT_WHEEL_MAC, leftKey, rightKey);
        } else {
            // First press: emergency stop (Supervisor handles failsafe via control.deadman = false)
            Serial.println("[E-Stop] Emergency stop");
            control.deadman = false;
            control.mode = DRIVE_MODE_STOP;
        }
        // Continue processing to feed stop command to supervisor
    }

    // --- Feature buttons (state-dependent) ---
    
    // Dual button press: force reconnect (hill-hold + assist simultaneously)
    if (hillHoldPressed && assistPressed) {
        Serial.println("[Button] Dual press: forcing reconnect...");
        static const uint8_t leftKey[] = ENCRYPTION_KEY_LEFT;
        static const uint8_t rightKey[] = ENCRYPTION_KEY_RIGHT;
        supervisor.requestDisconnect();
        delay(100);
        supervisor.requestConnect(LEFT_WHEEL_MAC, RIGHT_WHEEL_MAC, leftKey, rightKey);
        ledTick();
        return;
    }
    
    // Hill hold toggle (only when not driving)
    if (hillHoldPressed) {
        if (supState == SUPERVISOR_PAIRED || supState == SUPERVISOR_ARMED) {
            hillHoldOn = !hillHoldOn;
            ledSetHillHold(hillHoldOn);
            buzzerPlay(BUZZ_BUTTON);
            bleSendHillHold(hillHoldOn);
            Serial.printf("[HillHold] %s\n", hillHoldOn ? "ON" : "OFF");
        } else if (supState == SUPERVISOR_DRIVING) {
            buzzerPlay(BUZZ_WARNING);
            Serial.println("[HillHold] Ignored - motors active");
        } else {
            Serial.println("[HillHold] Ignored - not connected");
        }
    }

    // Assist level cycle (only when not driving)
    if (assistPressed) {
        if (supState == SUPERVISOR_PAIRED || supState == SUPERVISOR_ARMED) {
            assistLevel = (assistLevel + 1) % ASSIST_COUNT;
            ledSetAssistLevel(assistLevel);
            buzzerPlay(BUZZ_CONFIRM);
            bleSendAssistLevel(assistLevel);
            Serial.printf("[Assist] -> %s  (Vmax fwd %d %%)\n",
                          assistConfigs[assistLevel].name,
                          assistConfigs[assistLevel].vmaxForward);
        } else if (supState == SUPERVISOR_DRIVING) {
            buzzerPlay(BUZZ_WARNING);
            Serial.println("[Assist] Ignored - motors active, stop first");
        } else {
            Serial.println("[Assist] Ignored - not connected");
        }
    }

    // --- Feed control input to Supervisor ---
    supervisor.processInput(control);
    
    // --- Update Supervisor state machine (handles all state logic and motor commands) ---
    supervisor.update();

    // --- Battery monitoring (every 10 s) ---
#ifdef ENABLE_BATTERY_MONITOR
    if (now - lastBatteryMs >= BATTERY_READ_INTERVAL_MS && sysState != STATE_OFF) {
        lastBatteryMs = now;
        batteryPct    = readBatteryPct();
        ledSetBattery(batteryPct);
        Serial.printf("[Battery] %d %%\n", batteryPct);
        
        // Critical battery: force safe shutdown
        if (batteryPct <= BATT_AUTO_OFF_PCT && sysState != STATE_ERROR && sysState != STATE_OFF) {
            Serial.println("[Battery] CRITICAL - forcing disconnect");
            supervisor.requestDisconnect();
            buzzerPlay(BUZZ_ERROR);
            // Supervisor will enter FAILSAFE automatically
        }
    }
#endif

    // --- LED tick (handles blinking) ---
    ledTick();
    
    // --- Buzzer tick (handles pattern playback) ---
    buzzerTick();
}
