"""Tests for core types"""

import pytest
import time
from core.types import (
    ControlState,
    CommandFrame,
    VehicleState,
    DriveMode,
    SupervisorState,
    MapperConfig,
    SupervisorConfig,
)


def test_control_state_valid():
    """Test valid control state creation"""
    state = ControlState(
        vx=0.5,
        vy=0.3,
        deadman=True,
        mode=DriveMode.NORMAL
    )
    assert state.vx == 0.5
    assert state.vy == 0.3
    assert state.deadman is True
    assert state.mode == DriveMode.NORMAL
    assert state.timestamp > 0


def test_control_state_validation():
    """Test control state validates ranges"""
    with pytest.raises(AssertionError):
        ControlState(vx=1.5, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    
    with pytest.raises(AssertionError):
        ControlState(vx=0.0, vy=-1.5, deadman=True, mode=DriveMode.NORMAL)


def test_control_state_is_neutral():
    """Test neutral detection"""
    neutral = ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    assert neutral.is_neutral is True
    
    not_neutral = ControlState(vx=0.1, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    assert not_neutral.is_neutral is False


def test_control_state_is_safe():
    """Test safety check"""
    safe = ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.NORMAL)
    assert safe.is_safe is True
    
    unsafe_no_deadman = ControlState(vx=0.5, vy=0.0, deadman=False, mode=DriveMode.NORMAL)
    assert unsafe_no_deadman.is_safe is False
    
    unsafe_stop = ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.STOP)
    assert unsafe_stop.is_safe is False


def test_command_frame_valid():
    """Test valid command frame creation"""
    frame = CommandFrame(left_speed=50, right_speed=60, flags=0)
    assert frame.left_speed == 50
    assert frame.right_speed == 60
    assert frame.flags == 0
    assert frame.timestamp > 0


def test_command_frame_validation():
    """Test command frame validates ranges"""
    with pytest.raises(AssertionError):
        CommandFrame(left_speed=150, right_speed=50)
    
    with pytest.raises(AssertionError):
        CommandFrame(left_speed=50, right_speed=-150)


def test_command_frame_stop():
    """Test stop command creation"""
    stop = CommandFrame.stop()
    assert stop.is_stop is True
    assert stop.left_speed == 0
    assert stop.right_speed == 0


def test_vehicle_state():
    """Test vehicle state"""
    state = VehicleState(
        battery_left=80,
        battery_right=75,
        speed_kmh=5.2,
        errors=[],
        connected=True
    )
    
    assert state.battery_min == 75
    assert state.has_errors is False
    assert state.is_healthy is True


def test_vehicle_state_with_errors():
    """Test vehicle state with errors"""
    state = VehicleState(
        battery_left=80,
        battery_right=75,
        errors=["Motor overheat"],
        connected=True
    )
    
    assert state.has_errors is True
    assert state.is_healthy is False


def test_mapper_config_get_max_speed():
    """Test mapper config speed limits"""
    config = MapperConfig()
    
    assert config.get_max_speed(DriveMode.STOP) == 0
    assert config.get_max_speed(DriveMode.SLOW) == 30
    assert config.get_max_speed(DriveMode.NORMAL) == 60
    assert config.get_max_speed(DriveMode.FAST) == 100


def test_drive_mode_enum():
    """Test drive mode enum values"""
    assert DriveMode.STOP == 0
    assert DriveMode.SLOW == 1
    assert DriveMode.NORMAL == 2
    assert DriveMode.FAST == 3


def test_supervisor_state_enum():
    """Test supervisor state enum"""
    assert SupervisorState.DISCONNECTED.value == "disconnected"
    assert SupervisorState.CONNECTING.value == "connecting"
    assert SupervisorState.PAIRED.value == "paired"
    assert SupervisorState.ARMED.value == "armed"
    assert SupervisorState.DRIVING.value == "driving"
    assert SupervisorState.FAILSAFE.value == "failsafe"
