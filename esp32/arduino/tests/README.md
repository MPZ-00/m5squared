# ESP32 Arduino Tests

## Test Framework

Modular test framework with optional hardware feedback (OLED, buzzer, LEDs).

**Hardware wiring:** See [HARDWARE_SETUP.md](HARDWARE_SETUP.md)

## Quick Start

### 1. Hardware Test
```
1. Wire hardware per HARDWARE_SETUP.md
2. Install libraries: Adafruit GFX, Adafruit SSD1306
3. Upload test_hardware/test_hardware.ino
4. Verify OLED, buzzer, LEDs work
```

### 2. Create Your Test
```cpp
#include "../test_base.h"

void setup() {
    testBegin("My Test Suite");
    
    testStartSection("My Tests");
    ASSERT_EQ(expected, actual, "Test description");
    testEndSection();
    
    testSummary();
}

void loop() {
    delay(1000);
}
```

## Structure

```
tests/
├── test_base.h              # Core framework (include in all tests)
├── HARDWARE_SETUP.md        # Wiring and setup guide
├── test_hardware/           # Hardware verification
├── test_example/            # Minimal template
└── test_mapper/             # Real unit tests
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
