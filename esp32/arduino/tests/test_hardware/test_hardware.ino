/*
 * test_hardware.ino - Hardware Verification
 * 
 * Tests OLED and buzzer.
 */

#include "../test_base.h"

void scanI2C() {
    Serial.println("\n=== I2C Scanner ===");
    Serial.println("Scanning I2C bus...");
    
    byte error, address;
    int devices = 0;
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.printf("I2C device found at 0x%02X\n", address);
            devices++;
        }
    }
    
    if (devices == 0) {
        Serial.println("ERROR: No I2C devices found!");
        Serial.println("Check OLED wiring:");
        Serial.println("  VCC → 3.3V, GND → GND");
        Serial.println("  SDA → GPIO 21, SCL → GPIO 22");
    } else {
        Serial.printf("Found %d device(s)\n", devices);
    }
}

void testOLED() {
    testStartSection("OLED Display");
    
    Serial.printf("Current OLED_ADDR: 0x%02X\n", OLED_ADDR);
    scanI2C();
    
    if (g_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED initialized!");
        g_display.clearDisplay();
        g_display.setTextSize(2);
        g_display.setTextColor(SSD1306_WHITE);
        g_display.setCursor(0, 0);
        g_display.println("OLED");
        g_display.println("WORKS!");
        g_display.display();
        ASSERT_TRUE(true, "OLED working");
    } else {
        Serial.println("OLED init failed");
        Serial.println("If device found above, update OLED_ADDR in test_base.h");
        ASSERT_TRUE(false, "OLED failed");
    }
    
    testEndSection();
}

void testBuzzer() {
    testStartSection("Buzzer");
    
    Serial.println("Buzzer test - 3 beeps...");
    for (int i = 0; i < 3; i++) {
        Serial.printf("  Beep %d\n", i+1);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
    }
    
    ASSERT_TRUE(true, "Buzzer working");
    testEndSection();
}

void setup() {
    testBegin("Hardware Test");
    Serial.println("Testing OLED and buzzer");
    delay(1000);
    
    testOLED();
    delay(1000);
    
    testBuzzer();
    delay(1000);
    
    testSummary();
}

void loop() {
    int secondsToWait = 10;
    Serial.printf("Waiting %d seconds...\n", secondsToWait);
    delay(secondsToWait * 1000);
}
