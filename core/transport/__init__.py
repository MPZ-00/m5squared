"""
Mock Transport - For testing without hardware.

Simulates vehicle connection and responses without actual Bluetooth.
"""

import asyncio
import logging
from typing import Optional
from core.types import CommandFrame, VehicleState


logger = logging.getLogger(__name__)


class MockTransport:
    """
    Mock transport for testing.
    
    Logs commands instead of sending them, returns fake vehicle state.
    """
    
    def __init__(self, simulate_errors: bool = False, connection_delay: float = 0.1) -> None:
        """
        Initialize mock transport.
        
        Args:
            simulate_errors: If True, randomly inject errors
            connection_delay: Delay to simulate connection time
        """
        self._connected = False
        self._simulate_errors = simulate_errors
        self._connection_delay = connection_delay
        
        self._last_command: Optional[CommandFrame] = None
        self._command_count = 0
        
        # Simulated vehicle state
        self._battery_left = 85
        self._battery_right = 83
        self._speed = 0.0
        self._distance = 0.0
    
    async def connect(
        self,
        left_addr: str,
        right_addr: str,
        left_key: bytes,
        right_key: bytes
    ) -> bool:
        """Simulate connection"""
        logger.info(f"[MOCK] Connecting to {left_addr} and {right_addr}")
        
        # Simulate connection delay
        await asyncio.sleep(self._connection_delay)
        
        self._connected = True
        logger.info("[MOCK] Connected successfully")
        return True
    
    async def disconnect(self) -> None:
        """Simulate disconnection"""
        logger.info("[MOCK] Disconnecting")
        self._connected = False
        self._last_command = None
    
    async def send_command(self, frame: CommandFrame) -> bool:
        """Log command instead of sending"""
        if not self._connected:
            logger.warning("[MOCK] Cannot send command - not connected")
            return False
        
        self._last_command = frame
        self._command_count += 1
        
        # Update simulated speed based on command
        avg_speed = (frame.left_speed + frame.right_speed) / 2
        self._speed = abs(avg_speed) / 10.0  # Convert to km/h (rough sim)
        
        logger.debug(
            f"[MOCK] Command #{self._command_count}: "
            f"L={frame.left_speed:+4d} R={frame.right_speed:+4d} "
            f"-> {self._speed:.1f} km/h"
        )
        
        return True
    
    async def read_state(self) -> Optional[VehicleState]:
        """Return simulated vehicle state"""
        if not self._connected:
            return None
        
        # Simulate battery drain (very slowly)
        if self._command_count % 100 == 0 and self._battery_left > 0:
            self._battery_left -= 1
            self._battery_right -= 1
        
        # Simulate errors if configured
        errors = []
        if self._simulate_errors and self._command_count % 50 == 0:
            errors.append("Simulated error for testing")
        
        return VehicleState(
            battery_left=self._battery_left,
            battery_right=self._battery_right,
            speed_kmh=self._speed,
            distance_km=self._distance,
            errors=errors,
            connected=True
        )
    
    @property
    def is_connected(self) -> bool:
        """Check connection status"""
        return self._connected
    
    @property
    def last_command(self) -> Optional[CommandFrame]:
        """Get last command sent (for testing)"""
        return self._last_command
    
    @property
    def command_count(self) -> int:
        """Get total commands sent (for testing)"""
        return self._command_count
