# ESP32 One-Handed Remote Control - Design Specification

## Project Overview
Design and prototype a standalone, one-handed remote control device for Alber e-motion M25 power-assist wheels using ESP32 with WiFi + Bluetooth capabilities. This specification prioritizes safety, ergonomics, and usability while supporting experimental development and client feedback iteration.

**Target Platform**: ESP32 (WiFi + Bluetooth)
**Control Method**: Python-based control software (leveraging existing m5squared codebase)
**Primary Use Case**: Wheelchair remote operation with parking/assistance features

---

## MUST-HAVE Features (Essential for MVP)

### 1. Core Hardware Components
- **ESP32 Development Board** with WiFi + Bluetooth
  - Dual-core processor for concurrent BLE + WiFi operations
  - Sufficient GPIO pins for inputs/outputs
  - USB-C programming/charging interface
  - 3.3V compatible I/O

- **2-Axis Analog Joystick**
  - Spring-return to center (CRITICAL for safety)
  - 12-bit ADC resolution (0-4095)
  - Integrated push button (deadman switch candidate)
  - GPIO pins: 34 (X-axis), 35 (Y-axis), 25 (button)

- **Emergency Stop Button**
  - Large, red, tactile momentary button
  - Immediate access position (top edge, reachable with index finger)
  - Hardware priority: Direct connection to safety logic
  - Must override all other inputs

- **Power Management**
  - LiPo battery (1000-2000mAh, ~3.7V nominal)
  - Battery charging circuit (TP4056 or similar)
  - Low battery detection (<15% remaining)
  - Power on/off switch

### 2. Essential Controls (Physical Switches)
Priority-ordered by frequency of use:

1. **Joystick** (most frequent)
   - One-handed thumb operation
   - Proportional control for wheelchair movement
   - Deadman button integration (press-to-enable motion)

2. **Assist Level Selector** (frequent)
   - 3-position switch or up/down buttons
   - Levels: 1 (Normal), 2 (Outdoor), 3 (Learning)
   - Thumb-accessible position

3. **Hill Hold Toggle** (moderate)
   - Momentary or latching button
   - Enable/disable slope holding
   - Side-mounted, index/middle finger access

4. **Power Button** (infrequent)
   - Momentary push button
   - Wake/sleep device and connect to wheels
   - Protected against accidental activation

### 3. Essential Indicators (Visual Feedback)
Minimum viable status display:

- **Connection Status LED** (Green/Blue)
  - Solid: Connected to both wheels
  - Blinking: Connecting or partial connection
  - Off: Disconnected

- **Battery Level Indicator**
  - 3-5 segment LED bar OR single RGB LED
  - Green (>70%), Yellow (30-70%), Red (<30%)
  - Blinking red for critical battery (<15%)

- **Assist Level LEDs** (3 individual)
  - Label: 1, 2, 3
  - One lit = current active level
  - Position: Near assist level switch

- **Emergency/Error LED** (Red)
  - Solid: E-stop activated
  - Blinking: Connection error or timeout
  - Off: Normal operation

### 4. Essential Safety Features

- **Deadman Switch**
  - OPTION A: Joystick integrated button (press to enable)
  - OPTION B: Capacitive touch strip (grip-to-enable)
  - Behavior: Release = immediate stop
  - Implementation: Both hardware AND software enforcement

- **Watchdog Timer**
  - Auto-stop if no joystick input for 3-5 seconds
  - Visual/audio warning at 3 seconds
  - Reset on any control input

- **Connection Loss Handling**
  - Detect Bluetooth disconnection within 500ms
  - Immediate stop command to wheels (if possible)
  - Visual/audio alert to user
  - Prevent movement until reconnection confirmed

- **Low Battery Protection**
  - Warning at 20% battery
  - Auto-disconnect at 10% battery
  - Prevent new connections below 15%

- **Power-On Safety Check**
  - Verify joystick is centered before enabling remote mode
  - Confirm emergency stop is not pressed
  - Require user confirmation (button press) to activate

### 5. Software Requirements (Python + MicroPython)

- **MicroPython on ESP32**
  - Joystick reading with deadzone (±200 ADC units from center)
  - BLE connection to M25 wheels (leverage existing protocol)
  - Speed calculation: joystick → left/right wheel speeds
  - Command transmission: 20Hz update rate (50ms intervals)
  - Safety timeouts and watchdog implementation

- **WiFi Configuration Interface** (Nice-to-have for MVP)
  - Web-based setup page (ESP32 as AP)
  - Configure wheel MAC addresses
  - Input encryption keys (QR code derived)
  - Save configuration to ESP32 flash

- **Python Development Tools**
  - Leverage existing m5squared codebase
  - Protocol implementation from `m25_protocol.py`
  - Bluetooth SPP connection utilities
  - Testing and debugging scripts

---

## NICE-TO-HAVE Features (Post-MVP)

### Display Enhancement
- **OLED Display** (0.96" or 1.3" I2C)
  - Battery percentage (numeric)
  - Current assist level (text + icon)
  - Connection status (icon + text)
  - Speed visualization (left/right wheels)
  - Error messages and warnings
  - Settings menu

### Additional Controls
- **Drive Profile Selector**
  - Rotary encoder or multi-position switch
  - Profiles: Standard, Sensitive, Soft, Active, Sensitive+, Custom
  - Less frequent use → back/bottom placement

- **Cruise Control Toggle**
  - Latching button or toggle switch
  - Enable/disable cruise mode
  - Test compatibility with remote mode

- **Speed Limiter Button**
  - Hold for higher speed limit unlock
  - Default: 60% max speed
  - Held: 100% max speed (safety feature)

### Enhanced Feedback
- **Haptic Feedback**
  - Vibration motor for alerts (connection loss, low battery)
  - Tactile confirmation of mode changes
  - Deadzone exit feedback

- **Audio Alerts** (Buzzer)
  - Connection established/lost
  - Low battery warning
  - Error conditions
  - Mode changes (optional, can be annoying)

### Advanced Safety
- **Redundant Joystick**
  - Dual joystick input with averaging
  - Fail-safe if one joystick fails

- **Tilt/Orientation Sensor** (IMU)
  - Detect if device is dropped
  - Auto-stop if device orientation is abnormal

- **Gesture Detection**
  - Recognize joystick patterns for special commands
  - Example: Circle motion = cancel cruise control

### Connectivity Enhancements
- **WiFi Remote Monitoring**
  - Web dashboard for status monitoring
  - Debug logs accessible via browser
  - Remote firmware update OTA

- **Bluetooth Multi-Device**
  - Save multiple wheelchair profiles
  - Quick switch between devices

---

## Ergonomic Design Considerations

### Form Factor Options

#### Option A: Handheld Grip Device
```
Dimensions: ~140mm (L) × 100mm (W) × 30-35mm (H)
Weight target: 150-200g with battery

Layout:
- Joystick: Center, thumb-operated
- E-stop: Top edge, index finger reach
- Assist +/-: Side buttons, thumb reach
- Power: Rear center, middle/ring finger
- Hill hold: Side button, index/middle finger
- LEDs: Top edge and sides (visible from any angle)
- Display: Center top (optional, if space allows)
```

#### Option B: Stick-Mounted Controller
```
Device mounts to wheelchair armrest or handlebar
Extended "neck" for mounting clamp
All controls on device "head"

Advantages:
- No need to hold constantly
- Always in same position
- Can be larger/heavier
- More LEDs/display space

Disadvantages:
- Not ambidextrous
- Requires mounting hardware
- Less portable
```

#### Option C: Wearable/Wrist Mount
```
Velcro strap or clip attachment to wrist/forearm
Lighter weight requirement (<150g)
Compact joystick (smaller travel)

Advantages:
- Always accessible
- Hands-free when not in use
- Portable

Disadvantages:
- Limited control size
- May interfere with normal hand use
- Uncomfortable for extended use
```

### Recommended: Option A (Handheld) for Prototype
- Most flexible for experimentation
- Easy to modify and iterate
- Client can test different grips/positions
- Can later adapt to Options B or C based on feedback

### Grip and Handling (Option A Details)

**One-Handed Operation Principles:**
1. **Primary control (joystick)**: Thumb position, natural arc
2. **Emergency controls**: Index finger immediate reach
3. **Frequent controls**: Thumb + index accessible without re-grip
4. **Infrequent controls**: Require slight hand adjustment

**Hand Position Scenarios:**

**Right-Handed Use:**
```
    ╔═══════════════════╗
    ║  [E-STOP]    LEDs ║
    ║                   ║
    ║    [Display]      ║
    ║                   ║
    ║       ╔═○═╗       ║  ← Thumb on joystick
    ║       Joyst       ║
    ║   [▲]      [HILL] ║  ← Assist +/- (thumb)
    ║   [▼]      [PWR]  ║
    ╚═══════════════════╝
         │ grip │
         └──────┘ Palm + 4 fingers wrap sides/back
```

**Left-Handed Use (mirror layout):**
Device should support mirrored button operation OR
symmetric layout where both sides have duplicate controls

### Deadman Switch Implementation

#### Option 1: Joystick Integrated Button
- Press down on joystick cap to enable movement
- Release = immediate stop
- **Pros**: Natural grip, already part of joystick
- **Cons**: Fatigue from constant pressing

#### Option 2: Capacitive Touch Strip (RECOMMENDED)
- Conductive strip on device sides/back
- Detects skin contact (palm/fingers)
- Similar to touch-dimmer lamp technology
- **Pros**: Natural grip activation, less fatigue
- **Cons**: May require ESP32 capacitive touch GPIO (built-in)
- **Implementation**: ESP32 pins 0, 2, 4, 12, 13, 14, 15, 27, 32, 33 have touch sensors

**Recommended**: Capacitive touch on device back/sides
- User naturally holds device → contact detected
- Release grip → auto-stop
- Less finger fatigue than button press
- More intuitive "presence detection"

### Joystick Control

**One-Finger Operation (from above):**
- Joystick cap: 15-20mm diameter, textured
- Height above surface: 8-12mm
- Travel distance: ±10-15mm from center
- Force required: 50-150g (light to medium)
- Tactile center detent: Optional but recommended

**Thumb Operation (natural arc):**
- Position joystick 30-40mm from device edge
- Angle: Slight tilt toward thumb rest position (5-10°)
- Clearance: Adequate for full deflection without finger interference

---

## Design Process & Client Iteration

### Phase 1: Breadboard Prototype (Current Stage)
**Goal**: Validate core functionality without ergonomics

- [ ] ESP32 basic setup (MicroPython installed)
- [ ] Joystick reading and calibration
- [ ] BLE connection to M25 wheels (single wheel test)
- [ ] Basic movement control (forward/backward)
- [ ] Emergency stop implementation (button)
- [ ] LED indicators (connection + battery)
- [ ] Python control scripts (PC-side development)
- [ ] Safety timeout testing

**Output**: Working proof-of-concept demonstrating:
- ESP32 can control wheels via BLE
- Joystick provides proportional control
- Safety features function correctly
- Identify latency/reliability issues

### Phase 2: Functional Mockup
**Goal**: Test ergonomics with client

- [ ] 3D printed enclosure (basic form factor)
- [ ] All essential controls installed
- [ ] LED indicators functional
- [ ] Battery operation (not just USB power)
- [ ] Client testing session:
  - Grip comfort (5-10 minute continuous use)
  - Button reachability
  - Joystick sensitivity
  - Emergency stop accessibility
  - Display readability (if included)

**Client Questions to Answer:**
1. Handheld vs. stick-mounted preference?
2. Deadman switch: button vs. capacitive touch?
3. Display: necessary or LEDs sufficient?
4. Control frequency: which buttons used most?
5. Size/weight: too large, too small, just right?

### Phase 3: Refined Prototype
**Goal**: Incorporate client feedback

- [ ] Redesigned enclosure based on feedback
- [ ] Adjusted button/joystick positions
- [ ] Display integration (if requested)
- [ ] Additional nice-to-have features
- [ ] Improved battery life optimization
- [ ] Firmware stability improvements

### Phase 4: Final Design
**Goal**: Production-ready device

- [ ] Polished enclosure (ergonomic curves)
- [ ] Durable materials (ABS + TPU grip surfaces)
- [ ] Proper mounting solution (if applicable)
- [ ] Comprehensive user manual
- [ ] Safety certification considerations

---

## Technical Specifications

### ESP32 Module Requirements
- **Recommended**: ESP32-WROOM-32 or ESP32-WROVER
- **Features needed**:
  - Bluetooth Classic + BLE (dual-mode)
  - WiFi 802.11 b/g/n
  - Minimum 4MB flash
  - 520KB SRAM
  - 2x ADC (for joystick X/Y)
  - Capacitive touch GPIO (8+ pins available)
  - UART/USB for programming

### Power Budget (Rough Estimates)
| Component | Current Draw | Notes |
|-----------|--------------|-------|
| ESP32 (active BT) | 160-240mA | Peak during transmission |
| ESP32 (WiFi off) | 80-120mA | BLE only mode |
| OLED display | 15-30mA | When backlit/active |
| LEDs (all on) | 20-60mA | Depends on count and brightness |
| Joystick | <1mA | Passive potentiometers |
| Other switches | <5mA | Pull-up resistors |
| **Total (active)** | ~250-350mA | Typical operation |
| **Battery runtime** | 3-6 hours | With 1500mAh LiPo |

### Communication Protocol
- **M25 Bluetooth**: SPP (Serial Port Profile), Channel 6
- **Encryption**: AES-128, per-wheel keys from QR codes
- **Command rate**: 20Hz (50ms interval) for movement
- **Packet structure**: See `m5squared` protocol documentation

### GPIO Pin Assignment (Suggested)
```python
# Analog Inputs
JOYSTICK_X_PIN = 34  # ADC1_CH6
JOYSTICK_Y_PIN = 35  # ADC1_CH7

# Digital Inputs
JOYSTICK_BTN_PIN = 25        # Deadman button (pull-up)
EMERGENCY_STOP_PIN = 26      # E-stop button (pull-up)
ASSIST_UP_PIN = 27           # Assist level increase
ASSIST_DOWN_PIN = 14         # Assist level decrease
HILL_HOLD_PIN = 12           # Hill hold toggle
POWER_BTN_PIN = 13           # Power/wake button

# Capacitive Touch (Deadman Alternative)
TOUCH_DEADMAN_PIN = 32       # Capacitive touch sensor

# Output (LEDs)
LED_CONN_PIN = 16            # Connection status (green)
LED_BATTERY_R_PIN = 17       # Battery red segment
LED_BATTERY_Y_PIN = 5        # Battery yellow segment
LED_BATTERY_G_PIN = 18       # Battery green segment
LED_ASSIST_1_PIN = 19        # Assist level 1
LED_ASSIST_2_PIN = 21        # Assist level 2
LED_ASSIST_3_PIN = 22        # Assist level 3
LED_ERROR_PIN = 23           # Error/emergency indicator

# I2C (Display, optional)
I2C_SDA_PIN = 21             # OLED display data
I2C_SCL_PIN = 22             # OLED display clock

# Battery Monitoring
BATTERY_ADC_PIN = 36         # Battery voltage divider (ADC1_CH0)
```

---

## Safety Critical Requirements

### Non-Negotiable Safety Rules

1. **Joystick Spring-Return**
   - MUST return to center when released
   - Test before every use
   - Use high-quality joystick module (avoid cheap failures)

2. **Deadman Switch Enforcement**
   - Must be active during ALL movement commands
   - Release = immediate stop (within 50ms)
   - Both hardware (GPIO state) AND software checks

3. **Watchdog Timeout**
   - No control input for >5 seconds = auto-stop
   - Visual warning at 3 seconds
   - Cannot be disabled by user

4. **Emergency Stop Priority**
   - Overrides ALL other inputs
   - Hardware interrupt priority
   - Sends stop command to wheels immediately
   - Requires explicit reset before re-enabling movement

5. **Power-On State**
   - Device always starts in SAFE mode (no movement)
   - Joystick must be centered for 2 seconds before enabling
   - User must acknowledge safety message (button press)

6. **Connection Loss**
   - Detect disconnect within 500ms
   - Attempt stop command transmission
   - Lock out movement until reconnection + safety check

7. **Low Battery Lockout**
   - <15%: Prevent new remote mode activation
   - <10%: Force disconnect and shutdown
   - Continuous battery monitoring (every 10s)

### Testing Requirements
Before client handoff, verify:
- [ ] E-stop works from any state (100% success)
- [ ] Deadman switch stops movement reliably
- [ ] Timeout auto-stop functions correctly
- [ ] Connection loss is detected and handled
- [ ] Low battery triggers warnings/lockout
- [ ] Joystick spring return is functional
- [ ] Calibration is accurate and persistent

---

## Development Roadmap

### Milestone 1: ESP32 Basic Control (Week 1-2)
- Set up ESP32 development environment
- Install MicroPython firmware
- Test joystick reading (ADC calibration)
- Implement deadzone and center detection
- Test basic GPIO (buttons, LEDs)
- Validate capacitive touch (if using)

### Milestone 2: Bluetooth Integration (Week 2-3)
- Port m5squared protocol to MicroPython
- Establish BLE connection to ONE M25 wheel
- Send basic commands (connect, disconnect)
- Test movement commands (forward, stop)
- Implement encryption/decryption
- Measure latency and reliability

### Milestone 3: Safety Features (Week 3-4)
- Emergency stop implementation + testing
- Deadman switch (button or capacitive)
- Watchdog timer with visual countdown
- Connection loss detection
- Battery monitoring and warnings
- Power-on safety sequence

### Milestone 4: Full Control (Week 4-5)
- Two-wheel coordinated control
- Joystick → differential steering
- Assist level switching
- Hill hold toggle
- All LED indicators functional
- Command update rate optimization (20Hz)

### Milestone 5: Ergonomic Prototype (Week 5-6)
- Design 3D printable enclosure
- Print and assemble prototype
- Install all components in enclosure
- Test with client (ergonomics session)
- Gather feedback and iterate

### Milestone 6: Refinement (Week 6+)
- Incorporate client feedback
- Add nice-to-have features (display, haptics, etc.)
- Optimize battery life
- Firmware stability and bug fixes
- Documentation and user manual

---

## Client Questions for Feedback Session

### Ergonomics
1. How does the device feel in your hand?
   - Too large / too small / just right?
   - Comfortable for 5-10 minute continuous use?
   - Grip fatigue? Hand strain?

2. Are all controls reachable without re-gripping?
   - Joystick easy to operate?
   - Emergency stop immediate access?
   - Assist level buttons convenient?

3. Handheld vs. mounted preference?
   - Would you prefer this attached to wheelchair?
   - Or as a portable handheld device?
   - Both options available?

### Control Feel
4. Joystick sensitivity:
   - Too sensitive / too sluggish / just right?
   - Need more/less deadzone?
   - Prefer different speed curve?

5. Deadman switch preference:
   - Button (press to enable) OR
   - Capacitive touch (grip to enable)?
   - Which feels more natural/less fatiguing?

### Display vs. LEDs
6. Information display:
   - Are LEDs sufficient for status?
   - Would you prefer a small OLED screen?
   - What info is most critical to see?

### Additional Features
7. Which features are most valuable?
   - Cruise control?
   - Drive profile switching?
   - Speed limiter?
   - Audio feedback?
   - Haptic (vibration) alerts?

### Safety Preferences
8. Timeout duration:
   - 5 seconds auto-stop too fast/slow?
   - Warning at 3 seconds sufficient?

9. Emergency stop behavior:
   - Should it require reset button press?
   - Or automatically re-enable after release?

---

## Repository Integration

### Leverage Existing m5squared Codebase
- **Protocol**: `m25_protocol.py`, `m25_protocol_data.py`
- **Bluetooth**: `m25_bluetooth.py`, `m25_spp.py`
- **Encryption**: `m25_crypto.py`
- **Utilities**: `m25_utils.py`
- **Examples**: `m25_parking.py` (remote control demo)

### New Files to Create
```
esp32/
├── main.py                    # MicroPython main entry point
├── joystick.py               # Joystick reading and calibration
├── m25_protocol_micro.py     # MicroPython port of protocol
├── bluetooth_spp_esp32.py    # ESP32 Bluetooth SPP wrapper
├── safety.py                 # Safety checks and watchdog
├── ui.py                     # LED control and display (if added)
├── config.py                 # Configuration storage
├── capacitive_deadman.py     # Capacitive touch deadman switch
└── README.md                 # ESP32-specific setup instructions

demos/
├── demo_esp32_basic.py       # Test joystick on ESP32
├── demo_esp32_control.py     # Full remote control demo
└── demo_capacitive_touch.py  # Capacitive deadman testing
```

---

## Reference Documentation Links
- [M25 Protocol Specification](m25-protocol.md)
- [Joystick Integration](joystick-integration.md)
- [Remote Control Design](remote-control-design.md)
- [ESP32 Setup Guide](esp32-setup.md)
- [Usage & Setup](usage-setup.md)

---

## Summary Checklist

### Must-Have (MVP)
- [x] ESP32 with WiFi + Bluetooth
- [x] 2-axis spring-return joystick
- [x] Emergency stop button (red, prominent)
- [x] Power button and battery management
- [x] Assist level selector (3 positions)
- [x] Hill hold toggle
- [x] Connection status LED
- [x] Battery level indicator (3-5 segments)
- [x] Assist level LEDs (3 individual)
- [x] Emergency/error LED
- [x] Deadman switch (button OR capacitive)
- [x] Watchdog timer (3-5 second timeout)
- [x] Connection loss handling
- [x] Low battery protection
- [x] Power-on safety checks
- [x] MicroPython firmware
- [x] M25 protocol implementation
- [x] 20Hz command update rate

### Nice-to-Have (Post-MVP)
- [ ] OLED display (0.96" or 1.3")
- [ ] Drive profile selector
- [ ] Cruise control toggle
- [ ] Speed limiter button
- [ ] Haptic feedback (vibration)
- [ ] Audio alerts (buzzer)
- [ ] Gesture detection
- [ ] WiFi remote monitoring
- [ ] OTA firmware updates

### Design Questions for Client
- [ ] Handheld vs. stick-mounted?
- [ ] Deadman: button vs. capacitive touch?
- [ ] Display: OLED screen vs. LEDs only?
- [ ] Size/weight preferences?
- [ ] Control layout feedback?

---

**Document Version**: 1.0  
**Date**: 2026-01-14  
**Status**: Draft for client review and iteration
