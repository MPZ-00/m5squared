# Remote Control Design Specification
## Overview
Design specification for a standalone, one-handed remote control for the Alber e-motion M25 power-assist wheels.  
This document outlines the essential switches, indicators, and ergonomic considerations for a practical, safe, and accessible remote control device.

## Design Philosophy
- **One-handed operation**: All controls accessible with a single hand
- **Safety first**: Emergency stop and critical indicators prominently placed
- **Intuitive layout**: Most-used controls in prime positions
- **Clear feedback**: Visual and optional tactile/audible confirmation
- **Standalone device**: No smartphone required

## Essential Switches
### Primary Controls
#### 1. Assist Level Selector
- **Type**: 3-position rocker switch or rotary selector
- **Positions**:
  - Level 1: Normal/Indoor
  - Level 2: Outdoor
  - Level 3: Learning/Sport
- **Placement**: Under thumb for frequent adjustment
- **Protocol**: `WRITE_ASSIST_LEVEL` (Service 0x02, Parameter 0x20)

#### 2. Hill Hold Toggle
- **Type**: Momentary or latching toggle switch
- **Function**: Enable/disable auto-hold on slopes
- **Placement**: Middle finger area
- **Protocol**: `WRITE_DRIVE_MODE` bit flag (0x01)

#### 3. Emergency Stop
- **Type**: Large, prominent momentary button
- **Color**: Red
- **Function**: Immediately disable remote mode and stop all movement
- **Placement**: Index finger - immediate access
- **Protocol**: `WRITE_DRIVE_MODE = 0x00` (normal mode) + `WRITE_REMOTE_SPEED = 0`

### Secondary Controls
#### 4. Drive Profile Selector
- **Type**: Rotary switch or multi-position selector
- **Options**:
  - Standard
  - Sensitive
  - Soft
  - Active
  - Sensitive Plus
  - Customized
- **Placement**: Side or top edge (less frequent use)
- **Protocol**: `WRITE_DRIVE_PROFILE` (Service 0x02, Parameter 0x21)

#### 5. Cruise Control Toggle
- **Type**: Momentary or latching switch
- **Function**: Enable/disable cruise mode
- **Placement**: Secondary position
- **Protocol**: `WRITE_DRIVE_MODE` bit flag (0x02)
- **Note**: Feature requires testing

#### 6. Power/Wake Button
- **Type**: Momentary push button
- **Function**: Connect/disconnect from wheels
- **Placement**: Top edge
- **Protocol**: `WRITE_SYSTEM_MODE = 0x01` (connect) or disconnect sequence

## Essential Indicators
### Status LEDs
#### 1. Battery Level Indicator
- **Type**: Multi-segment LED bar or RGB LED
- **Display**: 4-5 segments or color-coded
  - Green: 70-100%
  - Yellow: 30-69%
  - Red: 0-29%
  - Blinking red: Critical (<10%)
- **Data source**: `READ_SOC` (Service 0x04, Parameter 0x11)
- **Update frequency**: Every 5-10 seconds

#### 2. Connection Status
- **Type**: Single LED (green/blue)
- **States**:
  - Solid: Connected to both wheels
  - Slow blink: Connecting or single wheel connected
  - Fast blink: Connection error
  - Off: Disconnected
- **Placement**: Prominent position near top

#### 3. Assist Level Indicators
- **Type**: 3 individual LEDs
- **Labels**: 1, 2, 3 or Indoor/Outdoor/Sport
- **State**: One LED lit per active level
- **Placement**: Near assist level switch

#### 4. Hill Hold Status
- **Type**: Single LED (amber/orange)
- **State**: On when hill hold is active
- **Placement**: Near hill hold switch

#### 5. Cruise Control Status
- **Type**: Single LED (blue)
- **State**: On when cruise is engaged
- **Placement**: Near cruise switch
- **Note**: Optional if cruise is implemented

### Optional Display
#### 6. Small OLED/LCD Screen (128x64 or similar)
- **Content**:
  - Battery percentage (numerical, e.g., "85%")
  - Current speed (if in remote control mode)
  - Distance traveled (cruise values)
  - Active profile name
  - Firmware version
  - Error messages/status
- **Advantages**: 
  - More detailed information
  - Contextual help text
  - Configuration menus
- **Power**: Consider auto-dimming/sleep

## Ergonomic Layout
### Recommended Physical Layout
```
┌─────────────────────────────┐
│     [POWER]    ◉ Status     │  Top edge
├─────────────────────────────┤
│                             │
│    ┌─────────────┐          │
│    │   OLED/LCD  │   [▲▼▲]  │  Profile selector
│    │   Display   │          │
│    └─────────────┘          │
│                             │
│  ◉◉◉  Assist Level          │  Level indicators
│  123                        │
│                             │
│  [====] Assist Rocker       │  Thumb control
│                             │
│  [STOP] Emergency           │  Index finger
│                             │
│  [□] Hill Hold   ◉          │  Toggle + indicator
│  [□] Cruise      ◉          │  Toggle + indicator
│                             │
│  ▓▓▓▓░ Battery              │  LED bar
│                             │
└─────────────────────────────┘
```

### Priority-Based Access
1. **Thumb**: Assist level rocker (most frequent)
2. **Index finger**: Emergency stop (safety critical)
3. **Middle finger area**: Hill hold toggle
4. **Top edge**: Power button
5. **Side/rear**: Profile selector (less frequent)
6. **Face**: All indicators clearly visible

## Safety Features
### Critical Safety Systems
#### 1. Deadman Switch (Optional)
- **Type**: Grip sensor or continuous pressure button
- **Function**: Requires active holding to maintain control
- **Behavior**: Release triggers stop and mode reset
- **Protocol**: Stop sending speed commands, send `WRITE_DRIVE_MODE = 0x00`

#### 2. Timeout Protection
- **Function**: Auto-disconnect after inactivity
- **Duration**: 30-60 seconds without commands
- **Warning**: LED blinks 5 seconds before timeout
- **Recovery**: Press any button to extend

#### 3. Low Battery Warning
- **Remote battery**: Visual + optional vibration at 20%
- **Wheel battery**: LED indicator from SOC data
- **Behavior**: Limit features at critical levels

#### 4. Mode Indication
- **Visual**: Clear distinction between modes
  - Normal mode: Green LED
  - Remote control active: Blue LED (pulsing)
  - Error state: Red LED (blinking)

#### 5. Connection Loss Handling
- **Detection**: No response to 3 consecutive packets
- **Action**: 
  - Visual/audible alert
  - Disable remote control mode
  - Attempt reconnection
- **User action required**: Press power button to reconnect

## Accessibility Features
### Universal Design Elements
#### 1. Tactile Markers
- **Emergency stop**: Raised texture or distinctive shape
- **Power button**: Distinctive feel
- **Assist level positions**: Tactile detents

#### 2. Audible Feedback (Optional)
- **Beeper patterns**:
  - Single beep: Level change
  - Double beep: Mode toggle
  - Triple beep: Connection/disconnection
  - Continuous: Emergency/error

#### 3. Visual Design
- **High contrast**: Black housing with bright indicator colors
- **Large indicators**: Minimum 5mm LED size
- **Clear labels**: Raised or high-contrast text
- **Night visibility**: Backlit controls or glow markers

#### 4. Grip Design
- **Ergonomic shape**: Natural hand position
- **Non-slip surface**: Textured grip areas
- **Weight distribution**: Balanced for one-handed use
- **Size options**: Accommodate different hand sizes

## Protocol Implementation
### Communication Requirements
#### Bluetooth Connection
- **Standard**: Bluetooth Classic SPP (Serial Port Profile)
- **MAC addresses**: Left and right wheel addresses
- **Encryption**: AES-128-CBC per-device keys
- **Pairing**: QR code or manual key entry

#### Command Timing
- **Regular commands**: 100ms interval for remote control
- **Status polling**: Every 2-5 seconds
- **Response timeout**: 500ms-1s per command
- **Keepalive**: Send status query every 10s

#### Error Handling
- **Retry logic**: Up to 3 attempts per command
- **Fallback**: Return to safe state on communication failure
- **Logging**: Store last 100 events for diagnostics

### Key Protocol Commands
```python
# Connection
WRITE_SYSTEM_MODE = 0x01  # Connect to wheels

# Assist Level (0-2)
WRITE_ASSIST_LEVEL = 0x20
READ_ASSIST_LEVEL = 0x10

# Drive Mode (bit flags)
WRITE_DRIVE_MODE = 0x24
  # Bit 0: Hill hold (auto-hold)
  # Bit 1: Cruise control
  # Bit 2: Remote control mode

# Drive Profile (0-5)
WRITE_DRIVE_PROFILE = 0x21
READ_DRIVE_PROFILE = 0x12

# Battery Status
READ_SOC = 0x11  # State of charge

# Remote Control Speed
WRITE_REMOTE_SPEED = 0x30  # Signed 16-bit speed value
```

## Power Management
### Remote Control Battery
#### Requirements
- **Type**: Rechargeable Li-ion or Li-Po
- **Capacity**: 500-1000mAh (days of use)
- **Charging**: USB-C port
- **Indicator**: LED shows charging status
- **Runtime**: 8-24 hours active use

#### Power States
- **Active**: Full power, all features enabled
- **Idle**: Display dimmed, maintain connection
- **Sleep**: Disconnect, low-power monitoring for button press
- **Off**: Complete power down

#### Power Saving
- **Display timeout**: 30 seconds of inactivity
- **Connection management**: Disconnect after 5 minutes idle
- **Wake-on-button**: Any button press restores

## Manufacturing Considerations
### Components
#### Electronics
- **Microcontroller**: ESP32 or similar (Bluetooth + processing)
- **Display**: 128x64 OLED (if included)
- **Switches**: High-quality tactile or mechanical
- **LEDs**: High-brightness, wide viewing angle
- **Battery**: Protected Li-ion cell with charging circuit

#### Housing
- **Material**: ABS or polycarbonate
- **Protection**: IP54 rating minimum (splash/dust resistant)
- **Mounting**: Clip or strap for attachment to wheelchair
- **Dimensions**: Approximately 100x60x25mm

### Cost Estimation (DIY Build)
| Component | Approximate Cost |
|-----------|-----------------|
| ESP32 module | $5-10 |
| OLED display | $3-8 |
| Switches/buttons | $5-15 |
| LEDs | $2-5 |
| Battery + charging | $5-10 |
| Housing (3D printed) | $2-5 |
| PCB | $5-15 |
| Misc (wires, etc.) | $5-10 |
| **Total** | **$30-80** |

Compare to official remote: €595 ($650+)

## Testing Protocol
### Pre-Use Testing
1. **Connection test**: Verify pairing with both wheels
2. **Command response**: Test each switch function
3. **Indicator verification**: Confirm all LEDs functional
4. **Battery check**: Verify charge level and warnings
5. **Emergency stop**: Confirm immediate response
6. **Range test**: Test maximum operating distance

### Safety Testing
1. **Connection loss**: Verify safe behavior on disconnect
2. **Low battery**: Confirm warnings and graceful degradation
3. **Emergency stop**: Test from various operating modes
4. **Timeout**: Verify auto-disconnect functionality
5. **Error conditions**: Test handling of malformed responses

### Field Testing
1. **Real-world usage**: Multiple hours of normal operation
2. **Environmental**: Test in various weather conditions
3. **Battery life**: Measure actual runtime
4. **Ergonomics**: User comfort over extended use
5. **Accessibility**: Testing with users of varying abilities

## Future Enhancements
### Potential Additions
1. **Haptic feedback**: Vibration motor for alerts
2. **Voice control**: Microphone for voice commands
3. **Gesture control**: Accelerometer for tilt/shake gestures
4. **Data logging**: SD card for trip history
5. **Wireless charging**: Qi charging pad compatibility
6. **App connectivity**: Optional smartphone companion app
7. **Customizable profiles**: Save multiple drive configurations
8. **Speed limiter**: User-configurable maximum speed

### Advanced Features
1. **GPS tracking**: Location logging and anti-theft
2. **Obstacle detection**: Ultrasonic sensors (requires hardware mod)
3. **Auto-profile switching**: Based on location or time
4. **Social features**: Share routes, connect with other users
5. **Predictive battery**: Machine learning for range estimation

## References
### Related Documentation
- [M25 Protocol Specification](m25-protocol.md)
- [Bluetooth Cross-Platform](ble-cross-platform.md)
- [Windows Setup Guide](windows-setup.md)
- [ESP32 Setup](esp32-setup.md)

### Source Code
- `m25_ecs.py`: ECS Remote protocol implementation
- `m25_parking.py`: Remote control mode demonstration
- `m25_bluetooth.py`: Bluetooth communication layer
- `m25_gui.py`: Reference GUI implementation

### External References
- Alber e-motion M25 official documentation
- Bluetooth SPP specification
- AES-128-CBC encryption standard
- Wheelchair safety standards (ISO 7176)

## Revision History
| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-14 | Initial specification |

## License
This design specification is part of the m5squared project and is provided under the same license terms. Use at your own risk. We are not liable for any incidents resulting from the use of this design.

**Safety Notice**: This remote control can move a wheelchair. Always use responsibly, test thoroughly, and follow all safety protocols. When in doubt, include more safety features, not fewer.
