# M5Squared Core Architecture

**Clean, typed, testable wheelchair control system.**

## Overview

The core module provides a safety-first, pluggable architecture for controlling M25 wheelchair wheels. It separates concerns cleanly and uses type hints throughout for maintainability.

## Architecture

```
InputProvider → ControlState → Mapper → CommandFrame → Transport → Vehicle
                                  ↑                         ↓
                            Supervisor ←── VehicleState ─────┘
                          (State Machine
                           & Watchdogs)
```

## Core Components

### 1. Types (`core/types.py`)

**Data classes that flow through the system:**

- **`ControlState`**: Normalized input from any source
  - `vx`, `vy`: Joystick axes (-1.0 to 1.0)
  - `deadman`: Safety switch
  - `mode`: Drive mode (STOP, SLOW, NORMAL, FAST)

- **`CommandFrame`**: Commands to send to vehicles
  - `left_speed`, `right_speed`: Wheel speeds (-100 to 100)
  - `flags`: Additional control flags

- **`VehicleState`**: Current vehicle status
  - Battery levels, speed, errors, connection status

- **`SupervisorState`**: State machine states
  - DISCONNECTED → CONNECTING → PAIRED → ARMED → DRIVING → FAILSAFE

### 2. Interfaces (`core/interfaces.py`)

**Protocols (interfaces) for pluggable components:**

- **`InputProvider`**: Any input source (joystick, GUI, keyboard, test)
  - `start()`, `stop()`, `read_control_state()`

- **`Transport`**: Any communication method (Bluetooth, mock, network)
  - `connect()`, `disconnect()`, `send_command()`, `read_state()`

### 3. Mapper (`core/mapper.py`)

**Transforms input into safe commands:**

- ✅ Deadman switch enforcement
- ✅ Deadzone filtering (prevents drift)
- ✅ Response curves (smoother control)
- ✅ Mode-specific speed limits
- ✅ Ramping (prevents sudden changes)
- ✅ Differential drive kinematics

**This is safety-critical code - extensively tested.**

### 4. Supervisor (`core/supervisor.py`)

**Main control loop and state machine:**

**States:**
1. **DISCONNECTED**: Idle, waiting for connection
2. **CONNECTING**: Attempting to connect
3. **PAIRED**: Connected but not armed
4. **ARMED**: Ready to drive
5. **DRIVING**: Actively controlling
6. **FAILSAFE**: Emergency state

**Watchdogs:**
- **Input watchdog**: No input → FAILSAFE
- **Link watchdog**: Lost connection → FAILSAFE  
- **Heartbeat**: Periodic keep-alive

**Safety Rules:**
- Deadman must be pressed in DRIVING
- Any error triggers FAILSAFE
- All state transitions logged

## Usage

### Basic Example

```python
import asyncio
from core import Supervisor, Mapper
from core.types import MapperConfig, SupervisorConfig
from core.transport import MockTransport
from input import MockInput

async def main():
    # Create components
    input_provider = MockInput()
    transport = MockTransport()
    mapper = Mapper(MapperConfig())
    supervisor = Supervisor(
        input_provider=input_provider,
        mapper=mapper,
        transport=transport,
        config=SupervisorConfig()
    )
    
    # Run supervisor
    await supervisor.run()

asyncio.run(main())
```

### Running the Demo

```bash
# Run the included demo with mock components
python demo_core.py
```

This demonstrates:
- State machine transitions
- Scripted input processing
- Command generation
- Safety features

## Testing

### Run Tests

```bash
# Run all tests
pytest

# Run with coverage
pytest --cov=core --cov-report=html

# Run specific test file
pytest tests/test_mapper.py -v
```

### Type Checking

```bash
# Check types
mypy core/
```

## Configuration

### Mapper Configuration

```python
from core.types import MapperConfig

config = MapperConfig(
    deadzone=0.1,           # Ignore inputs below 10%
    curve=2.0,              # Exponential curve for smooth control
    max_speed_slow=30,      # 30% max in SLOW mode
    max_speed_normal=60,    # 60% max in NORMAL mode
    max_speed_fast=100,     # 100% max in FAST mode
    ramp_rate=50.0          # Max 50 units/sec change
)
```

### Supervisor Configuration

```python
from core.types import SupervisorConfig

config = SupervisorConfig(
    loop_interval=0.05,         # 20 Hz control loop
    input_timeout=0.5,          # Failsafe if no input for 500ms
    link_timeout=2.0,           # Failsafe if no link for 2s
    heartbeat_interval=1.0,     # Send heartbeat every 1s
    reconnect_delay=2.0,        # Wait 2s between reconnect attempts
    max_reconnect_attempts=3    # Give up after 3 attempts
)
```

## Safety Features

### ⚠️ Safety-Critical Components

The following components have **extra scrutiny**:

1. **Mapper** - All safety rules implemented here
   - Deadman enforcement
   - Speed limiting
   - Ramping
   - Full test coverage required

2. **Supervisor Watchdogs** - Detect failures
   - Input timeout detection
   - Link timeout detection
   - Tested with edge cases

3. **State Machine** - Correct transitions
   - All states properly handled
   - Failsafe on any error
   - Logged for debugging

### Testing Safety Features

```python
# Test deadman requirement
state = ControlState(vx=0.5, vy=0.0, deadman=False, mode=DriveMode.NORMAL)
frame = mapper.map(state)
assert frame.is_stop  # Must produce stop command

# Test speed limits
state = ControlState(vx=1.0, vy=0.0, deadman=True, mode=DriveMode.SLOW)
frame = mapper.map(state)
assert abs(frame.left_speed) <= 30  # Enforces SLOW limit
```

## Extending

### Create a New Input Provider

```python
from core.interfaces import InputProvider
from core.types import ControlState

class MyInput:
    async def start(self) -> None:
        # Initialize your input device
        pass
    
    async def stop(self) -> None:
        # Cleanup
        pass
    
    async def read_control_state(self) -> Optional[ControlState]:
        # Read from your device
        # Return ControlState with normalized values
        return ControlState(...)
```

### Create a New Transport

```python
from core.interfaces import Transport
from core.types import CommandFrame, VehicleState

class MyTransport:
    async def connect(self, left_addr, right_addr, 
                     left_key, right_key) -> bool:
        # Establish connection
        return True
    
    async def send_command(self, frame: CommandFrame) -> bool:
        # Send command to vehicles
        return True
    
    # ... implement other methods
```

## Project Structure

```
core/
├── __init__.py          # Public API exports
├── types.py             # Data classes and enums
├── interfaces.py        # Protocols for pluggable components
├── mapper.py            # Input → Command transformation
├── supervisor.py        # State machine and control loop
└── transport/
    └── __init__.py      # MockTransport

input/
├── __init__.py          # Input providers
└── mock_input.py        # Mock/test input

tests/
├── __init__.py
├── test_types.py        # Type validation tests
└── test_mapper.py       # Mapper safety tests
```

## Next Steps

See [REFACTOR_ROADMAP.md](../REFACTOR_ROADMAP.md) for the full implementation plan:

- ✅ Phase 1: Core Types & Interfaces (COMPLETE)
- ⏭️ Phase 2: Transport Layer (wrap existing Bluetooth)
- ⏭️ Phase 3: Physical Input Providers
- ⏭️ Phase 4: GUI Integration
- ⏭️ Phase 5: Hardware Testing

## License

See [LICENSE](../LICENSE) for details.
