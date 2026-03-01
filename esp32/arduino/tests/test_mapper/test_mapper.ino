/*
 * test_mapper.ino - Unit tests for Mapper
 *
 * Tests all safety-critical features:
 * - Deadzone handling
 * - Response curves
 * - Differential drive kinematics
 * - Speed limiting per mode
 * - Acceleration ramping
 * - Safety interlocks
 *
 * Upload this sketch to ESP32 for testing.
 * Open Serial Monitor at 115200 baud to see results.
 */

#include "mapper.h"

// Test result tracking
static int testsPassed = 0;
static int testsFailed = 0;

// Helper: Assert with message
#define ASSERT_EQ(expected, actual, msg) \
    if ((expected) == (actual)) { \
        Serial.printf("  [PASS] %s: expected=%d, actual=%d\n", msg, (int)(expected), (int)(actual)); \
        testsPassed++; \
    } else { \
        Serial.printf("  [FAIL] %s: expected=%d, actual=%d\n", msg, (int)(expected), (int)(actual)); \
        testsFailed++; \
    }

#define ASSERT_NEAR(expected, actual, tolerance, msg) \
    if (fabsf((expected) - (actual)) <= (tolerance)) { \
        Serial.printf("  [PASS] %s: expected=%.3f, actual=%.3f\n", msg, (expected), (actual)); \
        testsPassed++; \
    } else { \
        Serial.printf("  [FAIL] %s: expected=%.3f, actual=%.3f (diff=%.3f)\n", \
                      msg, (expected), (actual), fabsf((expected) - (actual))); \
        testsFailed++; \
    }

#define ASSERT_TRUE(condition, msg) \
    if (condition) { \
        Serial.printf("  [PASS] %s\n", msg); \
        testsPassed++; \
    } else { \
        Serial.printf("  [FAIL] %s\n", msg); \
        testsFailed++; \
    }

// ---------------------------------------------------------------------------
// Test: Deadman Switch Enforcement
// ---------------------------------------------------------------------------
void testDeadmanSwitch() {
    Serial.println("\n=== Test: Deadman Switch ===");
    
    Mapper mapper;
    ControlState state;
    CommandFrame cmd;
    
    // Test 1: No deadman = stop
    state.vx = 0.5f;
    state.vy = 0.0f;
    state.deadman = false;
    state.mode = DRIVE_MODE_NORMAL;
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.isStop(), "No deadman should produce stop");
    
    // Test 2: Deadman pressed = movement allowed
    state.deadman = true;
    mapper.map(state, cmd);
    ASSERT_TRUE(!cmd.isStop(), "Deadman pressed should allow movement");
}

// ---------------------------------------------------------------------------
// Test: STOP Mode Enforcement
// ---------------------------------------------------------------------------
void testStopMode() {
    Serial.println("\n=== Test: STOP Mode ===");
    
    Mapper mapper;
    ControlState state;
    CommandFrame cmd;
    
    state.vx = 0.5f;
    state.vy = 0.0f;
    state.deadman = true;
    state.mode = DRIVE_MODE_STOP;
    
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.isStop(), "STOP mode should produce stop regardless of input");
}

// ---------------------------------------------------------------------------
// Test: Deadzone Handling
// ---------------------------------------------------------------------------
void testDeadzone() {
    Serial.println("\n=== Test: Deadzone ===");
    
    MapperConfig config;
    config.deadzone = 0.1f;
    config.curve = 1.0f;  // Linear for easier testing
    Mapper mapper(config);
    
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    
    // Test 1: Input below deadzone
    state.vx = 0.05f;
    state.vy = 0.0f;
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.isStop(), "Input below deadzone should produce stop");
    
    // Test 2: Input at deadzone boundary
    state.vx = 0.1f;
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.isStop(), "Input at deadzone boundary should produce stop");
    
    // Test 3: Input above deadzone
    state.vx = 0.15f;
    mapper.map(state, cmd);
    ASSERT_TRUE(!cmd.isStop(), "Input above deadzone should produce movement");
}

// ---------------------------------------------------------------------------
// Test: Response Curves
// ---------------------------------------------------------------------------
void testResponseCurves() {
    Serial.println("\n=== Test: Response Curves ===");
    
    MapperConfig config;
    config.deadzone = 0.0f;  // Disable deadzone for curve testing
    config.curve = 2.0f;     // Quadratic curve
    config.maxSpeedNormal = 100;
    Mapper mapper(config);
    
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    state.vy = 0.0f;
    
    // Test: Quadratic curve (x^2) makes small inputs even smaller
    state.vx = 0.5f;  // Half input
    mapper.map(state, cmd);
    // With quadratic curve: 0.5^2 = 0.25, scaled to 0-100 = 25
    // Both wheels should be around 25 (differential drive with no turn)
    ASSERT_NEAR(25, cmd.leftSpeed, 5, "Curve: 50% input should produce ~25% output");
}

// ---------------------------------------------------------------------------
// Test: Differential Drive Kinematics
// ---------------------------------------------------------------------------
void testDifferentialDrive() {
    Serial.println("\n=== Test: Differential Drive ===");
    
    MapperConfig config;
    config.deadzone = 0.0f;
    config.curve = 1.0f;  // Linear
    config.maxSpeedNormal = 100;
    Mapper mapper(config);
    
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    
    // Test 1: Pure forward
    state.vx = 1.0f;
    state.vy = 0.0f;
    mapper.map(state, cmd);
    ASSERT_EQ(100, cmd.leftSpeed, "Pure forward: left wheel");
    ASSERT_EQ(100, cmd.rightSpeed, "Pure forward: right wheel");
    
    // Test 2: Pure backward
    state.vx = -1.0f;
    state.vy = 0.0f;
    mapper.map(state, cmd);
    ASSERT_EQ(-100, cmd.leftSpeed, "Pure backward: left wheel");
    ASSERT_EQ(-100, cmd.rightSpeed, "Pure backward: right wheel");
    
    // Test 3: Pure right turn (in-place)
    state.vx = 0.0f;
    state.vy = 1.0f;
    mapper.map(state, cmd);
    ASSERT_EQ(-100, cmd.leftSpeed, "Pure right turn: left wheel");
    ASSERT_EQ(100, cmd.rightSpeed, "Pure right turn: right wheel");
    
    // Test 4: Pure left turn (in-place)
    state.vx = 0.0f;
    state.vy = -1.0f;
    mapper.map(state, cmd);
    ASSERT_EQ(100, cmd.leftSpeed, "Pure left turn: left wheel");
    ASSERT_EQ(-100, cmd.rightSpeed, "Pure left turn: right wheel");
    
    // Test 5: Forward + right (arc turn)
    state.vx = 1.0f;
    state.vy = 0.5f;
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.leftSpeed > cmd.rightSpeed, "Forward+right: left > right");
    ASSERT_TRUE(cmd.leftSpeed > 0, "Forward+right: both positive");
    ASSERT_TRUE(cmd.rightSpeed > 0, "Forward+right: both positive");
}

// ---------------------------------------------------------------------------
// Test: Speed Limiting Per Mode
// ---------------------------------------------------------------------------
void testSpeedLimiting() {
    Serial.println("\n=== Test: Speed Limiting ===");
    
    MapperConfig config;
    config.deadzone = 0.0f;
    config.curve = 1.0f;
    config.maxSpeedSlow = 30;
    config.maxSpeedNormal = 60;
    config.maxSpeedFast = 100;
    Mapper mapper(config);
    
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.vx = 1.0f;  // Full forward
    state.vy = 0.0f;
    
    // Test SLOW mode
    state.mode = DRIVE_MODE_SLOW;
    mapper.map(state, cmd);
    ASSERT_EQ(30, cmd.leftSpeed, "SLOW mode: max speed 30");
    
    // Test NORMAL mode
    state.mode = DRIVE_MODE_NORMAL;
    mapper.map(state, cmd);
    ASSERT_EQ(60, cmd.leftSpeed, "NORMAL mode: max speed 60");
    
    // Test FAST mode
    state.mode = DRIVE_MODE_FAST;
    mapper.map(state, cmd);
    ASSERT_EQ(100, cmd.leftSpeed, "FAST mode: max speed 100");
}

// ---------------------------------------------------------------------------
// Test: Acceleration Ramping
// ---------------------------------------------------------------------------
void testRamping() {
    Serial.println("\n=== Test: Acceleration Ramping ===");
    
    MapperConfig config;
    config.deadzone = 0.0f;
    config.curve = 1.0f;
    config.maxSpeedNormal = 100;
    config.rampRate = 50.0f;  // 50 units/second
    Mapper mapper(config);
    
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    state.vx = 0.0f;
    state.vy = 0.0f;
    
    // Start from stop
    mapper.map(state, cmd);
    ASSERT_TRUE(cmd.isStop(), "Start from stop");
    
    delay(100);  // 0.1 seconds
    
    // Command full speed, but ramping should limit it
    state.vx = 1.0f;
    mapper.map(state, cmd);
    // Max change in 0.1s at 50 units/s = 5 units
    ASSERT_NEAR(5, cmd.leftSpeed, 3, "Ramp: max change ~5 in 0.1s");
    
    delay(100);  // Another 0.1 seconds
    
    mapper.map(state, cmd);
    // Another 5 units, total ~10
    ASSERT_NEAR(10, cmd.leftSpeed, 3, "Ramp: max change ~10 in 0.2s");
}

// ---------------------------------------------------------------------------
// Test: Safety Clamping
// ---------------------------------------------------------------------------
void testSafetyClamping() {
    Serial.println("\n=== Test: Safety Clamping ===");
    
    Mapper mapper;
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    
    // Even with extreme inputs, output should be clamped
    state.vx = 10.0f;  // Way beyond valid range
    state.vy = 10.0f;
    mapper.map(state, cmd);
    
    ASSERT_TRUE(cmd.leftSpeed >= -100 && cmd.leftSpeed <= 100, 
                "Left speed within [-100, 100]");
    ASSERT_TRUE(cmd.rightSpeed >= -100 && cmd.rightSpeed <= 100,
                "Right speed within [-100, 100]");
}

// ---------------------------------------------------------------------------
// Test: Reset Functionality
// ---------------------------------------------------------------------------
void testReset() {
    Serial.println("\n=== Test: Reset ===");
    
    Mapper mapper;
    ControlState state;
    CommandFrame cmd;
    state.deadman = true;
    state.mode = DRIVE_MODE_NORMAL;
    state.vx = 0.5f;
    state.vy = 0.0f;
    
    // Build up some state
    mapper.map(state, cmd);
    ASSERT_TRUE(!cmd.isStop(), "Before reset: movement");
    
    // Reset mapper
    mapper.reset();
    
    // Next command should not be affected by ramping
    state.vx = 1.0f;
    mapper.map(state, cmd);
    // Without reset, ramping would limit this
    // With reset, should be closer to target
    ASSERT_TRUE(cmd.leftSpeed > 10, "After reset: no ramping from previous state");
}

// ---------------------------------------------------------------------------
// Arduino Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor
    
    Serial.println("\n\n");
    Serial.println("========================================");
    Serial.println("     Mapper Unit Tests");
    Serial.println("========================================");
    
    // Run all tests
    testDeadmanSwitch();
    testStopMode();
    testDeadzone();
    testResponseCurves();
    testDifferentialDrive();
    testSpeedLimiting();
    testRamping();
    testSafetyClamping();
    testReset();
    
    // Summary
    Serial.println("\n========================================");
    Serial.println("          Test Summary");
    Serial.println("========================================");
    Serial.printf("Tests Passed: %d\n", testsPassed);
    Serial.printf("Tests Failed: %d\n", testsFailed);
    Serial.printf("Success Rate: %.1f%%\n", 
                  (float)testsPassed / (testsPassed + testsFailed) * 100.0f);
    
    if (testsFailed == 0) {
        Serial.println("\n✓ ALL TESTS PASSED!");
    } else {
        Serial.println("\n✗ SOME TESTS FAILED!");
    }
}

void loop() {
    // Tests run once in setup()
    delay(10000);
}
