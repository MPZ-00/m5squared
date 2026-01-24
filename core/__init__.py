"""
m5Squared Core - Clean, typed, testable wheelchair control architecture.

This package contains the core logic for controlling M25 wheelchair wheels:
- Types: Data classes for control state, commands, vehicle state
- Interfaces: Protocols for pluggable components (input, transport)
- Mapper: Transforms control input into safe vehicle commands
- Supervisor: State machine, watchdogs, safety orchestration
"""

from .types import (
    ControlState,
    CommandFrame,
    VehicleState,
    DriveMode,
    SupervisorState,
)
from .interfaces import (
    InputProvider,
    Transport,
)

__all__ = [
    "ControlState",
    "CommandFrame",
    "VehicleState",
    "DriveMode",
    "SupervisorState",
    "InputProvider",
    "Transport",
]
