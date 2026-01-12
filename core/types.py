"""
Core data types for m5Squared control system.

All the data structures that flow through the system, fully typed.
"""

from dataclasses import dataclass, field
from enum import Enum, IntEnum
from typing import List, Optional
import time


class DriveMode(IntEnum):
    """Drive mode selection - affects max speed and behavior"""
    STOP = 0      # Emergency stop / parked
    SLOW = 1      # Reduced speed for indoor/tight spaces
    NORMAL = 2    # Standard outdoor speed
    FAST = 3      # Maximum speed (use with caution)


class SupervisorState(Enum):
    """Supervisor state machine states"""
    DISCONNECTED = "disconnected"  # No connection to vehicles
    CONNECTING = "connecting"      # Attempting to connect
    PAIRED = "paired"              # Connected but not ready to drive
    ARMED = "armed"                # Ready to drive, waiting for input
    DRIVING = "driving"            # Actively controlling vehicles
    FAILSAFE = "failsafe"         # Emergency state, sending stop commands


@dataclass
class ControlState:
    """
    Normalized control input from any input provider.
    
    This is the output of all InputProvider implementations.
    Values are normalized and ready for mapping.
    """
    vx: float                    # Forward/backward: -1.0 (back) to 1.0 (forward)
    vy: float                    # Left/right: -1.0 (left) to 1.0 (right)
    deadman: bool                # Hold-to-drive safety switch
    mode: DriveMode              # Current drive mode
    timestamp: float = field(default_factory=time.time)
    
    def __post_init__(self) -> None:
        """Validate ranges"""
        assert -1.0 <= self.vx <= 1.0, f"vx out of range: {self.vx}"
        assert -1.0 <= self.vy <= 1.0, f"vy out of range: {self.vy}"
    
    @property
    def is_neutral(self) -> bool:
        """Check if joystick is in neutral position"""
        return abs(self.vx) < 0.01 and abs(self.vy) < 0.01
    
    @property
    def is_safe(self) -> bool:
        """Check if control state is safe to execute"""
        return self.deadman and self.mode != DriveMode.STOP


@dataclass
class CommandFrame:
    """
    Command frame to send to vehicles.
    
    This is the output of the Mapper and input to the Transport.
    Contains actual speed values and flags ready for transmission.
    """
    left_speed: int              # Left wheel speed: -100 to 100
    right_speed: int             # Right wheel speed: -100 to 100
    flags: int = 0               # Additional flags (mode bits, etc.)
    timestamp: float = field(default_factory=time.time)
    
    def __post_init__(self) -> None:
        """Validate ranges"""
        assert -100 <= self.left_speed <= 100, f"left_speed out of range: {self.left_speed}"
        assert -100 <= self.right_speed <= 100, f"right_speed out of range: {self.right_speed}"
    
    @property
    def is_stop(self) -> bool:
        """Check if this is a stop command"""
        return self.left_speed == 0 and self.right_speed == 0
    
    @classmethod
    def stop(cls) -> "CommandFrame":
        """Create a stop command"""
        return cls(left_speed=0, right_speed=0, flags=0)


@dataclass
class VehicleState:
    """
    Current state of the vehicle(s).
    
    This is read from the Transport and represents the actual
    state of the hardware.
    """
    battery_left: Optional[int] = None      # Left wheel battery % (0-100)
    battery_right: Optional[int] = None     # Right wheel battery % (0-100)
    speed_kmh: Optional[float] = None       # Current speed in km/h
    distance_km: Optional[float] = None     # Total distance traveled
    errors: List[str] = field(default_factory=list)  # Error messages
    connected: bool = True                  # Connection status
    timestamp: float = field(default_factory=time.time)
    
    @property
    def battery_min(self) -> Optional[int]:
        """Get minimum battery level (limiting factor)"""
        batteries = [b for b in [self.battery_left, self.battery_right] if b is not None]
        return min(batteries) if batteries else None
    
    @property
    def has_errors(self) -> bool:
        """Check if there are any errors"""
        return len(self.errors) > 0
    
    @property
    def is_healthy(self) -> bool:
        """Check if vehicle is healthy and ready"""
        return self.connected and not self.has_errors


@dataclass
class MapperConfig:
    """Configuration for the Mapper"""
    deadzone: float = 0.1              # Ignore inputs below this threshold
    curve: float = 2.0                 # Exponential curve (1.0 = linear, >1 = more gradual)
    max_speed_slow: int = 30           # Max speed in SLOW mode (0-100)
    max_speed_normal: int = 60         # Max speed in NORMAL mode (0-100)
    max_speed_fast: int = 100          # Max speed in FAST mode (0-100)
    ramp_rate: float = 50.0            # Max speed change per second (units/sec)
    
    def get_max_speed(self, mode: DriveMode) -> int:
        """Get max speed for a given drive mode"""
        speed_map = {
            DriveMode.STOP: 0,
            DriveMode.SLOW: self.max_speed_slow,
            DriveMode.NORMAL: self.max_speed_normal,
            DriveMode.FAST: self.max_speed_fast,
        }
        return speed_map.get(mode, self.max_speed_normal)


@dataclass
class SupervisorConfig:
    """Configuration for the Supervisor"""
    loop_interval: float = 0.05        # Main loop interval (20Hz)
    input_timeout: float = 0.5         # Max time without input before failsafe
    link_timeout: float = 2.0          # Max time without link before failsafe
    heartbeat_interval: float = 1.0    # Send heartbeat every N seconds
    reconnect_delay: float = 2.0       # Wait N seconds before reconnect attempt
    max_reconnect_attempts: int = 3    # Max reconnection attempts before giving up
