# Hardware Setup

## Pin Connections

| Component | ESP32 Pin | Notes |
|-----------|-----------|-------|
| OLED VCC  | 3.3V      | |
| OLED GND  | GND       | |
| OLED SDA  | GPIO 21   | I2C Data |
| OLED SCL  | GPIO 22   | I2C Clock |
| Buzzer +  | GPIO 25   | Active buzzer |
| Buzzer -  | GND       | |

## Libraries

Install via Arduino IDE → Tools → Manage Libraries:
- Adafruit GFX Library
- Adafruit SSD1306

## Troubleshooting

- **OLED blank**: Try I2C address 0x3D in `test_base.h` (default is 0x3C)
- **No I2C devices found**: Check power (3.3V) and data connections (GPIO 21/22)
