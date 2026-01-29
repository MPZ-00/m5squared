"""
M25 MicroPython Main Entry Point
Basic control loop for ESP32
"""
import time
from machine import ADC, Pin
from m25_crypto import M25Crypto
from m25_ble import M25BLE
import config

# Initialize hardware
joy_x = ADC(Pin(config.JOY_X_PIN))
joy_y = ADC(Pin(config.JOY_Y_PIN))
button = Pin(config.BUTTON_PIN, Pin.IN, Pin.PULL_UP)

joy_x.atten(ADC.ATTN_11DB)  # Full 0-3.3V range
joy_y.atten(ADC.ATTN_11DB)

# Initialize crypto and BLE
crypto = M25Crypto(config.KEY)
ble = M25BLE(crypto)

print("M25 MicroPython Controller")
print("Connecting to wheel:", config.LEFT_WHEEL_MAC)

# Connect to wheel
ble.connect(config.LEFT_WHEEL_MAC)

# Main control loop
while True:
    if button.value() == 0:  # Button pressed (active low)
        # Read joystick (0-4095)
        x = joy_x.read()
        y = joy_y.read()
        
        # Normalize to -1.0 to 1.0
        x_norm = (x - 2048) / 2048.0
        y_norm = (y - 2048) / 2048.0
        
        # Build packet (simplified - adapt to actual M25 protocol)
        packet = bytearray(16)
        packet[0] = 0x01  # Command ID
        # Add speed values to packet
        
        ble.send_packet(bytes(packet))
    else:
        # Button released - send stop
        packet = bytearray(16)
        packet[0] = 0x01
        ble.send_packet(bytes(packet))
    
    time.sleep_ms(50)  # 20Hz update rate
