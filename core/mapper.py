"""
Mapper - Transforms control input into safe vehicle commands.

This is safety-critical code. The Mapper enforces:
- Deadman switch requirement
- Deadzones to prevent drift
- Response curves for smooth control
- Speed limits per mode
- Ramping to prevent sudden changes
- Differential drive kinematics
"""

import time
import math
from typing import Optional
from .types import ControlState, CommandFrame, DriveMode, MapperConfig


class Mapper:
    """
    Transforms ControlState into CommandFrame with safety rules.
    
    This is the core safety layer - all unsafe inputs are filtered here.
    """
    
    def __init__(self, config: MapperConfig) -> None:
        """
        Initialize mapper with configuration.
        
        Args:
            config: Mapper configuration (deadzones, curves, limits)
        """
        self.config = config
        self._last_command: Optional[CommandFrame] = None
        self._last_time: float = 0.0
    
    def map(self, state: ControlState) -> Optional[CommandFrame]:
        """
        Convert ControlState to CommandFrame with all safety rules applied.
        
        Args:
            state: Input control state from any InputProvider
        
        Returns:
            CommandFrame to send, or None if unsafe (triggers stop)
        """
        current_time = time.time()
        
        # SAFETY RULE 1: Deadman switch must be pressed
        if not state.deadman:
            self._last_command = CommandFrame.stop()
            self._last_time = current_time
            return self._last_command
        
        # SAFETY RULE 2: STOP mode = no movement
        if state.mode == DriveMode.STOP:
            self._last_command = CommandFrame.stop()
            self._last_time = current_time
            return self._last_command
        
        # Apply processing pipeline
        vx = self._apply_deadzone(state.vx)
        vy = self._apply_deadzone(state.vy)
        
        vx = self._apply_curve(vx)
        vy = self._apply_curve(vy)
        
        # Convert to differential drive
        left, right = self._differential_drive(vx, vy)
        
        # Apply mode-specific speed limit
        max_speed = self.config.get_max_speed(state.mode)
        left = self._clamp(left, -max_speed, max_speed)
        right = self._clamp(right, -max_speed, max_speed)
        
        # Apply ramping if we have previous command
        if self._last_command is not None and self._last_time > 0:
            dt = current_time - self._last_time
            if dt > 0:  # Prevent division by zero
                left = self._apply_ramp(left, self._last_command.left_speed, dt)
                right = self._apply_ramp(right, self._last_command.right_speed, dt)
        
        # Build command frame
        frame = CommandFrame(
            left_speed=int(round(left)),
            right_speed=int(round(right)),
            flags=self._build_flags(state),
            timestamp=current_time
        )
        
        self._last_command = frame
        self._last_time = current_time
        
        return frame
    
    def reset(self) -> None:
        """Reset mapper state (e.g., when connection lost)"""
        self._last_command = None
        self._last_time = 0.0
    
    def _apply_deadzone(self, value: float) -> float:
        """
        Apply deadzone to eliminate drift and small movements.
        
        Input below deadzone threshold returns 0.
        Input above deadzone is rescaled to maintain full range.
        
        Args:
            value: Input value (-1.0 to 1.0)
        
        Returns:
            Deadzone-filtered value
        """
        if abs(value) < self.config.deadzone:
            return 0.0
        
        # Rescale so deadzone maps to 0, and 1.0 stays 1.0
        sign = 1.0 if value > 0 else -1.0
        magnitude = abs(value)
        scaled = (magnitude - self.config.deadzone) / (1.0 - self.config.deadzone)
        return sign * scaled
    
    def _apply_curve(self, value: float) -> float:
        """
        Apply exponential curve for smoother control.
        
        Curve > 1.0 makes the response more gradual at low inputs,
        giving finer control at slow speeds.
        
        Args:
            value: Input value (-1.0 to 1.0)
        
        Returns:
            Curved value
        """
        if value == 0.0:
            return 0.0
        
        sign = 1.0 if value > 0 else -1.0
        magnitude = abs(value)
        curved = math.pow(magnitude, self.config.curve)
        return sign * curved
    
    def _differential_drive(self, vx: float, vy: float) -> tuple[float, float]:
        """
        Convert forward/turn inputs to left/right wheel speeds.
        
        Standard differential drive kinematics:
        - vx (forward) adds equally to both wheels
        - vy (turn) adds to one wheel, subtracts from other
        
        Args:
            vx: Forward/backward (-1.0 to 1.0)
            vy: Left/right (-1.0 to 1.0)
        
        Returns:
            (left_speed, right_speed) in range -1.0 to 1.0
        """
        # Basic differential drive: left = vx - vy, right = vx + vy
        # But we need to handle magnitude > 1.0
        left = vx - vy
        right = vx + vy
        
        # Normalize if magnitude exceeds 1.0
        max_magnitude = max(abs(left), abs(right))
        if max_magnitude > 1.0:
            left /= max_magnitude
            right /= max_magnitude
        
        return left, right
    
    def _clamp(self, value: float, min_val: float, max_val: float) -> float:
        """Clamp value to range [min_val, max_val]"""
        return max(min_val, min(max_val, value))
    
    def _apply_ramp(self, target: float, current: float, dt: float) -> float:
        """
        Apply ramping to prevent sudden speed changes.
        
        Limits the rate of change to config.ramp_rate.
        
        Args:
            target: Desired speed
            current: Current speed
            dt: Time delta in seconds
        
        Returns:
            Ramped speed (may not reach target yet)
        """
        max_change = self.config.ramp_rate * dt
        delta = target - current
        
        if abs(delta) <= max_change:
            return target
        
        # Limit change to max_change
        sign = 1.0 if delta > 0 else -1.0
        return current + sign * max_change
    
    def _build_flags(self, state: ControlState) -> int:
        """
        Build flag bits from control state.
        
        Can be used to encode mode, special features, etc.
        
        Args:
            state: Control state
        
        Returns:
            Flags as integer
        """
        flags = 0
        
        # Encode drive mode in lower bits
        flags |= state.mode.value & 0x0F
        
        return flags
