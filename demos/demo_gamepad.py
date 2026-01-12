"""
Gamepad Control Demo

Test wheelchair control with a USB/wireless game controller.
"""

import asyncio
import logging
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from core.types import MapperConfig, SupervisorConfig
from core.supervisor import Supervisor
from core.mapper import Mapper
from core.transport import MockTransport

try:
    from input.gamepad_input import GamepadInput
except ImportError:
    print("ERROR: pygame not installed")
    print("Install with: pip install pygame")
    sys.exit(1)


# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


async def main():
    """Run gamepad control demo"""
    logger.info("=" * 70)
    logger.info("M5SQUARED - GAMEPAD CONTROL DEMO")
    logger.info("=" * 70)
    logger.info("")
    logger.info("This demo shows gamepad control with safety features:")
    logger.info("  - Left stick: Movement control")
    logger.info("  - A button: Deadman switch (hold to enable)")
    logger.info("  - D-pad up/down: Change drive mode (SLOW/NORMAL/FAST)")
    logger.info("")
    logger.info("Safety features active:")
    logger.info("  - Deadman switch required")
    logger.info("  - Deadzones applied to stick")
    logger.info("  - Speed ramping")
    logger.info("  - Mock transport (no actual hardware control)")
    logger.info("")
    logger.info("Press Ctrl+C to exit")
    logger.info("=" * 70)
    logger.info("")
    
    try:
        # Create gamepad input (use first found)
        input_provider = GamepadInput(
            deadzone=0.15,  # Ignore small stick movements
            invert_y=False
        )
        
        # Create mapper with safety settings
        mapper = Mapper(MapperConfig(
            deadzone=0.05,  # Additional safety deadzone
            max_speed_normal=60,
            curve=2.0,
            ramp_rate=50.0
        ))
        
        # Create mock transport
        transport = MockTransport()
        
        # Create supervisor
        supervisor = Supervisor(
            input_provider=input_provider,
            mapper=mapper,
            transport=transport,
            config=SupervisorConfig()
        )
        
        # Run until interrupted
        await supervisor.run()
        
    except KeyboardInterrupt:
        logger.info("")
        logger.info("Interrupted by user")
    
    except RuntimeError as e:
        logger.error(f"Error: {e}")
        return 1
    
    finally:
        logger.info("Shutting down...")
        if 'supervisor' in locals():
            supervisor.stop()
    
    return 0


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
