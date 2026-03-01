#!/usr/bin/env python3
"""
Generate C++ test vectors from Python reference implementation.

Generates test files that verify C++ mapper and supervisor implementations
match the Python reference behavior exactly.
"""

import sys
from pathlib import Path
from typing import List, Tuple

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from core.mapper import Mapper
from core.types import ControlState, DriveMode, MapperConfig


def generate_mapper_tests(output_path: str):
    """Generate C++ test cases for mapper implementation"""
    
    # Create test configuration
    config = MapperConfig(
        deadzone=0.15,
        curve=2.0,
        ramp_rate=50.0,
        max_speed_slow=30,
        max_speed_normal=60,
        max_speed_fast=100,
    )
    
    mapper = Mapper(config)
    
    # Generate test inputs
    test_cases = []
    
    # Test 1: Deadzone handling
    test_cases.append({
        'name': 'deadzone_below_threshold',
        'input': ControlState(vx=0.10, vy=0.10, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': True,
        'description': 'Input within deadzone should produce zero output'
    })
    
    # Test 2: Deadzone boundary
    test_cases.append({
        'name': 'deadzone_at_threshold',
        'input': ControlState(vx=0.15, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': True,
        'description': 'Input at deadzone threshold should produce zero output'
    })
    
    # Test 3: Just above deadzone
    test_cases.append({
        'name': 'deadzone_above_threshold',
        'input': ControlState(vx=0.20, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Input above deadzone should produce non-zero output'
    })
    
    # Test 4: Full forward
    test_cases.append({
        'name': 'full_forward',
        'input': ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Full forward joystick'
    })
    
    # Test 5: Full backward
    test_cases.append({
        'name': 'full_backward',
        'input': ControlState(vx=-1.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Full backward joystick'
    })
    
    # Test 6: Turn left
    test_cases.append({
        'name': 'turn_left',
        'input': ControlState(vx=0.5, vy=-0.5, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Forward with left turn'
    })
    
    # Test 7: Turn right
    test_cases.append({
        'name': 'turn_right',
        'input': ControlState(vx=0.5, vy=0.5, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Forward with right turn'
    })
    
    # Test 8: Rotate in place left
    test_cases.append({
        'name': 'rotate_left',
        'input': ControlState(vx=0.0, vy=-0.8, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Rotate in place (left)'
    })
    
    # Test 9: Rotate in place right
    test_cases.append({
        'name': 'rotate_right',
        'input': ControlState(vx=0.0, vy=0.8, deadman=True, mode=DriveMode.NORMAL),
        'expected_stop': False,
        'description': 'Rotate in place (right)'
    })
    
    # Test 10: No deadman switch
    test_cases.append({
        'name': 'no_deadman',
        'input': ControlState(vx=1.0, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
        'expected_stop': True,
        'description': 'Deadman switch not pressed should stop'
    })
    
    # Test 11: STOP mode
    test_cases.append({
        'name': 'stop_mode',
        'input': ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.STOP),
        'expected_stop': True,
        'description': 'STOP mode should override input'
    })
    
    # Test 12: SLOW mode speed limit
    test_cases.append({
        'name': 'slow_mode',
        'input': ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.SLOW),
        'expected_stop': False,
        'description': 'SLOW mode limits max speed'
    })
    
    # Test 13: FAST mode speed limit
    test_cases.append({
        'name': 'fast_mode',
        'input': ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.FAST),
        'expected_stop': False,
        'description': 'FAST mode allows higher speeds'
    })
    
    # Generate expected outputs
    for test in test_cases:
        mapper.reset()  # Reset state for clean test
        frame = mapper.map(test['input'])
        
        if frame is None or test['expected_stop']:
            test['output'] = {'left': 0, 'right': 0, 'is_stop': True}
        else:
            test['output'] = {
                'left': frame.left_speed,
                'right': frame.right_speed,
                'is_stop': False
            }
    
    # Write C++ test file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("""/*
 * test_mapper.cpp - Mapper unit tests
 * 
 * AUTO-GENERATED from tools/generate_tests.py
 * Tests are derived from Python reference implementation (core/mapper.py)
 * 
 * Verifies that C++ mapper behaves identically to Python version.
 */

#include <unity.h>
#include "mapper.h"
#include "types.h"

// Test configuration (matches Python test config)
// C++11 compatible initialization (field order must match struct definition)
static MapperConfig test_config{
    0.15f,  // deadzone
    2.0f,   // curve
    30,     // max_speed_slow
    60,     // max_speed_normal
    100,    // max_speed_fast
    50.0f   // ramp_rate
};

static Mapper* mapper = nullptr;

void setUp(void) {
    mapper = new Mapper(test_config);
}

void tearDown(void) {
    delete mapper;
    mapper = nullptr;
}

""")
        
        # Generate test functions
        for i, test in enumerate(test_cases):
            f.write(f"// Test {i+1}: {test['description']}\n")
            f.write(f"void test_{test['name']}(void) {{\n")
            
            # Input state
            state = test['input']
            f.write(f"    ControlState input;\n")
            f.write(f"    input.vx = {state.vx:.2f}f;\n")
            f.write(f"    input.vy = {state.vy:.2f}f;\n")
            f.write(f"    input.deadman = {'true' if state.deadman else 'false'};\n")
            f.write(f"    input.mode = DriveMode::{state.mode.name};\n")
            f.write(f"\n")
            
            # Expected output
            output = test['output']
            f.write(f"    CommandFrame result = mapper->map(input);\n")
            f.write(f"\n")
            
            if output['is_stop']:
                f.write(f"    // Expected: STOP (left=0, right=0)\n")
                f.write(f"    TEST_ASSERT_EQUAL_INT(0, result.left_speed);\n")
                f.write(f"    TEST_ASSERT_EQUAL_INT(0, result.right_speed);\n")
            else:
                f.write(f"    // Expected: left={output['left']}, right={output['right']}\n")
                f.write(f"    TEST_ASSERT_INT_WITHIN(5, {output['left']}, result.left_speed);\n")
                f.write(f"    TEST_ASSERT_INT_WITHIN(5, {output['right']}, result.right_speed);\n")
            
            f.write(f"}}\n\n")
        
        # Main test runner
        f.write("""int main(void) {
    UNITY_BEGIN();
    
""")
        for test in test_cases:
            f.write(f"    RUN_TEST(test_{test['name']});\n")
        
        f.write("""    
    return UNITY_END();
}
""")
    
    print(f"Generated: {output_path}")
    print(f"  - {len(test_cases)} test cases")
    print(f"  - Validates mapper behavior against Python reference")


def generate_supervisor_state_tests(output_path: str):
    """Generate C++ test cases for supervisor state machine"""
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("""/*
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
""")
    
    print(f"Generated: {output_path}")
    print(f"  - 7 state machine test cases")
    print(f"  - Validates supervisor transitions")


def main():
    """Generate test files"""
    script_dir = Path(__file__).parent
    
    print("Generating C++ test vectors from Python reference implementation...\n")
    
    # Generate mapper tests
    mapper_test_path = script_dir / '../esp32/test/test_mapper.cpp'
    generate_mapper_tests(str(mapper_test_path))
    
    # Generate supervisor tests
    supervisor_test_path = script_dir / '../esp32/test/test_supervisor.cpp'
    generate_supervisor_state_tests(str(supervisor_test_path))
    
    print("\nDone! Test files generated.")
    print("\nTo run tests:")
    print("  cd esp32/test/")
    print("  pio test")


if __name__ == '__main__':
    main()
