# ESP32 Arduino Tests

## Test Framework

Unit tests for remote_control components. Each test is a standalone Arduino sketch.

## Quick Start (Arduino IDE)

### 1. Prepare Test Build Environment
```powershell
cd esp32/arduino/tests
.\prepare_test.ps1 -TestName test_supervisor
```

This creates/updates `_build_test_supervisor/` with:
- All files from `remote_control/` (headers, sources)
- Your test as `_build_test_supervisor.ino`

### 2. Open in Arduino IDE
```powershell
arduino _build_test_supervisor\_build_test_supervisor.ino
```
(Where `arduino` is alias to Arduino IDE.exe)

Or manually:
1. Open `_build_test_supervisor/_build_test_supervisor.ino`
2. Select **Tools > Board > ESP32 Dev Module**
3. Select your COM port
4. Click **Verify** to compile
5. Click **Upload** to run tests
6. Open **Serial Monitor** (115200 baud) to see results

### 3. Make Changes
- Edit files in `remote_control/` (production code)
- Edit test in `test_supervisor/` (test code)
- Run `prepare_test.ps1` again to rebuild

**Note:** `_build_test/` is reused and git-ignored.

## Alternative: Command Line (Automated)

```powershell
cd esp32/arduino/tests
.\run_tests.ps1 -Board "esp32:esp32:esp32" -Port "COM8"
```

Requires: Arduino CLI (`winget install Arduino.ArduinoCLI`)

## Structure

```
tests/
├── prepare_test.ps1         # Prepare test for Arduino IDE
├── run_tests.ps1           # Automated CLI testing
├── test_supervisor/        # Supervisor state machine tests
│   └── test_supervisor.ino
├── test_mapper/            # Mapper safety tests
│   └── test_mapper.ino
└── _build_test_supervisor/ # Temporary (git-ignored)
    ├── *.h, *.cpp          # Copied from remote_control/
    └── _build_test_supervisor.ino
```

## API Reference

### Framework
```cpp
testBegin("Suite Name");          // Initialize
testStartSection("Section");      // Start section
testEndSection();                  // End section
testSummary();                     // Final results
```

### Assertions
```cpp
ASSERT_EQ(expected, actual, "msg");
ASSERT_NE(val1, val2, "msg");
ASSERT_NEAR(expected, actual, tolerance, "msg");
ASSERT_TRUE(condition, "msg");
ASSERT_FALSE(condition, "msg");
```

### Hardware Control
```cpp
// Buzzer
buzzStartup();  buzzTestStart();  buzzTestEnd();
buzzSuccess();  buzzFailure();

// LEDs
ledStatusOn/Off();     ledStatusBlink(n);
ledErrorOn/Off();      ledErrorBlink(n);

// Display
displayUpdate("Line1", "Line2", "Line3", "Line4");
displayTestStatus();
```

## Pin Configuration

| Component  | Pin     | Notes          |
|------------|---------|----------------|
| OLED SDA   | GPIO 21 | I2C Data       |
| OLED SCL   | GPIO 22 | I2C Clock      |
| Buzzer     | GPIO 25 | Signal         |
| Status LED | GPIO 26 | + 330Ω to GND  |
| Error LED  | GPIO 27 | + 330Ω to GND  |

Change pins in `test_base.h` if needed.

## Libraries Required

Install via Arduino IDE → Manage Libraries:
- Adafruit GFX Library
- Adafruit SSD1306

## Usage Modes

**Serial Only (No Hardware)**
- Include test_base.h
- Use assertions
- Serial Monitor shows results

**With Hardware (Full Feedback)**
- Wire components per HARDWARE_SETUP.md
- OLED displays status
- Buzzer provides audio feedback
- LEDs indicate pass/fail

## Troubleshooting

- **OLED blank**: Try I2C address 0x3D (edit test_base.h)
- **No buzzer**: Verify ACTIVE buzzer type
- **LED not lighting**: Check polarity (long leg = +)

See HARDWARE_SETUP.md for details.
