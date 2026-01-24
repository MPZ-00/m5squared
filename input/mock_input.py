"""
Mock (test) input provider.

Provides scripted control input or keyboard controls for testing without physical hardware.
"""

import asyncio
import logging
from typing import Optional, List, Dict
from core.types import ControlState, DriveMode


logger = logging.getLogger(__name__)


class MockInput:
    """
    Mock input provider for testing.
    
    Can use either:
    - Scripted control states for automated testing
    - Keyboard input (via async polling)
    """
    
    def __init__(
        self,
        states: Optional[List[ControlState]] = None,
        use_keyboard: bool = False
    ) -> None:
        """
        Initialize mock input.
        
        Args:
            states: List of control states to return in sequence.
                   If None, returns neutral state.
            use_keyboard: If True, reads from keyboard instead of script
        """
        self._states = states or []
        self._index = 0
        self._running = False
        self._use_keyboard = use_keyboard
        
        # Keyboard state
        self._keys_pressed: Dict[str, bool] = {}
        self._vx = 0.0
        self._vy = 0.0
        self._deadman = False
        self._mode = DriveMode.NORMAL
        
        self._default_state = ControlState(
            vx=0.0,
            vy=0.0,
            deadman=False,
            mode=DriveMode.STOP
        )
    
    async def start(self) -> None:
        """Start the input provider"""
        if self._use_keyboard:
            logger.info("[MOCK INPUT] Started - Keyboard mode")
            logger.info("Controls: W/S=forward/back, A/D=left/right, SPACE=deadman, Q=quit")
        else:
            logger.info(f"[MOCK INPUT] Started - Script mode ({len(self._states)} states)")
        self._running = True
        self._index = 0
    
    async def stop(self) -> None:
        """Stop the input provider"""
        logger.info("[MOCK INPUT] Stopped")
        self._running = False
    
    async def read_control_state(self) -> Optional[ControlState]:
        """Return next scripted state or keyboard state"""
        if not self._running:
            return None
        
        if self._use_keyboard:
            return await self._read_keyboard_state()
        
        # Scripted mode
        if not self._states:
            return self._default_state
        
        if self._index >= len(self._states):
            # Loop back to start or return last state
            return self._states[-1]
        
        state = self._states[self._index]
        self._index += 1
        return state
    
    async def _read_keyboard_state(self) -> ControlState:
        """
        Read keyboard input (simulated for now).
        
        Note: Real keyboard reading requires platform-specific code.
        For demo purposes, this returns gradually changing values.
        """
        # In a real implementation, you would use:
        # - Windows: msvcrt.kbhit() / msvcrt.getch()
        # - Linux: pynput or pygame
        
        # For demo: cycle through a simple pattern
        t = self._index * 0.1
        self._index += 1
        
        # Simple pattern: forward, turn, backward
        if t < 2.0:
            self._vx = 0.5
            self._vy = 0.0
            self._deadman = True
        elif t < 4.0:
            self._vx = 0.5
            self._vy = 0.5
            self._deadman = True
        elif t < 6.0:
            self._vx = -0.3
            self._vy = 0.0
            self._deadman = True
        else:
            self._vx = 0.0
            self._vy = 0.0
            self._deadman = False
        
        return ControlState(
            vx=self._vx,
            vy=self._vy,
            deadman=self._deadman,
            mode=self._mode
        )
    
    def reset(self) -> None:
        """Reset to beginning of script"""
        self._index = 0
    
    def load_script(self, script_name: str) -> None:
        """
        Load a predefined test script.
        
        Args:
            script_name: Name of script to load from TestScripts
        """
        script_map = {
            "forward": TestScripts.forward_drive(),
            "emergency_stop": TestScripts.emergency_stop(),
            "turn": TestScripts.turn_test(),
            "forward_turn_stop": TestScripts.forward_turn_stop(),
        }
        
        if script_name in script_map:
            self._states = script_map[script_name]
            self._use_keyboard = False
            logger.info(f"Loaded script '{script_name}' with {len(self._states)} states")
        else:
            logger.warning(f"Unknown script '{script_name}'")



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
    
    @staticmethod
    def forward_turn_stop() -> List[ControlState]:
        """Combined test: forward, turn, stop"""
        return [
            # Engage and start
            ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.5, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Forward
            ControlState(vx=0.7, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.7, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Turn right while moving
            ControlState(vx=0.6, vy=0.6, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.6, vy=0.6, deadman=True, mode=DriveMode.NORMAL),
            # Turn left
            ControlState(vx=0.6, vy=-0.6, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.6, vy=-0.6, deadman=True, mode=DriveMode.NORMAL),
            # Slow down
            ControlState(vx=0.3, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.1, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            # Stop and disengage
            ControlState(vx=0.0, vy=0.0, deadman=True, mode=DriveMode.NORMAL),
            ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.NORMAL),
        ]

