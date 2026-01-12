"""
Mock (test) input provider.

Provides scripted control input for testing without physical hardware.
"""

import asyncio
import logging
from typing import Optional, List
from core.types import ControlState, DriveMode


logger = logging.getLogger(__name__)


class MockInput:
    """
    Mock input provider for testing.
    
    Returns scripted control states for automated testing.
    """
    
    def __init__(self, states: Optional[List[ControlState]] = None) -> None:
        """
        Initialize mock input.
        
        Args:
            states: List of control states to return in sequence.
                   If None, returns neutral state.
        """
        self._states = states or []
        self._index = 0
        self._running = False
        self._default_state = ControlState(
            vx=0.0,
            vy=0.0,
            deadman=False,
            mode=DriveMode.STOP
        )
    
    async def start(self) -> None:
        """Start the input provider"""
        logger.info("[MOCK INPUT] Started")
        self._running = True
        self._index = 0
    
    async def stop(self) -> None:
        """Stop the input provider"""
        logger.info("[MOCK INPUT] Stopped")
        self._running = False
    
    async def read_control_state(self) -> Optional[ControlState]:
        """Return next scripted state"""
        if not self._running:
            return None
        
        if not self._states:
            return self._default_state
        
        if self._index >= len(self._states):
            # Loop back to start or return last state
            return self._states[-1]
        
        state = self._states[self._index]
        self._index += 1
        return state
    
    def reset(self) -> None:
        """Reset to beginning of script"""
        self._index = 0


class TestScripts:
    """Pre-defined test scripts"""
    
    @staticmethod
    def forward_drive() -> List[ControlState]:
        """Simple forward drive test"""
        return [
            # Start neutral
            ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
            # Engage deadman
            ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Gradual acceleration
            ControlState(vx=0.3, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.6, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.8, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Maintain speed
            ControlState(vx=0.8, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.8, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Gradual deceleration
            ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.2, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Stop
            ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
        ]
    
    @staticmethod
    def emergency_stop() -> List[ControlState]:
        """Test deadman release during movement"""
        return [
            # Start driving
            ControlState(vx=0.8, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.8, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Emergency stop (release deadman)
            ControlState(vx=0.8, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
            # Stay stopped
            ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
        ]
    
    @staticmethod
    def turn_test() -> List[ControlState]:
        """Test turning maneuvers"""
        return [
            # Forward with right turn
            ControlState(vx=0.5, vy=0.5, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.5, vy=0.5, deadman=True, mode=DriveMode.NORMAL),
            # Forward with left turn
            ControlState(vx=0.5, vy=-0.5, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.5, vy=-0.5, deadman=True, mode=DriveMode.NORMAL),
            # Stop
            ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
        ]
