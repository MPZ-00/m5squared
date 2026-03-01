/*
 * test_example.ino - Simple OLED Display Example
 * 
 * Hardware Setup:
 * - OLED: SDA=GPIO 21, SCL=GPIO 22, VCC=3.3V, GND=GND
 * - Buzzer: Signal=GPIO 25, VCC=3.3V, GND=GND
 * 
 * This program displays a welcome message and beeps.
 */

#include "../test_base.h"

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== OLED Display Test ===");
    
    // Initialize test framework (includes OLED, buzzer, LEDs)
    testBegin("OLED Display Demo");
    
    // Clear and display welcome message
    g_display.clearDisplay();
    g_display.setTextSize(1);
    g_display.setTextColor(SSD1306_WHITE);
    
    // Line 1
    g_display.setCursor(0, 0);
    g_display.println("M5 Squared");
    
    // Line 2
    g_display.setCursor(0, 10);
    g_display.println("ESP32 Project");
    
    // Line 3
    g_display.setCursor(0, 20);
    g_display.println("OLED Test OK!");
    
    g_display.display();
    
    // Beep to confirm initialization
    buzzTestStart();
    
    Serial.println("Display initialized!");
    Serial.println("Message shown on OLED");
}

void loop() {
    // Animate display every 2 seconds
    static unsigned long lastUpdate = 0;
    static int counter = 0;
    
    if (millis() - lastUpdate > 2000) {
        lastUpdate = millis();
        counter++;
        
        // Update bottom line with counter
        g_display.fillRect(0, 20, OLED_WIDTH, 12, SSD1306_BLACK);
        g_display.setCursor(0, 20);
        g_display.printf("Count: %d", counter);
        g_display.display();
        
        // Short beep every 5 counts
        if (counter % 5 == 0) {
            buzzTestStart();
        }
        
        Serial.printf("Counter: %d\n", counter);
    }
    
    delay(10);
}
