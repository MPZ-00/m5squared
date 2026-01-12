"""
Gamepad Input Provider

Reads control input from USB/wireless game controllers.
Tested with "Controller 1.02" (no-name controller).
"""

import asyncio
import logging
from typing import Optional

try:
    import pygame
    HAS_PYGAME = True
except ImportError:
    HAS_PYGAME = False

from core.types import ControlState, DriveMode


logger = logging.getLogger(__name__)


class GamepadInput:
    """
    Game controller input provider.
    
    Maps gamepad controls to wheelchair movement:
    - Left stick: Movement (vx/vy)
    - A button: Deadman switch
    - D-pad up/down: Change drive mode
    """
    
    def __init__(
        self,
        deadzone: float = 0.1,
        invert_y: bool = False
    ) -> None:
        """
        Initialize gamepad input.
        
        Args:
            deadzone: Ignore stick movements below this threshold
            invert_y: Invert Y-axis (some controllers are backwards)
        """
        if not HAS_PYGAME:
            raise RuntimeError(
                "pygame not installed. Install with: pip install pygame"
            )
        
        self._deadzone = deadzone
        self._invert_y = invert_y
        
        self._joystick: Optional[pygame.joystick.Joystick] = None
        self._running = False
        self._mode = DriveMode.NORMAL
        
        # Button mapping (will be detected from controller)
        self._button_a = 0  # Usually button 0
        self._axis_x = 0    # Left stick X
        self._axis_y = 1    # Left stick Y
    
    async def start(self) -> None:
        """Initialize pygame and connect to controller"""
        if self._running:
            return
        
        logger.info("Initializing gamepad input...")
        
        # Initialize pygame joystick module
        pygame.init()
        pygame.joystick.init()
        
        # Find controller
        joystick_count = pygame.joystick.get_count()
        logger.info(f"Found {joystick_count} game controller(s)")
        
        if joystick_count == 0:
            raise RuntimeError("No game controllers found")
        
        # Use first controller
        self._joystick = pygame.joystick.Joystick(0)
        self._joystick.init()
        logger.info(f"Selected: {self._joystick.get_name()}")
        
        # Log controller capabilities
        logger.info(f"Axes: {self._joystick.get_numaxes()}")
        logger.info(f"Buttons: {self._joystick.get_numbuttons()}")
        logger.info(f"Hats: {self._joystick.get_numhats()}")
        logger.info("")
        logger.info("Controls:")
        logger.info("  Left stick: Movement (forward/back, left/right)")
        logger.info("  A button: Deadman switch (hold to enable)")
        logger.info("  D-pad up/down: Change drive mode")
        logger.info("")
        
        self._running = True
    
    async def stop(self) -> None:
        """Disconnect from controller"""
        logger.info("Stopping gamepad input")
        self._running = False
        
        if self._joystick:
            self._joystick.quit()
            self._joystick = None
        
        pygame.joystick.quit()
        pygame.quit()
    
    async def read_control_state(self) -> Optional[ControlState]:
        """Read current controller state"""
        if not self._running or not self._joystick:
            return None
        
        # Process pygame events (required to update joystick state)
        pygame.event.pump()
        
        # Read joystick axes
        x = self._joystick.get_axis(self._axis_x)
        y = self._joystick.get_axis(self._axis_y)
        
        # Apply deadzone
        if abs(x) < self._deadzone:
            x = 0.0
        if abs(y) < self._deadzone:
            y = 0.0
        
        # Invert Y if needed (forward = positive)
        if self._invert_y:
            y = -y
        else:
            y = -y  # Most controllers have up=negative, we want up=positive
        
        # Read A button (deadman)
        deadman = bool(self._joystick.get_button(self._button_a))
        
        # Check for mode changes (D-pad)
        if self._joystick.get_numhats() > 0:
            hat = self._joystick.get_hat(0)
            if hat[1] > 0:  # D-pad up
                self._cycle_mode_up()
            elif hat[1] < 0:  # D-pad down
                self._cycle_mode_down()
        
        # Debug output - show all controller inputs
        self._log_controller_state(x, y, deadman, hat if self._joystick.get_numhats() > 0 else (0, 0))
        
        return ControlState(
            vx=y,  # Forward/backward
            vy=x,  # Left/right (turn)
            deadman=deadman,
            mode=self._mode
        )
    
    def _log_controller_state(self, x: float, y: float, deadman: bool, hat: tuple) -> None:
        """Log detailed controller state for debugging"""
        # Build axes string
        axes_str = f"Axis 0 (X): {x:+.3f}, Axis 1 (Y): {y:+.3f}"
        
        # Add additional axes if present
        if self._joystick.get_numaxes() > 2:
            axes_str += f", Axis 2: {self._joystick.get_axis(2):+.3f}"
        if self._joystick.get_numaxes() > 3:
            axes_str += f", Axis 3: {self._joystick.get_axis(3):+.3f}"
        
        # Build buttons string - show which buttons are pressed
        pressed_buttons = []
        for i in range(self._joystick.get_numbuttons()):
            if self._joystick.get_button(i):
                pressed_buttons.append(str(i))
        buttons_str = f"Buttons: [{', '.join(pressed_buttons)}]" if pressed_buttons else "Buttons: []"
        
        # Hat (D-pad) state
        hat_str = f"Hat: {hat}"
        
        # Final control state
        control_str = f"vx={y:+.2f}, vy={x:+.2f}, deadman={deadman}, mode={self._mode.name}"
        
        logger.info(f"{axes_str} | {buttons_str} | {hat_str} | → {control_str}")
    
    def _cycle_mode_up(self) -> None:
        """Cycle to faster drive mode"""
        modes = [DriveMode.SLOW, DriveMode.NORMAL, DriveMode.FAST]
        try:
            idx = modes.index(self._mode)
            if idx < len(modes) - 1:
                self._mode = modes[idx + 1]
                logger.info(f"Mode: {self._mode.name}")
        except ValueError:
            self._mode = DriveMode.NORMAL
    
    def _cycle_mode_down(self) -> None:
        """Cycle to slower drive mode"""
        modes = [DriveMode.SLOW, DriveMode.NORMAL, DriveMode.FAST]
        try:
            idx = modes.index(self._mode)
            if idx > 0:
                self._mode = modes[idx - 1]
                logger.info(f"Mode: {self._mode.name}")
        except ValueError:
            self._mode = DriveMode.NORMAL
