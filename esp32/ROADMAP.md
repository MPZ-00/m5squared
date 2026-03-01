# ESP32 M25 Remote Control - Implementation Roadmap

**Goal:** Fully standalone remote with vendor ECS + mobile app features

**Current State:**
- Unidirectional control working (sending speed commands)
- Basic ACK/NACK detection exists
- Response parsing incomplete
- No telemetry feedback
- Basic state machine (BOOT → CONNECTING → READY → OPERATING → ERROR)

**Target State:** 
- Standalone operation (no PC/phone required)
- Button + LED interface (one-handed)
- Professional-grade safety core
- Full bidirectional communication

---

## Phase 1: Core Safety
**Priority:** CRITICAL | **Dependencies:** None

### 1.1 Port Mapper
**Reference:** `core/mapper.py`

- [ ] Response curves (exponential/quadratic for fine control)
- [ ] Deadzone handling
- [ ] Acceleration ramping with delta-time tracking
- [ ] Differential drive kinematics
- [ ] Speed limiting per mode (slow/normal/fast)
- [ ] Safety interlocks

**Deliverable:** `mapper.h` - Smooth, predictable control
**Success Criteria:** No jerky movements, smooth turns

### 1.2 Port Supervisor
**Reference:** `core/supervisor.py`

- [ ] Extended state machine
  - Add INITIALIZING state (wait for wheel telemetry)
  - Proper error recovery
- [ ] Multiple watchdogs
  - Input timeout (stop if no joystick input)
  - Link timeout (detect connection drops)
  - Heartbeat keepalive
- [ ] Auto-reconnection
  - Exponential backoff
  - Retry attempt tracking
- [ ] Telemetry monitoring
- [ ] Vehicle state caching (battery, speed, distance)

**Deliverable:** `supervisor.h` - Robust fault-tolerant control
**Success Criteria:** Recovers from connection loss, no runaway conditions

### 1.3 Port Types
**Reference:** `core/types.py`

- [ ] Data structures
  - ControlState, CommandFrame, VehicleState
  - DriveMode, SupervisorState enums
  - MapperConfig, SupervisorConfig

**Deliverable:** `types.h` - Shared data structures

### 1.4 Response Parsing
**Why Critical:** Everything depends on bidirectional communication

- [ ] Response header parser
  - Extract and validate all header fields
  - Telegram ID tracking for request-response pairing
- [ ] Payload extraction framework
  - Type-safe parsing (uint8, int16_be, uint32_be)
  - ACK/NACK error code handling
- [ ] Request-response pairing
  - Request queue
  - Timeout handling
  - Retry mechanism

**Deliverable:** Complete response parsing in `m25_ble.h`
**Success Criteria:** Can parse all response types reliably

---

## Phase 2: Telemetry & UI
**Priority:** HIGH | **Dependencies:** Phase 1

### 2.1 Battery Monitoring
**Reference:** `m25_ecs.py` READ_SOC command

- [ ] Implement `READ_SOC` command (Service 0x08, Param 0x01)
- [ ] Parse response (1 byte: battery %)
- [ ] Periodic polling (configurable interval)
- [ ] Store per-wheel battery state
- [ ] Low battery warning
  - LED pattern
  - Speed limiting
  - Serial output

**Deliverable:** Real-time battery monitoring
**Success Criteria:** Accuracy matches Python implementation

### 2.2 Firmware Version
**Reference:** `m25_ecs.py` READ_SW_VERSION

- [ ] Implement `READ_SW_VERSION` command (Service 0x0A, Param 0x01)
- [ ] Parse response (4 bytes: dev_state, major, minor, patch)
- [ ] Display on boot / via serial

**Deliverable:** Firmware version diagnostics

### 2.3 Button-Based UI

- [ ] Assist level cycling (ASSIST button)
  - LED feedback (blink count)
  - Buzzer confirmation
- [ ] Profile selection (HILL_HOLD long-press)
### 2.4 Settings Persistence

- [ ] EEPROM or SPIFFS storage
  - Current profile ID
  - Assist level
  - User preferences
- [ ] Load on boot
- [ ] Save on change

**Deliverable:** Settings survive power cycle

### 2.5 Serial Interface

- [ ] Debugging commands
- [ ] Real-time telemetry output
- [ ] Status monitoring
- [ ] Wheel simulation mode (test without hardware)

**Deliverable:** Development interface for testing

---

## Phase 3: Drive Features
**Priority:** HIGH | **Dependencies:** Phase 1

### 3.1 Distance Tracking
**Reference:** `m25_ecs.py` READ_CRUISE_VALUES
---

## Phase 3: Cruise Control & Distance Tracking
**Priority:** HIGH | **Dependencies:** Phase 1 | **Effort:** Medium

### 3.1 Read Cruise Values (Week 3)
**Why High Priority:** Essential telemetry for advanced features

- [ ] Implement `READ_CRUISE_VALUES` command (Service 0x01, Param 0xD1)
- [ ] Parse response (12+ bytes)
  - Overall distance (uint32_be, 0.01m units)
  - Convert to km
  - Speed, push counter
- [ ] Periodic polling during operation
- [ ] Store per-wheel telemetry
  - Total distance (km)
  - Trip distance (resettable)
  - Average speed

**Deliverable:** Real-time distance tracking

### 3.2 Cruise Control
**Reference:** `m25_parking.py`

- [ ] Extend `WRITE_DRIVE_MODE` for cruise bit
- [ ] Speed setpoint control
- [ ] Button toggle
- [ ] Safety checks

**Deliverable:** Functional cruise control

### 3.3 Drive Profile Presets

- [ ] Use generated `profiles.h` from `tools/generate_profiles.py`
- [ ] Profile switching via button menu
- [ ] Store current profile in flash

**Deliverable:** Profile presets available on device

---

## Phase 4: Advanced Features
**Priority:** MEDIUM | **Dependencies:** Phases 1-3

### 4.1 WiFi Web Interface

- [ ] ESP32 WebServer setup
- [ ] Access point mode
- [ ] Web UI for settings
  - Profiles
  - Deadzone/curves
  - Calibration
  - Diagnostics
- [ ] REST API
- [ ] HTML/JS from PROGMEM

**Deliverable:** Phone/tablet configuration interface

### 4.2 LCD Display Support

- [ ] Display driver integration
- [ ] Status screen (battery, speed, distance)
- [ ] Menu system
- [ ] One-handed navigation

**Deliverable:** Optional display support (when hardware available)

### 4.3 Joystick Calibration

- [ ] Calibration wizard
- [ ] Store calibration in flash
- [ ] Auto-calibration on boot option

**Deliverable:** Proper joystick calibration

### 4.4 Data Logging

- [ ] Serial data export
- [ ] Telemetry logging
- [ ] Error log storage

**Deliverable:** Debugging and analysis data

### 4.5 OTA Firmware Updates

- [ ] OTA update capability
- [ ] Version management
- [ ] Rollback on failure

**Deliverable:** Remote firmware updates

---

## Phase 5: Parking Mode
**Priority:** LOW | **Dependencies:** Phase 1

### 5.1 Precision Control Mode
**Reference:** `m25_parking.py`

- [ ] Low-speed override
- [ ] Fine-grained control
  - Reduced deadzone
  - Instant response (minimal ramping)
- [ ] Incremental movements
- [ ] Safety: Max 20% speed
- [ ] Explicit mode entry (button combo)

**Deliverable:** Precise positioning control

---

## Phase 6: Advanced Diagnostics
**Priority:** LOW | **Dependencies:** Phase 1

### 6.1 Drive Profile Management

- [ ] Read drive profile (READ_DRIVE_PROFILE)
- [ ] Read profile parameters (READ_DRIVE_PROFILE_PARAMS)
- [ ] Write profile selection (WRITE_DRIVE_PROFILE)
- [ ] Write custom parameters (WRITE_DRIVE_PROFILE_PARAMS)

**Reference:** `m25_ecs.py`, `m25_ecs_driveprofiles.py`
**Deliverable:** Full profile customization

### 6.2 Memory Management

- [ ] READ_MEMORY commands (Service 0x09)
- [ ] WRITE_MEMORY commands
- [ ] Parameter validation
- [ ] Require developer mode unlock

**Reference:** `m25_protocol_data.py`
**Deliverable:** Low-level parameter access

### 6.3 Statistics & RTC

- [ ] STATS service (Service 0x0B)
  - Usage statistics
  - Runtime hours
  - Push count
- [ ] RTC service (Service 0x0E)
  - Read/write clock
  - Event timestamps

**Deliverable:** Complete diagnostic dashboard

---

## Testing Strategy

### Unit Tests
- Mapper: Response curves, ramping, differential drive
- Supervisor: State transitions, watchdogs
- Response parser: All known formats

### Integration Tests
- End-to-end command/response cycles
- Multi-wheel synchronization
- Error recovery (connection loss, battery low, NACK)

### Physical Tests (Safety-Critical)
- Test on actual wheelchair with safety measures
- Smooth acceleration/deceleration
- Emergency stop response time (<100ms)
- Watchdog triggers

---

## Dependencies

### Hardware
- ESP32 (ESP32-S3 recommended)
- M25 wheels with AES keys (from QR codes)
- Joystick, buttons
- Optional: LCD display

### Software
- Arduino IDE or PlatformIO
- mbedtls (AES encryption)
- ESP32 BLE libraries
- Python reference implementation

### Code Generators (Already Implemented)
- `tools/generate_constants.py` - Protocol constants
- `tools/generate_profiles.py` - Drive profiles
- `tools/generate_tests.py` - Test vectors

---

## Phase Priority Summary

| Phase | Priority | Blocking |
|-------|----------|----------|
| 1: Core Safety | CRITICAL | Blocks all |
| 2: Telemetry & UI | HIGH | Standalone operation |
| 3: Drive Features | HIGH | Advanced functionality |
| 4: Advanced Features | MEDIUM | Polish |
| 5: Parking Mode | LOW | Specialized |
| 6: Diagnostics | LOW | Power users |

---

## Related Files

### Python Reference Implementation
- `m25_protocol.py` - CRC, byte stuffing, encryption
- `m25_protocol_data.py` - All protocol constants
- `m25_spp.py` - Packet builder
- `m25_ecs.py` - Read/write commands, response parsing
- `m25_parking.py` - Remote control demo
- `core/mapper.py` - Control algorithms
- `core/supervisor.py` - State machine + safety

### ESP32 Current Implementation
- `esp32/arduino/remote_control/m25_ble.h` - BLE client, basic commands
- `esp32/arduino/remote_control/remote_control.ino` - Main state machine
- `esp32/arduino/remote_control/joystick.h` - Input handling
- `esp32/arduino/remote_control/motor_control.h` - Speed commands

### Documentation
- `doc/m25-protocol.md` - Protocol overview
- `doc/ble-cross-platform.md` - BLE implementation notes
- `README.md` - Project overview

---

**Document Version:** 1.0
**Last Updated:** 2026-02-28
**Status:** Draft - awaiting Phase 1 implementation kickoff
core/mapper.py` - Control algorithms
- `core/supervisor.py` - State machine + safety
- `core/types.py` - Data structures
- `m25_protocol.py` - CRC, byte stuffing, encryption
- `m25_protocol_data.py` - Protocol constants
- `m25_ecs.py` - Command implementation
- `m25_parking.py` - Remote control demo

### ESP32 Current Implementation
- `esp32/arduino/remote_control/m25_ble.h` - BLE client, commands
- `esp32/arduino/remote_control/remote_control.ino` - Main state machine
- `esp32/arduino/remote_control/joystick.h` - Input handling
- `esp32/arduino/remote_control/motor_control.h` - Speed commands
- `esp32/arduino/remote_control/constants.h` - Generated constants
- `esp32/arduino/remote_control/profiles.h` - Generated profiles

### Code Generators
- `tools/generate_constants.py`
- `tools/generate_profiles.py`
- `tools/generate_tests.py`

### Documentation
- `esp32/HYBRID_ARCHITECTURE.md` - Architecture overview
- `doc/m25-protocol.md` - Protocol specification
- `doc/ble-cross-platform.md` - BLE implementation notes

---

**Status:** Code generators complete, ready for Phase 1 implementation