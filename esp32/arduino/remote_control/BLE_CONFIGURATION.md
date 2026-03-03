# ESP32 Dual Wheel BLE Configuration

## Problem

The ESP32 BLE stack's default configuration only supports **1 simultaneous GATT client connection**. This causes the first wheel to disconnect when you try to connect the second wheel.

**Symptoms:**
```
[BLE] Left wheel ready
[BLE] Connecting to Right wheel...
[BLE] Left wheel disconnected  ← LEFT DIES!
[BLE] Right wheel: Notifications enabled
[BLE] Right wheel disconnected ← THEN RIGHT DIES!
```

## Root Cause

The ESP32 Arduino Core is compiled with:
- `CONFIG_BT_ACL_CONNECTIONS=3` (total Bluetooth connections)
- `CONFIG_GATTC_MAX_CONNECTIONS=1` ← **This is the problem!**

We need to increase `CONFIG_GATTC_MAX_CONNECTIONS` to **2** for dual wheel support.

---

## Solution Options

### Option 1: PlatformIO (Recommended ✅)

**Advantages:**
- Clean, project-specific configuration
- Easy to version control
- No global changes to ESP32 core
- Uses ESP-IDF 5.x with better BLE support

**Important: Custom Platform Required**
This project uses a **custom ESP32 platform** from pioarduino (ESP-IDF 5.x based) instead of the standard PlatformIO espressif32 platform. This provides:
- Enhanced BLE stack with better multi-connection support
- More recent toolchain and bug fixes
- Better compatibility with dual GATT client connections

**DO NOT change the platform line** in platformio.ini - it must remain:
```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
```

**Steps:**
1. Install PlatformIO extension in VS Code
2. The `platformio.ini` file is already configured correctly
3. Ensure `sdkconfig.defaults` exists with:
   ```
   CONFIG_GATTC_MAX_CONNECTIONS=2
   CONFIG_BT_ACL_CONNECTIONS=4
   ```
4. Open this folder in PlatformIO (`File → Open Folder`)
5. Click the PlatformIO icon → Build
6. Upload to ESP32

The build flags in `platformio.ini` automatically configure the BLE stack:
```ini
build_flags = 
    -DCONFIG_BT_ACL_CONNECTIONS=4
    -DCONFIG_GATTC_MAX_CONNECTIONS=2
```
The configuration is applied through both `sdkconfig.defaults` (embedded config) and `build_flags` (compile-time defines).


---

### Option 2: Arduino IDE (Manual Core Modification)

**Advantages:**
- Works with existing Arduino IDE setup

**Disadvantages:**
- Affects ALL ESP32 projects (global change)
- Must redo after ESP32 core updates

**Steps:**

#### Windows:
1. Close Arduino IDE
2. Navigate to: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<version>\tools\sdk\`
3. Find `sdkconfig` (no extension)
4. Open in text editor (Notepad++)
5. Find and change these lines:
   ```
   CONFIG_BT_ACL_CONNECTIONS=4
   CONFIG_GATTC_MAX_CONNECTIONS=2
   ```
6. Save and close
7. Restart Arduino IDE
8. Do a **clean build** (delete build folder, or Sketch → Clean)
9. Upload sketch

#### macOS:
1. Path: `~/Library/Arduino15/packages/esp32/hardware/esp32/<version>/tools/sdk/sdkconfig`
2. Follow same editing steps as Windows

#### Linux:
1. Path: `~/.arduino15/packages/esp32/hardware/esp32/<version>/tools/sdk/sdkconfig`
2. Follow same editing steps as Windows

---

### Option 3: Custom Board Definition (Advanced)

Create a custom ESP32 board definition with modified BLE settings. See ESP32 Arduino Core documentation for details.

---

## Verification

After applying the fix, both wheels should stay connected:

```
[BLE] Left wheel ready
[BLE] Right wheel ready
[Supervisor] Connected successfully (all wheels)
```

If wheels still disconnect, the configuration was not applied correctly. Check:
1. Build output for `-DCONFIG_GATTC_MAX_CONNECTIONS=2` flag
2. Clean build was performed (Arduino IDE: delete build folder)
3. Correct sdkconfig file was modified (check ESP32 core version)

---

## Technical Details

### BLE Stack Internals

The ESP32 uses Bluedroid BLE stack by default. Connection limits are:
- **ACL connections**: Low-level Bluetooth connections (3 by default)
- **GATT server connections**: ESP32 acting as peripheral (3 by default)
- **GATT client connections**: ESP32 acting as central/client (1 by default!)

Our remote control acts as a **GATT client**, connecting to two M25 wheel **GATT servers**. We need at least 2 client connections.

### Alternative: NimBLE

NimBLE is an alternative BLE stack that's more memory-efficient but requires more code changes. Consider if dual Bluedroid clients don't work.

---

## Troubleshooting

**Q: After editing sdkconfig, wheels still disconnect**
- Ensure you did a **clean build** (delete build folder)
- Verify correct sdkconfig file (match ESP32 core version)
- Check serial monitor for `[BLE] Created new BLE client (address: 0x...)` - should see two different addresses

**Q: Can't find sdkconfig file**
- Verify ESP32 board is installed in Arduino IDE
- Check ESP32 core version (Tools → Board → Boards Manager → esp32)
- Path includes version number, e.g., `.../esp32/2.0.11/tools/sdk/sdkconfig`

**Q: PlatformIO build fails**
- Ensure `platform = espressif32` is correct
- Try `platform = espressif32@6.4.0` (specific version)
- Check `pio lib install` ran successfully

---

## References

- [ESP-IDF BLE Configuration](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/index.html)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
