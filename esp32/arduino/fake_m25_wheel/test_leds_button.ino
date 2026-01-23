/*
 * LED and Button Test
 * Simple test sketch to verify hardware connections
 * 
 * Hardware:
 * - D25 (orange) - Red LED with 220-330Ω resistor
 * - D26 (yellow) - Yellow LED with 220-330Ω resistor
 * - D27 (turquoise) - Green LED with 220-330Ω resistor
 * - D33 (blue) - Button (other side to GND)
 * - 3.3V (red) and GND (brown) for power
 */

// Pin definitions
#include <HardwareSerial.h>
#define LED_RED 25      // Orange wire
#define LED_YELLOW 26   // Yellow wire
#define LED_GREEN 27    // Turquoise wire
#define BUTTON_PIN 33   // Blue wire
#define BUTTON_DEBOUNCE 50

bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
int currentLED = 0;  // 0=red, 1=yellow, 2=green, 3=all

void allOff();

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=================================");
    Serial.println("LED and Button Test");
    Serial.println("=================================");
    
    // Initialize pins
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    // Test all LEDs on startup
    Serial.println("\nTesting all LEDs...");
    Serial.println("Red ON");
    digitalWrite(LED_RED, HIGH);
    delay(500);
    Serial.println("Yellow ON");
    digitalWrite(LED_YELLOW, HIGH);
    delay(500);
    Serial.println("Green ON");
    digitalWrite(LED_GREEN, HIGH);
    delay(500);
    
    Serial.println("All OFF");
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    delay(500);
    
    Serial.println("\nTest complete!");
    Serial.println("\nPress button to cycle through LEDs:");
    Serial.println("  1st press: Red");
    Serial.println("  2nd press: Yellow");
    Serial.println("  3rd press: Green");
    Serial.println("  4th press: All LEDs");
    Serial.println("  5th press: All OFF (repeat)\n");
    
    // Start with all off
    allOff();
}

void loop() {
    handleButton();
    delay(10);
}

void handleButton() {
    int reading = digitalRead(BUTTON_PIN);
    
    // Debounce check - wait for stable state
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
        lastButtonState = reading;
    }
    
    // If button is pressed (LOW) and debounce time has passed
    if (reading == LOW && (millis() - lastDebounceTime) > BUTTON_DEBOUNCE) {
        Serial.println("Button pressed!");
        
        // Cycle through LED states
        currentLED++;
        if (currentLED > 4) currentLED = 0;
        
        switch(currentLED) {
            case 0:
                Serial.println("-> All OFF");
                allOff();
                break;
            case 1:
                Serial.println("-> Red LED ON");
                allOff();
                digitalWrite(LED_RED, HIGH);
                break;
            case 2:
                Serial.println("-> Yellow LED ON");
                allOff();
                digitalWrite(LED_YELLOW, HIGH);
                break;
            case 3:
                Serial.println("-> Green LED ON");
                allOff();
                digitalWrite(LED_GREEN, HIGH);
                break;
            case 4:
                Serial.println("-> All LEDs ON");
                digitalWrite(LED_RED, HIGH);
                digitalWrite(LED_YELLOW, HIGH);
                digitalWrite(LED_GREEN, HIGH);
                break;
        }
        
        // Wait for button release
        while (digitalRead(BUTTON_PIN) == LOW) {
            delay(10);
        }
        Serial.println("Button released\n");
        
        // Reset debounce timer
        lastDebounceTime = millis();
    }
}

void allOff() {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
}
