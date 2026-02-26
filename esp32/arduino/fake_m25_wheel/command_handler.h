/**
 * Command Handler
 * 
 * Serial command processing for wheel control and debugging
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <BLEDevice.h>
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
static uint8_t debugFlags = DBG_COMMANDS;  // Default: only show decoded commands

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
    Serial.println("debug [option]    - Control debug flags (use 'debug help')");
    Serial.println("audio [on/off]    - Toggle audio feedback (buzzer)");
    Serial.println("visual [on/off]   - Toggle visual feedback (Blue LED)");
    Serial.println("beep [count]      - Play beeps (1-10)");
    Serial.println("tone <freq>       - Play tone (50-5000 Hz)");
    Serial.println("send              - Send response packet now");
    Serial.println("disconnect        - Disconnect client");
    Serial.println("advertise         - Force restart BLE advertising");
    Serial.println("========================\n");
}

/**
 * Print debug help text
 */
static void printDebugHelp() {
    Serial.println("\n=== Debug Flags ===");
    Serial.println("debug [status]       - Show current flags");
    Serial.println("debug help           - This help");
    Serial.println("debug all            - Enable all flags");
    Serial.println("debug none           - Disable all flags");
    Serial.println("debug protocol       - Toggle protocol parsing (0x01)");
    Serial.println("debug crypto         - Toggle encryption steps (0x02)");
    Serial.println("debug crc            - Toggle CRC validation (0x04)");
    Serial.println("debug commands       - Toggle command decode (0x08)");
    Serial.println("debug raw            - Toggle raw hex dumps (0x10)");
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
        if (arg == "help") {
            printDebugHelp();
        } else if (arg == "status" || arg.length() == 0) {
            Serial.println("\n=== Debug Status ===");
            Serial.printf("Flags: 0x%02X\n", debugFlags);
            Serial.print("Protocol: "); Serial.println(debugFlags & DBG_PROTOCOL ? "ON" : "OFF");
            Serial.print("Crypto: "); Serial.println(debugFlags & DBG_CRYPTO ? "ON" : "OFF");
            Serial.print("CRC: "); Serial.println(debugFlags & DBG_CRC ? "ON" : "OFF");
            Serial.print("Commands: "); Serial.println(debugFlags & DBG_COMMANDS ? "ON" : "OFF");
            Serial.print("Raw Data: "); Serial.println(debugFlags & DBG_RAW_DATA ? "ON" : "OFF");
            Serial.println("===================\n");
        } else if (arg == "all") {
            debugFlags = 0xFF;
            Serial.println("All debug flags enabled");
        } else if (arg == "none") {
            debugFlags = 0;
            Serial.println("All debug flags disabled");
        } else if (arg == "protocol") {
            debugFlags ^= DBG_PROTOCOL;
            Serial.print("Protocol debug: ");
            Serial.println(debugFlags & DBG_PROTOCOL ? "ON" : "OFF");
        } else if (arg == "crypto") {
            debugFlags ^= DBG_CRYPTO;
            Serial.print("Crypto debug: ");
            Serial.println(debugFlags & DBG_CRYPTO ? "ON" : "OFF");
        } else if (arg == "crc") {
            debugFlags ^= DBG_CRC;
            Serial.print("CRC debug: ");
            Serial.println(debugFlags & DBG_CRC ? "ON" : "OFF");
        } else if (arg == "commands") {
            debugFlags ^= DBG_COMMANDS;
            Serial.print("Commands debug: ");
            Serial.println(debugFlags & DBG_COMMANDS ? "ON" : "OFF");
        } else if (arg == "raw") {
            debugFlags ^= DBG_RAW_DATA;
            Serial.print("Raw data debug: ");
            Serial.println(debugFlags & DBG_RAW_DATA ? "ON" : "OFF");
        } else {
            Serial.println("Unknown debug option. Use 'debug help'");
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
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

#endif // COMMAND_HANDLER_H
