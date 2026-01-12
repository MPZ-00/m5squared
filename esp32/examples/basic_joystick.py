"""
ESP32 Basic Joystick Example

Simple example using analog joystick to control M25 wheels.
Hardware: ESP32 + 2-axis analog joystick module

Wiring:
- Joystick VRX -> GPIO 34 (ADC)
- Joystick VRY -> GPIO 35 (ADC)  
- Joystick SW  -> GPIO 25 (Button, active low)
- Joystick VCC -> 3.3V
- Joystick GND -> GND
"""

import uasyncio as asyncio
from machine import Pin, ADC
import time

# Configuration
JOYSTICK_X = 34
JOYSTICK_Y = 35
BUTTON = 25

class SimpleJoystick:
    def __init__(self):
        # Setup ADC for joystick
        self.x_adc = ADC(Pin(JOYSTICK_X))
        self.y_adc = ADC(Pin(JOYSTICK_Y))
        
        # Set attenuation for 0-3.3V range
        self.x_adc.atten(ADC.ATTN_11DB)
        self.y_adc.atten(ADC.ATTN_11DB)
        
        # Deadman button (active low with pull-up)
        self.button = Pin(BUTTON, Pin.IN, Pin.PULL_UP)
        
        # Calibration values
        self.center_x = 2048
        self.center_y = 2048
        self.deadzone = 200
        
        print("Joystick initialized")
        print("Calibrating... Keep joystick centered")
        time.sleep(2)
        
        # Auto-calibrate center
        self.calibrate()
    
    def calibrate(self):
        """Calibrate joystick center position"""
        samples = 10
        x_sum = 0
        y_sum = 0
        
        for _ in range(samples):
            x_sum += self.x_adc.read()
            y_sum += self.y_adc.read()
            time.sleep(0.05)
        
        self.center_x = x_sum // samples
        self.center_y = y_sum // samples
        
        print(f"Calibrated: X={self.center_x}, Y={self.center_y}")
    
    def read(self):
        """Read joystick position and button state"""
        # Read raw values (0-4095)
        x_raw = self.x_adc.read()
        y_raw = self.y_adc.read()
        
        # Apply deadzone
        x_offset = x_raw - self.center_x
        y_offset = y_raw - self.center_y
        
        if abs(x_offset) < self.deadzone:
            x_offset = 0
        if abs(y_offset) < self.deadzone:
            y_offset = 0
        
        # Convert to -1.0 to 1.0 range
        x = x_offset / 2048.0
        y = y_offset / 2048.0
        
        # Clamp to range
        x = max(-1.0, min(1.0, x))
        y = max(-1.0, min(1.0, y))
        
        # Read button (active low, so invert)
        deadman = not self.button.value()
        
        return x, y, deadman

async def main():
    """Simple test of joystick input"""
    joystick = SimpleJoystick()
    
    print("\nJoystick Test")
    print("Move joystick and press button")
    print("Press Ctrl+C to exit\n")
    
    last_values = None
    
    while True:
        x, y, deadman = joystick.read()
        
        # Only print when values change
        current = (round(x, 2), round(y, 2), deadman)
        if current != last_values:
            status = "PRESSED" if deadman else "RELEASED"
            print(f"X: {x:+.2f}  Y: {y:+.2f}  Button: {status}")
            last_values = current
        
        await asyncio.sleep_ms(50)  # 20Hz update rate

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped by user")
