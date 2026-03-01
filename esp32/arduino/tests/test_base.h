/*
 * test_base.h - Modular Arduino Test Framework
 * 
 * Hardware Setup:
 * - OLED Display: 0.91" IIC 128x32 (SSD1306)
 *   - SDA: GPIO 21 (default I2C)
 *   - SCL: GPIO 22 (default I2C)
 *   - VCC: 3.3V
 *   - GND: GND
 * 
 * - Active Buzzer:
 *   - Signal: GPIO 25
 *   - VCC: 3.3V
 *   - GND: GND
 * 
 * - LEDs (with 330Ω resistors):
 *   - LED1 (Status): GPIO 26
 *   - LED2 (Error): GPIO 27
 *   - Each LED: Anode -> GPIO, Cathode -> 330Ω -> GND
 * 
 * Usage:
 *   #include "test_base.h"
 *   
 *   void setup() {
 *     testBegin("My Test Suite");
 *     // Your setup code
 *   }
 *   
 *   void loop() {
 *     testStartSection("Test Section 1");
 *     ASSERT_EQ(1, 1, "Basic equality");
 *     testEndSection();
 *     
 *     testSummary();
 *     while(1) delay(1000);
 *   }
 */

#ifndef TEST_BASE_H
#define TEST_BASE_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// Hardware Pin Configuration
// ============================================================================
#define BUZZER_PIN      25
#define LED_STATUS_PIN  26
#define LED_ERROR_PIN   27

#define OLED_WIDTH      128
#define OLED_HEIGHT     32
#define OLED_RESET      -1  // Reset pin (-1 = share Arduino reset pin)
#define OLED_ADDR       0x3C

// ============================================================================
// Test Framework Globals
// ============================================================================
static int g_testsPassed = 0;
static int g_testsFailed = 0;
static char g_suiteName[64] = "Test Suite";
static Adafruit_SSD1306 g_display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// Hardware Control Functions
// ============================================================================

/**
 * Initialize hardware components (OLED, buzzer, LEDs)
 */
void testHardwareInit() {
    // Initialize I2C pins
    Wire.begin(21, 22);  // SDA=21, SCL=22 (ESP32 defaults)
    
    // Initialize OLED
    if (!g_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("ERROR: SSD1306 allocation failed"));
        // Continue anyway - Serial will still work
    } else {
        g_display.clearDisplay();
        g_display.setTextSize(1);
        g_display.setTextColor(SSD1306_WHITE);
        g_display.setCursor(0, 0);
        g_display.println("Test Framework");
        g_display.println("Initializing...");
        g_display.display();
    }
    
    // Initialize buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // Initialize LEDs
    pinMode(LED_STATUS_PIN, OUTPUT);
    pinMode(LED_ERROR_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
    digitalWrite(LED_ERROR_PIN, LOW);
}

/**
 * Buzzer patterns for different events
 */
void buzzStartup() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(50);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
}

void buzzTestStart() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);
}

void buzzTestEnd() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
}

void buzzSuccess() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(50);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
    }
}

void buzzFailure() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
}

/**
 * LED control functions
 */
void ledStatusOn() { digitalWrite(LED_STATUS_PIN, HIGH); }
void ledStatusOff() { digitalWrite(LED_STATUS_PIN, LOW); }
void ledStatusBlink(int times = 1) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(100);
        digitalWrite(LED_STATUS_PIN, LOW);
        delay(100);
    }
}

void ledErrorOn() { digitalWrite(LED_ERROR_PIN, HIGH); }
void ledErrorOff() { digitalWrite(LED_ERROR_PIN, LOW); }
void ledErrorBlink(int times = 1) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_ERROR_PIN, HIGH);
        delay(100);
        digitalWrite(LED_ERROR_PIN, LOW);
        delay(100);
    }
}

/**
 * Display update functions
 */
void displayUpdate(const char* line1, const char* line2 = nullptr, 
                   const char* line3 = nullptr, const char* line4 = nullptr) {
    g_display.clearDisplay();
    g_display.setTextSize(1);
    g_display.setTextColor(SSD1306_WHITE);
    g_display.setCursor(0, 0);
    
    if (line1) g_display.println(line1);
    if (line2) g_display.println(line2);
    if (line3) g_display.println(line3);
    if (line4) g_display.println(line4);
    
    g_display.display();
}

void displayTestStatus() {
    char buf1[32], buf2[32], buf3[32];
    snprintf(buf1, sizeof(buf1), "Suite: %s", g_suiteName);
    snprintf(buf2, sizeof(buf2), "Pass: %d", g_testsPassed);
    snprintf(buf3, sizeof(buf3), "Fail: %d", g_testsFailed);
    displayUpdate(buf1, buf2, buf3);
}

// ============================================================================
// Test Framework Functions
// ============================================================================

/**
 * Initialize test framework and hardware
 */
void testBegin(const char* suiteName = "Test Suite") {
    Serial.begin(115200);
    delay(100);
    
    // Copy suite name
    strncpy(g_suiteName, suiteName, sizeof(g_suiteName) - 1);
    g_suiteName[sizeof(g_suiteName) - 1] = '\0';
    
    // Initialize hardware
    testHardwareInit();
    
    // Signal startup
    buzzStartup();
    ledStatusBlink(2);
    
    // Display and print header
    Serial.println("\n========================================");
    Serial.printf("  %s\n", g_suiteName);
    Serial.println("========================================\n");
    
    displayUpdate(g_suiteName, "Ready", "");
    delay(1000);
}

/**
 * Start a test section
 */
void testStartSection(const char* sectionName) {
    Serial.printf("\n--- %s ---\n", sectionName);
    buzzTestStart();
    ledStatusOn();
    displayUpdate(g_suiteName, sectionName, "Running...");
}

/**
 * End a test section
 */
void testEndSection() {
    buzzTestEnd();
    ledStatusOff();
    displayTestStatus();
}

/**
 * Print test summary
 */
void testSummary() {
    Serial.println("\n========================================");
    Serial.println("          Test Summary");
    Serial.println("========================================");
    Serial.printf("Tests Passed: %d\n", g_testsPassed);
    Serial.printf("Tests Failed: %d\n", g_testsFailed);
    
    int total = g_testsPassed + g_testsFailed;
    if (total > 0) {
        float successRate = (float)g_testsPassed / total * 100.0f;
        Serial.printf("Success Rate: %.1f%%\n", successRate);
    }
    
    if (g_testsFailed == 0 && g_testsPassed > 0) {
        Serial.println("\nALL TESTS PASSED!");
        buzzSuccess();
        ledStatusBlink(5);
        displayUpdate("TEST COMPLETE", "ALL PASSED!", "", "");
    } else if (g_testsFailed > 0) {
        Serial.println("\nSOME TESTS FAILED!");
        buzzFailure();
        ledErrorOn();
        displayUpdate("TEST COMPLETE", "FAILURES!", "", "");
        delay(2000);
        ledErrorOff();
    }
    
    // Final status display
    delay(1000);
    displayTestStatus();
}

// ============================================================================
// Test Assertion Macros
// ============================================================================

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) == (actual)) { \
            Serial.printf("  [PASS] %s: expected=%d, actual=%d\n", \
                          msg, (int)(expected), (int)(actual)); \
            g_testsPassed++; \
            ledStatusBlink(1); \
        } else { \
            Serial.printf("  [FAIL] %s: expected=%d, actual=%d\n", \
                          msg, (int)(expected), (int)(actual)); \
            g_testsFailed++; \
            ledErrorBlink(1); \
        } \
    } while(0)

#define ASSERT_NEAR(expected, actual, tolerance, msg) \
    do { \
        float diff = fabsf((expected) - (actual)); \
        if (diff <= (tolerance)) { \
            Serial.printf("  [PASS] %s: expected=%.3f, actual=%.3f\n", \
                          msg, (expected), (actual)); \
            g_testsPassed++; \
            ledStatusBlink(1); \
        } else { \
            Serial.printf("  [FAIL] %s: expected=%.3f, actual=%.3f (diff=%.3f)\n", \
                          msg, (expected), (actual), diff); \
            g_testsFailed++; \
            ledErrorBlink(1); \
        } \
    } while(0)

#define ASSERT_TRUE(condition, msg) \
    do { \
        if (condition) { \
            Serial.printf("  [PASS] %s\n", msg); \
            g_testsPassed++; \
            ledStatusBlink(1); \
        } else { \
            Serial.printf("  [FAIL] %s\n", msg); \
            g_testsFailed++; \
            ledErrorBlink(1); \
        } \
    } while(0)

#define ASSERT_FALSE(condition, msg) \
    ASSERT_TRUE(!(condition), msg)

#define ASSERT_NE(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            Serial.printf("  [PASS] %s: values differ as expected\n", msg); \
            g_testsPassed++; \
            ledStatusBlink(1); \
        } else { \
            Serial.printf("  [FAIL] %s: expected values to differ\n", msg); \
            g_testsFailed++; \
            ledErrorBlink(1); \
        } \
    } while(0)

#endif // TEST_BASE_H
