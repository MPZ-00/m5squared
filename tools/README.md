# Code Generation Tools

These tools generate C++ headers from the Python reference implementation, ensuring consistency between platforms without manual synchronization.

## Tools Overview

### 1. `generate_constants.py`
Generates `constants.h` from `m25_protocol_data.py`

**What it generates:**
- Protocol packet structure positions
- Service IDs and parameter IDs
- Device IDs (source/destination)
- ACK/NACK error codes
- System mode and drive mode flags
- Assist levels and profile IDs
- ~100+ protocol constants

**Output:** `esp32/arduino/remote_control/constants.h`

**Usage:**
```bash
python tools/generate_constants.py
```

---

### 2. `generate_profiles.py`
Generates `profiles.h` from `m25_ecs_driveprofiles.py`

**What it generates:**
- Drive profile presets (Standard, Active, Sensitive, Soft, Gentle, SensitivePlus)
- Profile parameter structures (6 profiles × 2 assist levels = 12 parameter sets)
- Helper functions to load profiles from PROGMEM
- Speed conversion utilities

**Output:** `esp32/arduino/remote_control/profiles.h`

**Usage:**
```bash
python tools/generate_profiles.py
```

---

### 3. `generate_tests.py`
Generates C++ unit tests from Python reference implementation

**What it generates:**
- Mapper test vectors (validates response curves, deadzone, differential drive)
- Supervisor state machine tests (validates state transitions, timeouts, recovery)
- ~20+ test cases derived from Python behavior

**Output:** 
- `esp32/test/test_mapper.cpp`
- `esp32/test/test_supervisor.cpp`

**Usage:**
```bash
python tools/generate_tests.py
```

**Run tests:**
```bash
cd esp32/test/
pio test
```

---

## Development Workflow

### Initial Setup
```bash
# Generate all headers before first build
python tools/generate_constants.py
python tools/generate_profiles.py
python tools/generate_tests.py
```

### After Protocol Changes
```bash
# Update m25_protocol_data.py, then:
python tools/generate_constants.py
```

### After Profile Changes
```bash
# Update m25_ecs_driveprofiles.py, then:
python tools/generate_profiles.py
```

### After Core Algorithm Changes
```bash
# Update core/mapper.py or core/supervisor.py, then:
python tools/generate_tests.py
# Run tests to ensure C++ still matches Python
cd esp32/test/ && pio test
```

---

## Integration with Build System

### Option 1: Pre-build Hook (PlatformIO)
Add to `esp32/platformio.ini`:

```ini
[env:esp32]
extra_scripts = pre:generate_headers.py
```

Create `esp32/generate_headers.py`:
```python
Import("env")
import subprocess

print("Generating C++ headers from Python...")
subprocess.run(["python", "tools/generate_constants.py"])
subprocess.run(["python", "tools/generate_profiles.py"])
```

### Option 2: Manual (Current)
Run generators manually before building:
```bash
python tools/generate_constants.py
python tools/generate_profiles.py
arduino-cli compile esp32/arduino/remote_control/
```

### Option 3: Makefile
Create `esp32/Makefile`:
```makefile
.PHONY: generate build upload test

generate:
	python ../tools/generate_constants.py
	python ../tools/generate_profiles.py

build: generate
	arduino-cli compile arduino/remote_control/

upload: build
	arduino-cli upload -p COM3 arduino/remote_control/

test: generate
	cd test && pio test
```

Then simply: `make build`

---

## Benefits

### ✅ Single Source of Truth
Protocol constants and profiles defined once in Python, used everywhere.

### ✅ No Manual Synchronization
Changes to Python automatically propagate to C++ after running generator.

### ✅ Type Safety
Generated C++ uses proper types (uint8_t, uint16_t, etc.) and const correctness.

### ✅ Memory Efficiency
Profile presets stored in PROGMEM (flash), not RAM.

### ✅ Validated Behavior
Test generators ensure C++ mapper/supervisor match Python reference exactly.

### ✅ Documentation in Code
Generated headers include comments explaining each constant's purpose.

---

## File Sizes (Estimated)

| Generated File | Size | Location |
|----------------|------|----------|
| constants.h | ~8 KB | Flash (code) |
| profiles.h (code) | ~2 KB | Flash (code) |
| profiles.h (data) | ~1 KB | Flash (PROGMEM) |
| test_mapper.cpp | ~4 KB | Test only |
| test_supervisor.cpp | ~3 KB | Test only |

**Total runtime footprint:** ~11 KB flash, ~0 KB RAM (PROGMEM used for data)

---

## Maintenance

### When to Regenerate

**constants.h:** After any change to `m25_protocol_data.py`
- New service IDs
- New parameter IDs
- New protocol constants

**profiles.h:** After any change to `m25_ecs_driveprofiles.py`
- Profile parameter adjustments
- New profile presets
- Speed/torque limit changes

**test_*.cpp:** After any change to `core/mapper.py` or `core/supervisor.py`
- Algorithm changes
- New safety features
- State machine modifications

### Keeping Python and C++ in Sync

1. **Make changes in Python first** - It's the reference implementation
2. **Run generators** - Propagate constants/profiles to C++
3. **Port algorithm changes** - Manually update C++ mapper/supervisor
4. **Regenerate tests** - Ensure C++ matches Python behavior
5. **Run tests** - Validate parity

---

## Adding New Generators

To add a new generator (e.g., error messages, configuration templates):

1. Create `tools/generate_<name>.py`
2. Follow the pattern:
   ```python
   import sys
   from pathlib import Path
   sys.path.insert(0, str(Path(__file__).parent.parent))
   
   import source_module
   
   def generate_header(output_path):
       with open(output_path, 'w') as f:
           f.write("/* AUTO-GENERATED */\n")
           # ... generate code ...
   
   if __name__ == '__main__':
       generate_header('esp32/arduino/remote_control/generated.h')
   ```
3. Update this README
4. Add to build system if automatic generation desired

---

## Troubleshooting

### "Module not found" error
```
ModuleNotFoundError: No module named 'm25_protocol_data'
```

**Solution:** Run from repository root, not from tools/ directory:
```bash
cd m5squared_win/
python tools/generate_constants.py  # ✅ Correct
```

### Generated file not found during build
**Solution:** Run generators before building:
```bash
python tools/generate_constants.py
python tools/generate_profiles.py
# Then build
```

### C++ code doesn't match Python behavior
**Solution:** 
1. Regenerate tests: `python tools/generate_tests.py`
2. Run tests: `cd esp32/test && pio test`
3. Fix C++ implementation to match test expectations
4. Repeat until tests pass

---

## Future Enhancements

Potential additions:
- [ ] Configuration template generator (default settings)
- [ ] Error message string table generator
- [ ] Serial command protocol generator (both Python and C++)
- [ ] Continuous integration check (verify headers are up-to-date)
- [ ] Documentation generator (protocol spec from constants)
