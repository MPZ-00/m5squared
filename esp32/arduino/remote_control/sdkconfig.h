/**
 * ESP32 BLE Configuration - IMPORTANT SETUP REQUIRED
 * 
 * PROBLEM: ESP32 default BLE configuration only supports 1 GATT client connection.
 *          This causes wheels to disconnect when the second one connects.
 * 
 * SOLUTION: Increase CONFIG_BT_ACL_CONNECTIONS and CONFIG_GATTC_MAX_CONNECTIONS
 *           in the ESP32 build configuration.
 * 
 * ============================================================================
 * FOR ARDUINO IDE USERS:
 * ============================================================================
 * 
 * Option 1 (Recommended): Switch to PlatformIO
 *   1. Install PlatformIO extension in VS Code
 *   2. Create platformio.ini in remote_control folder (see below)
 *   3. Build with PlatformIO instead of Arduino IDE
 * 
 * Option 2: Modify ESP32 Core sdkconfig (affects ALL ESP32 projects)
 *   1. Find: %LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<version>\tools\sdk\sdkconfig
 *   2. Edit these lines:
 *      CONFIG_BT_ACL_CONNECTIONS=4         (change from 3)
 *      CONFIG_GATTC_MAX_CONNECTIONS=2       (change from 1) ← THIS IS THE KEY FIX
 *   3. Rebuild your sketch
 * 
 * ============================================================================
 * FOR PLATFORMIO USERS:
 * ============================================================================
 * 
 * Create platformio.ini in this folder with:
 * 
 * [env:esp32dev]
 * platform = espressif32
 * board = esp32dev
 * framework = arduino
 * build_flags = 
 *     -DCONFIG_BT_ACL_CONNECTIONS=4
 *     -DCONFIG_GATTC_MAX_CONNECTIONS=2
 * 
 * ============================================================================
 * VERIFICATION:
 * ============================================================================
 * 
 * After applying the fix, you should see in the logs:
 *   [BLE] Left wheel ready
 *   [BLE] Right wheel ready
 *   [Supervisor] Connected successfully (all wheels)
 * 
 * If wheels still disconnect when the second connects, the configuration
 * was not applied correctly.
 */

#warning "IMPORTANT: ESP32 BLE configuration requires modification for dual wheel support!"
#warning "See sdkconfig.h comments for setup instructions."

// These defines won't work in Arduino IDE - they're documentation only
// The actual configuration must be done via build flags or sdkconfig modification
