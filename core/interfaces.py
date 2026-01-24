"""
Core interfaces (protocols) for pluggable components.

These define the contracts that all implementations must follow.
Python Protocols are like interfaces in Java/C# - they define
what methods a class must have without forcing inheritance.
"""

from typing import Protocol, Optional
from .types import ControlState, CommandFrame, VehicleState


class InputProvider(Protocol):
    """
    Interface for input sources (joystick, virtual, keyboard, etc.).
    
    All input providers must implement these methods to be usable
    by the Supervisor.
    """
    
    async def start(self) -> None:
        """
        Initialize and start the input provider.
        
        Called once when the system starts up.
        May open devices, create connections, etc.
        """
        ...
    
    async def stop(self) -> None:
        """
        Stop and cleanup the input provider.
        
        Called when shutting down.
        Must close devices, release resources, etc.
        """
        ...
    
    async def read_control_state(self) -> Optional[ControlState]:
        """
        Read current control state from input device.
        
        This should be non-blocking and return immediately.
        Returns None if no input available or device not ready.
        
        Returns:
            ControlState with current joystick/input values, or None
        """
        ...


class Transport(Protocol):
    """
    Interface for vehicle communication (Bluetooth, mock, network, etc.).
    
    All transport implementations must follow this interface.
    """
    
    async def connect(self, left_addr: str, right_addr: str, 
                     left_key: bytes, right_key: bytes) -> bool:
        """
        Establish connection to both vehicles.
        
        Args:
            left_addr: Address of left wheel (MAC, IP, etc.)
            right_addr: Address of right wheel
            left_key: Encryption key for left wheel
            right_key: Encryption key for right wheel
        
        Returns:
            True if both wheels connected successfully
        """
        ...
    
    async def disconnect(self) -> None:
        """
        Close connections to vehicles.
        
        Should send stop command before disconnecting.
        """
        ...
    
    async def send_command(self, frame: CommandFrame) -> bool:
        """
        Send command frame to vehicles.
        
        Args:
            frame: Command to send (speeds for left/right wheels)
        
        Returns:
            True if command sent successfully to both wheels
        """
        ...
    
    async def read_state(self) -> Optional[VehicleState]:
        """
        Read current vehicle state (battery, speed, errors).
        
        This should be non-blocking and return cached state.
        May return None if no state available yet.
        
        Returns:
            Current vehicle state, or None if unavailable
        """
        ...
    
    @property
    def is_connected(self) -> bool:
        """
        Check if transport is connected.
        
        Returns:
            True if connected to both wheels
        """
        ...
