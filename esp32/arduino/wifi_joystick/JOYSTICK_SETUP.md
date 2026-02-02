# Physical Joystick Integration - Setup Guide
Your ESP32 WiFi joystick controller is now configured for a 6-cable analog outdoor joystick with voltage divider support.

## Your Joystick Type
- **Fancy outdoor joystick** (no button)
- **6 cables total**: likely VCC, GND, VRx, VRy, and 2 extra (TBD)
- **5V output** requiring voltage dividers for ESP32 ADC

## Pin Configuration
```
ESP32 GPIO Pins (ADC1 safe for WiFi):
├── GPIO 32 (ADC1_CH4) ← VRx (X-axis) via voltage divider
├── GPIO 33 (ADC1_CH5) ← VRy (Y-axis) via voltage divider
├── GPIO 34 (input-only) ← Extra axis 1 (or spare) via voltage divider
└── GPIO 35 (ADC1_CH7)  ← Extra axis 2 (or spare) via voltage divider

Power:
├── External 5V ← 4×AA battery pack (see Power Options below)
└── GND  ← ESP32 GND (common ground)
```

## IMPORTANT: Voltage Divider Setup
Your joystick provides **5V output**, but ESP32 ADC only tolerates **3.3V**.

**For EACH analog input (VRx, VRy, Extra), build this divider:**

```
    5V Input (from joystick)
         │
        [10kΩ resistor]
         │
    ┌────┴────┐
    │         │
   ADC      [10kΩ resistor]
   Pin       │
            GND

Output voltage: ~2.5V (safe for 3.3V ADC)
```

**Bill of Materials:**
- 6× 10kΩ resistors (for voltage dividers, need 2 per axis)
- Female-to-female jumper cables or breadboard
- 4×AA battery holder with wire leads (~$2-3 on Amazon)
- 4Identifying Your 6 Cables

When the joystick arrives, **test each cable with a multimeter**:

1. Set multimeter to **DC voltage** (20V range)
2. Find **GND** (typically black or brown wire)
3. Touch red probe to each unknown cable, black probe to GND
4. **Move the joystick** and watch voltage:
   - **0V always** → Extra GND (just connect to GND)
   - **5V always** → Extra VCC (just connect to +5V)
   - **Changes 0-5V** → Analog axis! (needs voltage divider)

**Most likely layout:**
- Cable 1: VCC (+5V power)
- Cable 2: GND (ground)
- Cable 3: VRx (X-axis, changes with left/right)
- Cable 4: VRy (Y-axis, changes with up/down)
- Cable 5: Extra axis OR extra GND
- Cable 6: Extra axis OR extra VCC

## Code Files Modified
### [device_config.h](device_config.h)
Added pin definitions and ADC configuration:
```cpp
#define JOYSTICK_VRX_PIN 32      // X-axis
#define JOYSTICK_VRY_PIN 33      // Y-axis
#define JOYSTICK_SW_PIN 34       // Extra axis 1 (GPIO34 input-only, good for ADC)
#define JOYSTICK_EXTRA_PIN 35    // Extra axis 2 (ADC1_CH7)
#define ADC_RESOLUTION 12        // 12-bit (0-4095)
#define ADC_ATTENUATION 3        // 11dB for 0-3.3V range
#define ADC_SAMPLES 10           // Averaging for noise reduction
```

**Note:** GPIO 34 is marked "SW" in the code but works fine for any analog input.lternative 1:** Use only **3×AA = 4.5V** (slightly low but usually fine)
**Alternative 2:** Add LM7805 voltage regulator for perfect 5V (requires soldering)

### Other Options
- **USB power bank** (5V USB output) - great if you have one
- **USB wall adapter** - if testing near an outlet
- **9V battery + LM7805** - works but poor capacity (not recommended)

## Code Files Modified
### [device_config.h](device_config.h)Added pin definitions and ADC configuration:
```cpp
#define JOYSTICK_VRX_PIN 32      // X-axis
#define JOYSTICK_VRY_PIN 33      // Y-axis
#define JOYSTICK_SW_PIN 34       // Button
#define JOYSTICK_EXTRA_PIN 35    // Extra axis
#define ADC_RESOLUTION 12        // 12-bit
#define ADC_ATTENUATION 3        // 11dB for 0-3.3V

### Step 1: Identify Joystick Cables
Use multimeter to identify which of the 6 cables are:
- VCC (+5V)
- GND
- VRx (X-axis)
- VRy (Y-axis)
- Cable 5 & 6 (extra axes or redundant power)

### Step 2: Build Voltage Dividers
For each **analog axis** (VRx, VRy, and any extras), build a divider:
- 2× 10kΩ resistors per axis
- If you have 2 analog axes → 4 resistors total
- If you have 4 analog axes → 8 resistors total

### Step 3: Connect Power Supply
- Get 4×AA battery holder
- Insert 4 AA batteries (or use only 3 for 4.5V)
- Red wire → Joystick VCC
- Black wire → Joystick GND and ESP32 GND (common ground!)

### Step 4: Connect Joystick to ESP32
```
Joystick Pin → ESP32 Pin
─────────────────────────
5V (VCC)     → 5V supply (battery pack, NOT ESP32 3.3V!)
GND          → ESP32 GND (common ground)
VRx          → GPIO 32 (via voltage divider)
VRy          → GPIO 33 (via voltage divider)
Extra 1      → GPIO 34 (via voltage divider, if needed)
Extra 2      → GPIO 35 (via voltage divider, if needed)
```

**Important:** Share common ground between battery, joystick, and ESP32!

### Step 5: Upload Firmware
Upload the sketch to ESP32 via USB.

### Step 6: Test & Calibrate
Connect to serial monitor (115200 baud):
```
help                    # Show all commands
joystick on             # Enable physical input
joystick once           # Check current values
joystick calibrate      # Run calibration (move joystick around)
debug                   # Enable continuous monitoring
joystick once           # Watch live values
```

**Calibration expectations:**
- Centered: ~2048 (12-bit ADC middle)
- Min: ~0-500 (joystick all the way in one direction)
- Max: ~3500-4095 (joystick all the way in other direction)
- If values are strange, recheck voltage divider wiring!

## How It Works
### ADC Reading Pipeline```
Physical Input (0-3.3V)
    ↓
[ADC 12-bit: 0-4095]
    ↓
[Average 10 samples]
    ↓
[Normalize: -1.0 to 1.0]
    ↓
[Apply deadzone: ±100 ADC units]
    ↓
JoystickState (x, y, button, extra)
    ↓
[Differential drive calc]
    ↓
[Send to wheel via BLE]
```

### Voltage Divider Math- Input: 0-5V
- Output: 0-2.5V (proportional)
- Formula: Vout = Vin × R2/(R1+R2) = Vin × 10k/(10k+10k) = Vin × 0.5
- Safety: 2.5V < 3.3V maximum, gives good ADC range (0-2000+ out of 4095)

## Serial Commands Reference
| Command | Usage | Notes |
|---------|-------|-------|
| `joystick on` | Enable physical input | Updates main joystick from ADC pins |
| `joystick off` | Disable physical input | Zeros joystick, uses web interface only |
| `joystick calibrate` | Run calibration wizard | 5-second test, shows min/max ADC values |
| `joystick once` | Show current state | One-time snapshot |
| `status` | Full system status | Includes physical input enabled/disabled |
| `debug` | Toggle debug mode | Enables continuous monitoring with `joystick` |
| `verbose` | Toggle BLE logging | Shows every command sent to wheel |

## Troubleshooting
### ADC reads only 0 or 4095- **Cause:** Voltage divider not connected properly
- **Fix:** Double-check resistor placement, verify 5V supply connected to joystick

### Joystick values always at extremes- **Cause:** Voltage divider gives wrong ratio (likely missing resistor)
- **Fix:** Run `joystick calibrate` to see actual min/max values

### Only seeing 2 axes but have 4 cables that change voltage
- **Cause:** Need to configure extra axes in firmware
- **Fix:** GPIO 34 and 35 are already set up, just connect via voltage dividers and test

### Noisy readings- **Solution:** Already handled by ADC_SAMPLES=10 averaging
- **If still noisy:** Increase ADC_SAMPLES in device_config.h (use 20-30)

- **Cause:** Using ADC2 pins (conflict with WiFi)
- **Solution:** We already use ADC1 only (GPIO 32,33,34,35), which doesn't conflict

## Next Steps

1. **Order the 4×AA battery holder** (~$2-3, arrives in 1-2 days)
2. When joystick arrives, **identify the 6 cables** with multimeter
3. **Build voltage dividers** on breadboard (2 resistors per axis)
4. **Connect everything** (battery → joystick → ESP32 via dividers)
5. **Upload firmware** and test with serial commands
6. **Fine-tune deadzone** if needed (edit `DEADZONE = 100` in joystick_input.h)
7. Test wheelchair control!

Enjoy your rugged outdoor joystick control! 🎮🦽
Enjoy your physical joystick control! 🎮
