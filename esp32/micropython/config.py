"""
Configuration for M25 MicroPython
"""

# Encryption key (16 bytes) - set your key here
KEY = b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'

# Wheel MAC addresses
LEFT_WHEEL_MAC = "AA:BB:CC:DD:EE:FF"
RIGHT_WHEEL_MAC = "11:22:33:44:55:66"

# WiFi AP config (if using web interface)
WIFI_SSID = "M25-Controller"
WIFI_PASSWORD = "m25wheel"

# Hardware pins
JOY_X_PIN = 34
JOY_Y_PIN = 35
BUTTON_PIN = 25
