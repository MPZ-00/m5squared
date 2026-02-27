/**
 * Command Handler
 * 
 * Serial command processing for wheel control and debugging
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_sleep.h>
#include "wheel_state.h"
#include "led_control.h"
#include "buzzer_control.h"

// Debug flags (bitfield)
#define DBG_PROTOCOL    0x01  // Protocol parsing details
#define DBG_CRYPTO      0x02  // Encryption/decryption steps
#define DBG_CRC         0x04  // CRC validation
#define DBG_COMMANDS    0x08  // Command interpretation
#define DBG_RAW_DATA    0x10  // Raw hex dumps

// Global debug flags
static uint8_t debugFlags = 0;

// Debug flag metadata for better UI
struct DebugFlagInfo {
    uint8_t     mask;
    const char* name;
    const char* description;
};

static const DebugFlagInfo _debugFlagTable[] = {
    { DBG_PROTOCOL, "protocol", "Protocol parsing details" },
    { DBG_CRYPTO,   "crypto",   "Encryption/decryption steps" },
    { DBG_CRC,      "crc",      "CRC validation" },
    { DBG_COMMANDS, "commands", "Command interpretation" },
    { DBG_RAW_DATA, "raw",      "Raw hex dumps" },
};
static const uint8_t _debugFlagCount = sizeof(_debugFlagTable) / sizeof(_debugFlagTable[0]);

// External references (set by main sketch)
static BLEServer* g_pServer = NULL;
static BLECharacteristic* g_pTxCharacteristic = NULL;
static bool* g_deviceConnected = NULL;
static const uint8_t* g_encryptionKey = NULL;

/**
 * Initialize command handler with external references
 */
static void initCommandHandler(BLEServer* server, BLECharacteristic* tx, bool* connected, const uint8_t* key) {
    g_pServer = server;
    g_pTxCharacteristic = tx;
    g_deviceConnected = connected;
    g_encryptionKey = key;
}

/**
 * Print help text
 */
static void printHelp() {
    Serial.println("\n=== Available Commands ===");
    Serial.println("help              - Show this help");
    Serial.println("status            - Show current wheel state");
    Serial.println("mac               - Show BLE MAC address");
    Serial.println("key               - Show encryption key");
    Serial.println("hardware          - Show hardware pin configuration");
    Serial.println("battery [0-100]   - Set/show battery level");
    Serial.println("speed <value>     - Set/show simulated speed");
    Serial.println("assist [0-2]      - Set/show assist level");
    Serial.println("profile [0-5]     - Set/show drive profile");
    Serial.println("hillhold [on/off] - Toggle hill hold");
    Serial.println("rotate [n]        - Simulate n wheel rotations (default=1)");
    Serial.println("reset             - Reset wheel rotation counter");
    Serial.println("debug [flag]      - Show/toggle debug flags (use 'debug' to list)");
    Serial.println("audio [on/off]    - Toggle audio feedback (buzzer)");
    Serial.println("visual [on/off]   - Toggle visual feedback (Blue LED)");
    Serial.println("beep [count]      - Play beeps (1-10)");
    Serial.println("tone <freq>       - Play tone (50-5000 Hz)");
    Serial.println("send              - Send response packet now");
    Serial.println("disconnect        - Disconnect client");
    Serial.println("advertise         - Force restart BLE advertising");
    Serial.println("power off         - Turn device off (deep sleep)");
    Serial.println("power on          - Wake device (only works if deep sleep disabled)");
    Serial.println("restart           - Restart ESP32");
    Serial.println("========================\n");
}

/**
 * Print debug flags with status
 */
static void printDebugFlags() {
    Serial.println("\n=== Debug Flags ===");
    Serial.printf("Current: 0x%02X", debugFlags);
    if (debugFlags == 0) {
        Serial.println("  (all disabled)");
    } else {
        Serial.print("  (");
        bool first = true;
        for (uint8_t i = 0; i < _debugFlagCount; i++) {
            if (debugFlags & _debugFlagTable[i].mask) {
                if (!first) Serial.print(", ");
                Serial.print(_debugFlagTable[i].name);
                first = false;
            }
        }
        Serial.println(")");
    }
    Serial.println();
    Serial.println("Flag       Status  Description");
    Serial.println("---------- ------- ----------------------------------");
    
    for (uint8_t i = 0; i < _debugFlagCount; i++) {
        bool enabled = (debugFlags & _debugFlagTable[i].mask) != 0;
        Serial.printf("%-10s [%s]  %s\n",
            _debugFlagTable[i].name,
            enabled ? "ON " : "off",
            _debugFlagTable[i].description);
    }
    
    Serial.println("\nUsage:");
    Serial.println("  debug              Show this list");
    Serial.println("  debug <flag>       Toggle specific flag");
    Serial.println("  debug all          Enable all flags");
    Serial.println("  debug none         Disable all flags");
    Serial.println("===================\n");
}

/**
 * Print encryption key
 */
static void printKey() {
    if (!g_encryptionKey) {
        Serial.println("ERROR: Encryption key not set");
        return;
    }
    Serial.println("\n=== Encryption Key ===");
    Serial.print("Key (hex): ");
    for (int i = 0; i < 16; i++) {
        if (g_encryptionKey[i] < 0x10) Serial.print("0");
        Serial.print(g_encryptionKey[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    Serial.println("======================\n");
}

/**
 * Print hardware configuration
 */
static void printHardware() {
    Serial.println("\n=== Hardware Configuration ===");
    Serial.println("LEDs:");
    Serial.println("  White LED (Pin " + String(LED_WHITE) + ") - Connection Status");
    Serial.println("  Blue LED (Pin " + String(LED_BLUE) + ") - Speed Indicator");
    Serial.println("  Red LED (Pin " + String(LED_RED) + ") - Low Battery");
    Serial.println("  Yellow LED (Pin " + String(LED_YELLOW) + ") - Medium Battery");
    Serial.println("  Green LED (Pin " + String(LED_GREEN) + ") - High Battery");
    Serial.println("Buzzers:");
    Serial.println("  Passive Buzzer (Pin " + String(BUZZER_PASSIVE) + ") - Speed Tone");
    Serial.println("  Active Buzzer (Pin " + String(BUZZER_ACTIVE) + ") - Event Beeps");
    Serial.println("==============================\n");
}

/**
 * Handle serial commands
 */
static void handleSerialCommand(WheelState& wheel) {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.length() == 0) return;
    
    Serial.print("> ");
    Serial.println(cmd);
    
    // Parse command
    int spaceIndex = cmd.indexOf(' ');
    String command = spaceIndex > 0 ? cmd.substring(0, spaceIndex) : cmd;
    String arg = spaceIndex > 0 ? cmd.substring(spaceIndex + 1) : "";
    
    command.toLowerCase();
    
    // Process commands
    if (command == "help") {
        printHelp();
    }
    else if (command == "status") {
        wheel.printStatus();
    }
    else if (command == "mac") {
        Serial.print("MAC Address: ");
        Serial.println(BLEDevice::getAddress().toString().c_str());
    }
    else if (command == "key") {
        printKey();
    }
    else if (command == "hardware") {
        printHardware();
    }
    else if (command == "battery") {
        if (arg.length() > 0) {
            int val = arg.toInt();
            if (val >= 0 && val <= 100) {
                wheel.batteryLevel = val;
                Serial.print("Battery set to ");
                Serial.print(val);
                Serial.println("%");
                showBatteryLevel(wheel.batteryLevel);
            } else {
                Serial.println("ERROR: Battery must be 0-100");
            }
        } else {
            Serial.print("Battery: ");
            Serial.print(wheel.batteryLevel);
            Serial.println("%");
        }
    }
    else if (command == "speed") {
        if (arg.length() > 0) {
            int val = arg.toInt();
            wheel.currentSpeed = val;
            Serial.print("Speed set to ");
            Serial.println(val);
        } else {
            Serial.print("Speed: ");
            Serial.println(wheel.currentSpeed);
        }
    }
    else if (command == "assist") {
        if (arg.length() > 0) {
            int val = arg.toInt();
            if (val >= 0 && val <= 2) {
                wheel.assistLevel = val;
                Serial.print("Assist level set to ");
                Serial.println(val);
            } else {
                Serial.println("ERROR: Assist level must be 0-2");
            }
        } else {
            Serial.print("Assist level: ");
            Serial.println(wheel.assistLevel);
        }
    }
    else if (command == "profile") {
        if (arg.length() > 0) {
            int val = arg.toInt();
            if (val >= 0 && val <= 5) {
                wheel.driveProfile = val;
                Serial.print("Drive profile set to ");
                Serial.println(val);
            } else {
                Serial.println("ERROR: Profile must be 0-5");
            }
        } else {
            Serial.print("Drive profile: ");
            Serial.println(wheel.driveProfile);
        }
    }
    else if (command == "hillhold") {
        if (arg.length() > 0) {
            arg.toLowerCase();
            if (arg == "on" || arg == "1") {
                wheel.hillHold = true;
                Serial.println("Hill hold enabled");
            } else if (arg == "off" || arg == "0") {
                wheel.hillHold = false;
                Serial.println("Hill hold disabled");
            } else {
                Serial.println("ERROR: Use 'on' or 'off'");
            }
        } else {
            Serial.print("Hill hold: ");
            Serial.println(wheel.hillHold ? "ON" : "OFF");
        }
    }
    else if (command == "rotate") {
        int rotations = arg.length() > 0 ? arg.toInt() : 1;
        wheel.simulateRotation(rotations);
    }
    else if (command == "reset") {
        wheel.resetRotation();
    }
    else if (command == "debug") {
        arg.toLowerCase();
        
        // No argument: show all flags
        if (arg.length() == 0) {
            printDebugFlags();
        }
        // debug none
        else if (arg == "none") {
            debugFlags = 0;
            Serial.println("All debug flags disabled");
            printDebugFlags();
        }
        // debug all
        else if (arg == "all") {
            debugFlags = 0xFF;
            Serial.println("All debug flags enabled");
            printDebugFlags();
        }
        // Try to find matching flag name in table
        else {
            bool found = false;
            for (uint8_t i = 0; i < _debugFlagCount; i++) {
                if (arg == _debugFlagTable[i].name) {
                    debugFlags ^= _debugFlagTable[i].mask;  // Toggle
                    bool nowEnabled = (debugFlags & _debugFlagTable[i].mask) != 0;
                    Serial.printf("%s debug -> %s\n", 
                        _debugFlagTable[i].name,
                        nowEnabled ? "ON" : "OFF");
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                Serial.printf("Unknown flag: '%s'\n", arg.c_str());
                Serial.println("Available flags:");
                for (uint8_t i = 0; i < _debugFlagCount; i++) {
                    Serial.printf("  %-10s  %s\n", 
                        _debugFlagTable[i].name,
                        _debugFlagTable[i].description);
                }
            }
        }
    }
    else if (command == "audio") {
        if (arg.length() > 0) {
            arg.toLowerCase();
            if (arg == "on" || arg == "1") {
                audioFeedbackEnabled = true;
                Serial.println("Audio feedback enabled");
            } else if (arg == "off" || arg == "0") {
                audioFeedbackEnabled = false;
                Serial.println("Audio feedback disabled");
            } else {
                Serial.println("ERROR: Use 'on' or 'off'");
            }
        } else {
            Serial.print("Audio feedback: ");
            Serial.println(audioFeedbackEnabled ? "ON" : "OFF");
        }
    }
    else if (command == "visual") {
        if (arg.length() > 0) {
            arg.toLowerCase();
            if (arg == "on" || arg == "1") {
                visualFeedbackEnabled = true;
                Serial.println("Visual feedback enabled");
            } else if (arg == "off" || arg == "0") {
                visualFeedbackEnabled = false;
                Serial.println("Visual feedback disabled");
            } else {
                Serial.println("ERROR: Use 'on' or 'off'");
            }
        } else {
            Serial.print("Visual feedback: ");
            Serial.println(visualFeedbackEnabled ? "ON" : "OFF");
        }
    }
    else if (command == "beep") {
        uint8_t count = 1;
        if (arg.length() > 0) {
            count = constrain(arg.toInt(), 1, 10);
        }
        playBeep(count);
    }
    else if (command == "tone") {
        if (arg.length() > 0) {
            uint16_t freq = constrain(arg.toInt(), 50, 5000);
            Serial.print("Playing ");
            Serial.print(freq);
            Serial.println(" Hz tone for 500ms");
            playTone(freq, 500);
        } else {
            Serial.println("Usage: tone <frequency>");
        }
    }
    else if (command == "send") {
        if (g_deviceConnected && *g_deviceConnected) {
            Serial.println("Sending response packet...");
            // Response sending handled in main sketch
        } else {
            Serial.println("ERROR: No client connected");
        }
    }
    else if (command == "disconnect") {
        if (g_pServer && g_deviceConnected && *g_deviceConnected) {
            Serial.println("Disconnecting client...");
            g_pServer->disconnect(g_pServer->getConnId());
        } else {
            Serial.println("ERROR: No client connected");
        }
    }
    else if (command == "advertise") {
        Serial.println("Restarting BLE advertising...");
        BLEDevice::startAdvertising();
        Serial.println("Advertising restarted");
    }
    else if (command == "power") {
        arg.toLowerCase();
        if (arg == "off") {
            Serial.println("Turning OFF (entering deep sleep)...");
            Serial.println("Press RESET button to wake up");
            delay(500);
            esp_deep_sleep_start();  // Never returns
        } else if (arg == "on") {
            Serial.println("Note: Device uses deep sleep. RESET button causes reboot.");
            Serial.println("This command only works if deep sleep is disabled.");
        } else {
            Serial.println("Usage: power <on|off>");
        }
    }
    else if (command == "restart") {
        Serial.println("Restarting ESP32...");
        delay(500);
        ESP.restart();
    }
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

#endif // COMMAND_HANDLER_H
