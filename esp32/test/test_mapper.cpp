/*
 * test_mapper.cpp - Mapper unit tests
 * 
 * AUTO-GENERATED from tools/generate_tests.py
 * Tests are derived from Python reference implementation (core/mapper.py)
 * 
 * Verifies that C++ mapper behaves identically to Python version.
 */

#include <unity.h>
#include "../remote_control/mapper.h"
#include "../remote_control/types.h"

// Test configuration (matches Python test config)
static MapperConfig test_config = {
    .deadzone = 0.15f,
    .curve = 2.0f,
    .ramp_rate = 50.0f,
    .max_speed_slow = 30,
    .max_speed_normal = 60,
    .max_speed_fast = 100
};

static Mapper* mapper = nullptr;

void setUp(void) {
    mapper = new Mapper(test_config);
}

void tearDown(void) {
    delete mapper;
    mapper = nullptr;
}

// Test 1: Input within deadzone should produce zero output
void test_deadzone_below_threshold(void) {
    ControlState input;
    input.vx = 0.10f;
    input.vy = 0.10f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: STOP (left=0, right=0)
    TEST_ASSERT_EQUAL_INT(0, result.left_speed);
    TEST_ASSERT_EQUAL_INT(0, result.right_speed);
}

// Test 2: Input at deadzone threshold should produce zero output
void test_deadzone_at_threshold(void) {
    ControlState input;
    input.vx = 0.15f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: STOP (left=0, right=0)
    TEST_ASSERT_EQUAL_INT(0, result.left_speed);
    TEST_ASSERT_EQUAL_INT(0, result.right_speed);
}

// Test 3: Input above deadzone should produce non-zero output
void test_deadzone_above_threshold(void) {
    ControlState input;
    input.vx = 0.20f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=0, right=0
    TEST_ASSERT_INT_WITHIN(5, 0, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 0, result.right_speed);
}

// Test 4: Full forward joystick
void test_full_forward(void) {
    ControlState input;
    input.vx = 1.00f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=60, right=60
    TEST_ASSERT_INT_WITHIN(5, 60, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 60, result.right_speed);
}

// Test 5: Full backward joystick
void test_full_backward(void) {
    ControlState input;
    input.vx = -1.00f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=-60, right=-60
    TEST_ASSERT_INT_WITHIN(5, -60, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, -60, result.right_speed);
}

// Test 6: Forward with left turn
void test_turn_left(void) {
    ControlState input;
    input.vx = 0.50f;
    input.vy = -0.50f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=20, right=0
    TEST_ASSERT_INT_WITHIN(5, 20, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 0, result.right_speed);
}

// Test 7: Forward with right turn
void test_turn_right(void) {
    ControlState input;
    input.vx = 0.50f;
    input.vy = 0.50f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=0, right=20
    TEST_ASSERT_INT_WITHIN(5, 0, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 20, result.right_speed);
}

// Test 8: Rotate in place (left)
void test_rotate_left(void) {
    ControlState input;
    input.vx = 0.00f;
    input.vy = -0.80f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=35, right=-35
    TEST_ASSERT_INT_WITHIN(5, 35, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, -35, result.right_speed);
}

// Test 9: Rotate in place (right)
void test_rotate_right(void) {
    ControlState input;
    input.vx = 0.00f;
    input.vy = 0.80f;
    input.deadman = true;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: left=-35, right=35
    TEST_ASSERT_INT_WITHIN(5, -35, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 35, result.right_speed);
}

// Test 10: Deadman switch not pressed should stop
void test_no_deadman(void) {
    ControlState input;
    input.vx = 1.00f;
    input.vy = 0.00f;
    input.deadman = false;
    input.mode = DriveMode::NORMAL;

    CommandFrame result = mapper->map(input);

    // Expected: STOP (left=0, right=0)
    TEST_ASSERT_EQUAL_INT(0, result.left_speed);
    TEST_ASSERT_EQUAL_INT(0, result.right_speed);
}

// Test 11: STOP mode should override input
void test_stop_mode(void) {
    ControlState input;
    input.vx = 1.00f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::STOP;

    CommandFrame result = mapper->map(input);

    // Expected: STOP (left=0, right=0)
    TEST_ASSERT_EQUAL_INT(0, result.left_speed);
    TEST_ASSERT_EQUAL_INT(0, result.right_speed);
}

// Test 12: SLOW mode limits max speed
void test_slow_mode(void) {
    ControlState input;
    input.vx = 1.00f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::SLOW;

    CommandFrame result = mapper->map(input);

    // Expected: left=30, right=30
    TEST_ASSERT_INT_WITHIN(5, 30, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 30, result.right_speed);
}

// Test 13: FAST mode allows higher speeds
void test_fast_mode(void) {
    ControlState input;
    input.vx = 1.00f;
    input.vy = 0.00f;
    input.deadman = true;
    input.mode = DriveMode::FAST;

    CommandFrame result = mapper->map(input);

    // Expected: left=100, right=100
    TEST_ASSERT_INT_WITHIN(5, 100, result.left_speed);
    TEST_ASSERT_INT_WITHIN(5, 100, result.right_speed);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_deadzone_below_threshold);
    RUN_TEST(test_deadzone_at_threshold);
    RUN_TEST(test_deadzone_above_threshold);
    RUN_TEST(test_full_forward);
    RUN_TEST(test_full_backward);
    RUN_TEST(test_turn_left);
    RUN_TEST(test_turn_right);
    RUN_TEST(test_rotate_left);
    RUN_TEST(test_rotate_right);
    RUN_TEST(test_no_deadman);
    RUN_TEST(test_stop_mode);
    RUN_TEST(test_slow_mode);
    RUN_TEST(test_fast_mode);
    
    return UNITY_END();
}
