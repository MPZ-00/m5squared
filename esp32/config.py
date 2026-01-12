# ESP32 Configuration

# Wi-Fi Settings (optional - for remote control)
WIFI_SSID = "YourWiFiNetwork"
WIFI_PASSWORD = "YourPassword"
WIFI_ENABLED = False  # Set to True to enable Wi-Fi

# M25 Wheel MAC Addresses
LEFT_WHEEL_MAC = "AA:BB:CC:DD:EE:FF"
RIGHT_WHEEL_MAC = "AA:BB:CC:DD:EE:FF"

# Encryption Keys (16 bytes each, hex string)
# Get these from your .env file or QR codes
LEFT_WHEEL_KEY = "0123456789ABCDEF0123456789ABCDEF"
RIGHT_WHEEL_KEY = "0123456789ABCDEF0123456789ABCDEF"

# Input Configuration
INPUT_TYPE = "joystick"  # Options: "joystick", "gamepad_bt", "wifi"

# Joystick GPIO Pins (for analog joystick)
JOYSTICK_X_PIN = 34  # ADC1 Channel 6
JOYSTICK_Y_PIN = 35  # ADC1 Channel 7
JOYSTICK_BTN_PIN = 25  # Deadman button

# Joystick Calibration
JOYSTICK_CENTER_X = 2048
JOYSTICK_CENTER_Y = 2048
JOYSTICK_DEADZONE = 200  # +/- range around center

# Control Settings
UPDATE_RATE_HZ = 20  # Control loop frequency (20Hz = 50ms)
MAX_SPEED = 100  # Maximum speed value (0-100)

# Safety Settings
REQUIRE_DEADMAN = True  # Must hold button to move
TIMEOUT_MS = 500  # Stop if no command received for 500ms

# Debug Settings
DEBUG = True  # Enable debug output
DEBUG_VERBOSE = False  # Extra verbose output

# Display Settings (if available)
DISPLAY_ENABLED = False  # Set to True if using board with display
DISPLAY_ROTATION = 0  # 0, 90, 180, or 270 degrees

# LED Status Indicator
STATUS_LED_PIN = 2  # Built-in LED on most ESP32 boards
LED_CONNECTED = "solid"  # Solid on when connected
LED_DISCONNECTED = "slow_blink"  # Slow blink when disconnected
LED_ERROR = "fast_blink"  # Fast blink on error
