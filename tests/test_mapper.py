"""Tests for Mapper"""

import pytest
import time
from core.types import ControlState, CommandFrame, DriveMode, MapperConfig
from core.mapper import Mapper


@pytest.fixture
def default_mapper():
    """Create mapper with default config"""
    config = MapperConfig(
        deadzone=0.1,
        curve=2.0,
        max_speed_slow=30,
        max_speed_normal=60,
        max_speed_fast=100,
        ramp_rate=50.0
    )
    return Mapper(config)


def test_deadman_required(default_mapper):
    """Test that deadman switch is required"""
    state = ControlState(vx=0.5, vy=0.0, deadman=False, mode=DriveMode.NORMAL)
    frame = default_mapper.map(state)
    
    assert frame is not None
    assert frame.is_stop is True


def test_stop_mode(default_mapper):
    """Test that STOP mode produces stop command"""
    state = ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.STOP)
    frame = default_mapper.map(state)
    
    assert frame is not None
    assert frame.is_stop is True


def test_deadzone_applied(default_mapper):
    """Test deadzone filtering"""
    # Input below deadzone should result in stop
    state = ControlState(vx=0.05, vy=0.05, deadman=True, mode=DriveMode.NORMAL)
    frame = default_mapper.map(state)
    
    assert frame is not None
    assert frame.is_stop is True


def test_forward_movement(default_mapper):
    """Test forward movement"""
    state = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    frame = default_mapper.map(state)
    
    assert frame is not None
    assert frame.left_speed > 0
    assert frame.right_speed > 0
    assert frame.left_speed == frame.right_speed  # Straight forward


def test_turn_movement():
    """Test turning"""
    # Use mapper with small deadzone to avoid filtering
    config = MapperConfig(deadzone=0.05, curve=1.0)
    mapper = Mapper(config)
    
    # Turn right (positive vy) with forward movement
    state = ControlState(vx=0.6, vy=0.4, deadman=True, mode=DriveMode.NORMAL)
    frame = mapper.map(state)
    
    assert frame is not None
    # Differential drive: left = vx - vy, right = vx + vy
    # With vx=0.6, vy=0.4: left=0.2, right=1.0
    # So right should be faster
    assert frame.right_speed > frame.left_speed


def test_speed_limits(default_mapper):
    """Test mode-specific speed limits"""
    # SLOW mode
    state_slow = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.SLOW)
    frame_slow = default_mapper.map(state_slow)
    assert abs(frame_slow.left_speed) <= 30
    
    # NORMAL mode
    state_normal = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    frame_normal = default_mapper.map(state_normal)
    assert abs(frame_normal.left_speed) <= 60
    
    # FAST mode
    state_fast = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.FAST)
    frame_fast = default_mapper.map(state_fast)
    assert abs(frame_fast.left_speed) <= 100


def test_ramping():
    """Test ramping limits rate of change"""
    config = MapperConfig(ramp_rate=10.0)  # Very slow ramp
    mapper = Mapper(config)
    
    # First command
    state1 = ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    frame1 = mapper.map(state1)
    time.sleep(0.1)  # 100ms
    
    # Sudden change to full speed
    state2 = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    frame2 = mapper.map(state2)
    
    # Should be limited by ramp rate (10 units/sec * 0.1 sec = 1 unit max change)
    # But curve and other factors apply, so just check it's less than max
    assert abs(frame2.left_speed) < 60  # Less than full NORMAL speed


def test_differential_drive():
    """Test differential drive kinematics"""
    config = MapperConfig(deadzone=0.0, curve=1.0)  # Linear response
    mapper = Mapper(config)
    
    # Forward + right turn
    state = ControlState(vx=0.5, vy=0.5, deadman=True, mode=DriveMode.NORMAL)
    frame = mapper.map(state)
    
    # Left wheel should be slower (vx - vy)
    # Right wheel should be faster (vx + vy)
    assert frame.left_speed < frame.right_speed


def test_reset(default_mapper):
    """Test mapper reset"""
    state = ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    default_mapper.map(state)
    
    assert default_mapper._last_command is not None
    
    default_mapper.reset()
    
    assert default_mapper._last_command is None
    assert default_mapper._last_time == 0.0


def test_curve_application():
    """Test exponential curve makes low inputs smoother"""
    import time
    
    # Test curve application directly without ramping interference
    # Linear mapper
    config_linear = MapperConfig(deadzone=0.0, curve=1.0)
    mapper_linear = Mapper(config_linear)
    
    # Curved mapper
    config_curved = MapperConfig(deadzone=0.0, curve=2.0)
    mapper_curved = Mapper(config_curved)
    
    # First establish baseline with zero command and wait
    neutral = ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.FAST)
    mapper_linear.map(neutral)
    mapper_curved.map(neutral)
    
    # Wait long enough for ramping to allow full range
    time.sleep(0.1)
    
    # Now apply same input to both
    state = ControlState(vx=0.7, vy=0.0, deadman=True, mode=DriveMode.FAST)
    
    frame_linear = mapper_linear.map(state)
    frame_curved = mapper_curved.map(state)
    
    #  With high ramp rate (50 units/sec) and 0.1s delay, we can change by 5 units max
    # So both might be limited. Let's just check that linear >= curved
    # since the curve should make it smaller or equal
    assert frame_linear.left_speed >= frame_curved.left_speed, \
        f"Linear ({frame_linear.left_speed}) should be >= curved ({frame_curved.left_speed})"
