/*
 * test_supervisor.cpp - Supervisor state machine tests
 * 
 * AUTO-GENERATED from tools/generate_tests.py
 * Tests are derived from Python reference implementation (core/supervisor.py)
 * 
 * Verifies that C++ supervisor state machine behaves identically to Python version.
 */

#include <unity.h>
#include "supervisor.h"
#include "types.h"

static Supervisor* supervisor = nullptr;

void setUp(void) {
    SupervisorConfig config = {
        .input_timeout_ms = 50,
        .link_timeout_ms = 200,
        .heartbeat_interval_ms = 1000,
        .max_reconnect_attempts = 5
    };
    supervisor = new Supervisor(config);
}

void tearDown(void) {
    delete supervisor;
    supervisor = nullptr;
}

// Test 1: Initial state is DISCONNECTED
void test_initial_state_disconnected(void) {
    TEST_ASSERT_EQUAL(SupervisorState::DISCONNECTED, supervisor->getState());
}

// Test 2: Transition DISCONNECTED -> CONNECTING on connect request
void test_transition_to_connecting(void) {
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();
    TEST_ASSERT_EQUAL(SupervisorState::CONNECTING, supervisor->getState());
}

// Test 3: Transition CONNECTING -> READY on successful connection
void test_transition_to_ready(void) {
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();  // -> CONNECTING
    supervisor->onConnectionSuccess();
    supervisor->tick();  // -> READY
    TEST_ASSERT_EQUAL(SupervisorState::READY, supervisor->getState());
}

// Test 4: Transition READY -> OPERATING on joystick input
void test_transition_to_operating(void) {
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();
    supervisor->onConnectionSuccess();
    supervisor->tick();  // -> READY
    
    ControlState input;
    input.vx = 0.5f;
    input.vy = 0.0f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;
    
    supervisor->onInput(input);
    supervisor->tick();  // -> OPERATING
    TEST_ASSERT_EQUAL(SupervisorState::OPERATING, supervisor->getState());
}

// Test 5: Transition OPERATING -> ERROR on input timeout
void test_input_timeout_to_error(void) {
    // Setup: Get to OPERATING state
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();
    supervisor->onConnectionSuccess();
    supervisor->tick();
    
    ControlState input;
    input.vx = 0.5f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;
    supervisor->onInput(input);
    supervisor->tick();  // -> OPERATING
    
    // Wait for input timeout (50ms)
    delay(60);
    supervisor->tick();
    
    TEST_ASSERT_EQUAL(SupervisorState::ERROR, supervisor->getState());
}

// Test 6: Transition ERROR -> CONNECTING on reconnect
void test_error_recovery_reconnect(void) {
    // Setup: Get to ERROR state (simplified)
    supervisor->onError(ErrorType::CONNECTION_LOST);
    TEST_ASSERT_EQUAL(SupervisorState::ERROR, supervisor->getState());
    
    // Attempt reconnect
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();
    
    TEST_ASSERT_EQUAL(SupervisorState::CONNECTING, supervisor->getState());
}

// Test 7: Max reconnect attempts limit
void test_max_reconnect_attempts(void) {
    for (int i = 0; i < 5; i++) {
        supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
        supervisor->tick();  // -> CONNECTING
        supervisor->onConnectionFailure();  // Connection fails
        supervisor->tick();  // Back to ERROR
    }
    
    // 6th attempt should fail (max = 5)
    supervisor->requestConnect("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    supervisor->tick();
    
    TEST_ASSERT_EQUAL(SupervisorState::ERROR, supervisor->getState());
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_initial_state_disconnected);
    RUN_TEST(test_transition_to_connecting);
    RUN_TEST(test_transition_to_ready);
    RUN_TEST(test_transition_to_operating);
    RUN_TEST(test_input_timeout_to_error);
    RUN_TEST(test_error_recovery_reconnect);
    RUN_TEST(test_max_reconnect_attempts);
    
    return UNITY_END();
}
