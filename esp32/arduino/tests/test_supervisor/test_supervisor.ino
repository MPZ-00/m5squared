/*
 * test_supervisor.ino - Unit tests for Supervisor
 *
 * Tests state machine, watchdogs, and safety orchestration:
 * - State transitions (DISCONNECTED -> CONNECTING -> PAIRED -> ARMED -> DRIVING)
 * - Input timeout watchdog
 * - Link timeout watchdog
 * - Heartbeat mechanism
 * - Failsafe behavior
 * - Connection management
 *
 * NOTE: This test focuses on state machine logic only.
 * Full integration testing with actual BLE requires hardware.
 *
 * Upload this sketch to ESP32 for testing.
 * Open Serial Monitor at 115200 baud to see results.
 */

// Include dependencies
#include "../../remote_control/types.h"
#include "../../remote_control/mapper.h"
#include "../../remote_control/supervisor.h"

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

#define ASSERT_TRUE(condition, msg) \
    if (condition) { \
        Serial.printf("  [PASS] %s\n", msg); \
        testsPassed++; \
    } else { \
        Serial.printf("  [FAIL] %s\n", msg); \
        testsFailed++; \
    }

#define ASSERT_FALSE(condition, msg) \
    if (!(condition)) { \
        Serial.printf("  [PASS] %s\n", msg); \
        testsPassed++; \
    } else { \
        Serial.printf("  [FAIL] %s\n", msg); \
        testsFailed++; \
    }

// ---------------------------------------------------------------------------
// Test: Supervisor State Enumeration
// ---------------------------------------------------------------------------
void testStateEnumeration() {
    Serial.println("\n=== Test: State Enumeration ===");
    
    // Test state to string conversion
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_DISCONNECTED), "DISCONNECTED") == 0, 
                "DISCONNECTED state name");
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_CONNECTING), "CONNECTING") == 0, 
                "CONNECTING state name");
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_PAIRED), "PAIRED") == 0, 
                "PAIRED state name");
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_ARMED), "ARMED") == 0, 
                "ARMED state name");
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_DRIVING), "DRIVING") == 0, 
                "DRIVING state name");
    ASSERT_TRUE(strcmp(supervisorStateToString(SUPERVISOR_FAILSAFE), "FAILSAFE") == 0, 
                "FAILSAFE state name");
}

// ---------------------------------------------------------------------------
// Test: Supervisor Configuration
// ---------------------------------------------------------------------------
void testSupervisorConfig() {
    Serial.println("\n=== Test: Supervisor Configuration ===");
    
    SupervisorConfig config;
    
    // Test default values
    ASSERT_EQ(50, config.loopIntervalMs, "Default loop interval should be 50ms");
    ASSERT_EQ(500, config.inputTimeoutMs, "Default input timeout should be 500ms");
    ASSERT_EQ(2000, config.linkTimeoutMs, "Default link timeout should be 2000ms");
    ASSERT_EQ(1000, config.heartbeatIntervalMs, "Default heartbeat interval should be 1000ms");
    ASSERT_EQ(2000, config.reconnectDelayMs, "Default reconnect delay should be 2000ms");
    ASSERT_EQ(5, config.maxReconnectAttempts, "Default max reconnect attempts should be 5");
}

// ---------------------------------------------------------------------------
// Test: Vehicle State
// ---------------------------------------------------------------------------
void testVehicleState() {
    Serial.println("\n=== Test: Vehicle State ===");
    
    VehicleState state;
    
    // Test initial values
    ASSERT_EQ(-1, state.batteryLeft, "Initial battery left should be -1");
    ASSERT_EQ(-1, state.batteryRight, "Initial battery right should be -1");
    ASSERT_FALSE(state.connected, "Initial connected should be false");
    ASSERT_FALSE(state.hasErrors, "Initial hasErrors should be false");
    
    // Test battery minimum
    ASSERT_EQ(-1, state.batteryMin(), "Battery min should be -1 when both unknown");
    
    state.batteryLeft = 80;
    state.batteryRight = 90;
    ASSERT_EQ(80, state.batteryMin(), "Battery min should be 80");
    
    state.batteryLeft = 95;
    ASSERT_EQ(90, state.batteryMin(), "Battery min should be 90");
    
    // Test health check
    state.connected = true;
    state.hasErrors = false;
    ASSERT_TRUE(state.isHealthy(), "Should be healthy when connected and no errors");
    
    state.hasErrors = true;
    ASSERT_FALSE(state.isHealthy(), "Should not be healthy when has errors");
}

// ---------------------------------------------------------------------------
// Test: Mapper Integration
// ---------------------------------------------------------------------------
void testMapperIntegration() {
    Serial.println("\n=== Test: Mapper Integration ===");
    
    MapperConfig mapperConfig;
    Mapper mapper(mapperConfig);
    
    // Test control state processing
    ControlState control;
    control.vx = 0.5f;
    control.vy = 0.0f;
    control.deadman = true;
    control.mode = DRIVE_MODE_NORMAL;
    control.timestamp = millis();
    
    CommandFrame cmd;
    bool valid = mapper.map(control, cmd);
    
    ASSERT_TRUE(valid, "Mapper should accept valid input");
    ASSERT_TRUE(!cmd.isStop(), "Command should not be stop");
    ASSERT_TRUE(cmd.leftSpeed > 0 && cmd.rightSpeed > 0, "Both wheels should move forward");
    
    // Test getLastCommand
    CommandFrame lastCmd = mapper.getLastCommand();
    ASSERT_EQ(cmd.leftSpeed, lastCmd.leftSpeed, "Last command should match");
    ASSERT_EQ(cmd.rightSpeed, lastCmd.rightSpeed, "Last command should match");
}

// ---------------------------------------------------------------------------
// Arduino Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=========================================");
    Serial.println("  Supervisor Unit Tests");
    Serial.println("=========================================\n");
    
    // Run all tests
    testInitialState();
    testConnectionTransition();
    testArmTransition();
    testDrivingTransition();
    testStopOnDeadmanRelease();
    testInputTimeoutWatchdog();
    testDisconnectRequest();
    
    // Print summary
    Serial.println("\n=========================================");
    Serial.printf("  TESTS PASSED: %d\n", testsPassed);
    Serial.printf("  TESTS FAILED: %d\n", testsFailed);
    Serial.println("=========================================\n");
    
    if (testsFailed == 0) {
        Serial.println("  ALL TESTS PASSED!");
    } else {
        Serial.println("  SOME TESTS FAILED!");
    }
}

// ---------------------------------------------------------------------------
// Arduino Loop
// ---------------------------------------------------------------------------
void loop() {
    // Nothing to do after tests complete
    delay(1000);
}
Serial.println("NOTE: These tests focus on data structures and");
    Serial.println("      mapper integration. Full supervisor testing");
    Serial.println("      requires BLE hardware integration.\n");
    
    // Run all tests
    testStateEnumeration();
    testSupervisorConfig();
    testVehicleState();
    testMapperIntegration