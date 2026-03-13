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
#include "nvs_config.h"
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

// Supervisor connection change callback (called from BLE disconnect callback)
void supervisorNotifyConnectionChange() {
    supervisor.notifyConnectionChange();
}

static void onSupervisorStateChange(SupervisorState oldState, SupervisorState newState) {
    if (debugFlags & DBG_STATE) {
        const char* newName = (newState == SUPERVISOR_DISCONNECTED) ? "DISCONNECTED" :
                              (newState == SUPERVISOR_CONNECTING) ? "CONNECTING" :
                              (newState == SUPERVISOR_PAIRED) ? "PAIRED" :
                              (newState == SUPERVISOR_ARMED) ? "ARMED" :
                              (newState == SUPERVISOR_DRIVING) ? "DRIVING" :
                              (newState == SUPERVISOR_FAILSAFE) ? "FAILSAFE" : "?";
        Serial.printf("[Supervisor] State change: %s\n", newName);
    }
    
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
            break;
            
        case SUPERVISOR_CONNECTING:
            ledSetStatus(LED_BLINK_SLOW);
            break;
            
        case SUPERVISOR_PAIRED:
            ledSetStatus(LED_OFF);
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
            break;
    }
}

#ifdef ENABLE_BATTERY_MONITOR
static uint32_t lastBatteryMs = 0;
#define BATTERY_READ_INTERVAL_MS 10000
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



static void enterOff() {
    sysState = STATE_OFF;
    supervisor.requestDisconnect();
    buzzerPlay(BUZZ_POWER_OFF);
    
    // Turn off all LEDs
    ledSetStatus(LED_OFF);
    ledSetBleMode(LED_OFF);
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
    
    delay(100);
    
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_POWER_PIN, 0);
    
    Serial.println("[Power] Entering deep sleep...");
    Serial.flush();
    delay(50);
    
    esp_deep_sleep_start();
}

static bool powerOnSafetyCheck() {
#ifdef NO_JOYSTICK
    Serial.println("[Safety] NO_JOYSTICK: press E-stop to confirm, or send 'confirm' via serial");
#else
    Serial.println("[Safety] Power-on check: center joystick and hold for 5 seconds");
    Serial.println("[Safety] (Send 'confirm' via serial or press E-stop to skip)");
#endif

    uint32_t centeredSince = 0;
    bool     centered      = false;
    char     scBuf[16];
    uint8_t  scLen         = 0;

    while (true) {
        buttonsTick();
        JoystickNorm js = joystickRead();

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
        if (js.inDeadzone) {
            if (!centered) {
                centered      = true;
                centeredSince = millis();
                Serial.println("[Safety] Joystick centered - hold for 5 seconds");
            }
        } else {
            if (centered) {
                centered = false;
                Serial.println("[Safety] Joystick off-center, re-center and hold");
            }
        }
        
        if (centered && ((millis() - centeredSince) >= 5000)) {
            Serial.println("[Safety] Check PASSED (auto-confirmed)");
            return true;
        }
        
        if (btnEstop.wasPressed()) {
            Serial.println("[Safety] Check PASSED (E-stop pressed)");
            return true;
        }
#else
        if (btnEstop.wasPressed()) {
            Serial.println("[Safety] Check PASSED (no-joystick mode)");
            return true;
        }
#endif

        ledTick();
        buzzerTick();
        delay(10);
    }
    return false;
}

static SerialContext _serialCtx = {
    &sysState,
    &assistLevel,
    &hillHoldOn,
    &supervisor,
    enterOff,
    joystickRecalibrate,
#ifdef ENABLE_BATTERY_MONITOR
    &batteryPct,
#endif
};

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

    sysState = STATE_BOOT;
    powerOnSafetyCheck();

    bleInit("M25-Remote");
    bleStartMotorTask();   // spawn async write task on Core 0
    ledSetAssistLevel(assistLevel);
    ledSetHillHold(hillHoldOn);

    char    leftMac[18], rightMac[18];
    uint8_t leftKey[16], rightKey[16];
    nvsLoadMac(WHEEL_LEFT,  leftMac,  sizeof(leftMac));
    nvsLoadMac(WHEEL_RIGHT, rightMac, sizeof(rightMac));
    nvsLoadKey(WHEEL_LEFT,  leftKey);
    nvsLoadKey(WHEEL_RIGHT, rightKey);
    Serial.printf("[Boot] Left  wheel: %s\n", leftMac);
    Serial.printf("[Boot] Right wheel: %s\n", rightMac);
    supervisor.begin();
    supervisor.addStateCallback(onSupervisorStateChange);
    supervisor.requestConnect(leftMac, rightMac, leftKey, rightKey);
    
    serialInit(_serialCtx);
}

void loop() {
    uint32_t now = millis();
    loopCounter++;
    
    SupervisorState supState = supervisor.getState();
    
    const char* stateName = (sysState == STATE_BOOT) ? "BOOT" :
                       (sysState == STATE_CONNECTING) ? "CONNECTING" :
                       (sysState == STATE_READY) ? "READY" :
                       (sysState == STATE_OPERATING) ? "OPERATING" :
                       (sysState == STATE_ERROR) ? "ERROR" :
                       (sysState == STATE_OFF) ? "OFF" : "?";

    if (now - lastHeartbeatMs >= 5000 && debugFlags & DBG_HEARTBEAT) {
        lastHeartbeatMs = now;
        Serial.printf("[Heartbeat] loop running, count=%u  state=%s\n", loopCounter, stateName);
    }

    serialTick(_serialCtx);

    buttonsTick();
    JoystickNorm js = joystickRead();

    ControlState control;
    control.vx = js.y;
    control.vy = js.x;
#ifdef NO_DEADMAN_HARDWARE
    control.deadman = true;   // no hardware button - always engaged
#else
    control.deadman = !js.inDeadzone;
#endif
    control.mode = js.inDeadzone ? DRIVE_MODE_STOP : DRIVE_MODE_NORMAL;
    control.timestamp = now;
    
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

    if (powerPressed && sysState != STATE_BOOT) {
        if (sysState == STATE_OFF) {
            Serial.println("[Power] Turning ON...");
            char    leftMac[18], rightMac[18];
            uint8_t leftKey[16], rightKey[16];
            nvsLoadMac(WHEEL_LEFT,  leftMac,  sizeof(leftMac));
            nvsLoadMac(WHEEL_RIGHT, rightMac, sizeof(rightMac));
            nvsLoadKey(WHEEL_LEFT,  leftKey);
            nvsLoadKey(WHEEL_RIGHT, rightKey);
            bleInit("M25-Remote");
            ledSetAssistLevel(assistLevel);
            ledSetHillHold(hillHoldOn);
            #ifdef ENABLE_BATTERY_MONITOR
            ledSetBattery(batteryPct);
            #endif
            supervisor.requestConnect(leftMac, rightMac, leftKey, rightKey);
        } else {
            Serial.println("[Power] Turning OFF...");
            enterOff();
        }
        ledTick();
        return;
    }

    if (estopPressed) {
        if (supState == SUPERVISOR_FAILSAFE) {
            if (debugFlags & DBG_STATE) {
                Serial.println("[E-Stop] Reset: requesting reconnect...");
            }
            supervisor.requestReconnect();
        } else {
            if (debugFlags & DBG_STATE) {
                Serial.println("[E-Stop] Emergency stop");
            }
            supervisor.requestEmergencyStop("E-Stop button pressed");
        }
    }

    if (hillHoldPressed && assistPressed) {
        if (debugFlags & DBG_BUTTONS) {
            Serial.println("[Button] Dual press: forcing reconnect...");
        }
        supervisor.requestReconnect();
        ledTick();
        return;
    }
    
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

    supervisor.processInput(control);
    supervisor.update();

#ifdef ENABLE_BATTERY_MONITOR
    if (now - lastBatteryMs >= BATTERY_READ_INTERVAL_MS && sysState != STATE_OFF) {
        lastBatteryMs = now;
        batteryPct    = readBatteryPct();
        ledSetBattery(batteryPct);
        Serial.printf("[Battery] %d %%\n", batteryPct);
        
        if (batteryPct <= BATT_AUTO_OFF_PCT && sysState != STATE_ERROR && sysState != STATE_OFF) {
            Serial.println("[Battery] CRITICAL - forcing disconnect");
            supervisor.requestDisconnect();
            buzzerPlay(BUZZ_ERROR);
        }
    }
#endif

    // BLE LED: derived from actual connection state every loop (none/partial/all)
    ledUpdateBle(bleAnyConnected(), bleAllConnected());
    ledTick();
    buzzerTick();
}
