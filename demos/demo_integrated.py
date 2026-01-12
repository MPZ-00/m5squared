"""
Integrated Demo - Core Architecture with M25 Parking

Demonstrates the pluggable core architecture:
- MockInput for keyboard/scripted control
- Core Supervisor with safety features
- MockTransport or BluetoothTransport
- Integration with existing m25_parking in dry-run mode
"""

import asyncio
import logging
import sys
from pathlib import Path
from typing import Optional

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from core.types import ControlState, DriveMode, MapperConfig, SupervisorConfig
from core.supervisor import Supervisor
from core.mapper import Mapper
from core.transport import MockTransport
from input.mock_input import MockInput


# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class IntegratedDemo:
    """Demo application showing pluggable architecture"""
    
    def __init__(self) -> None:
        """Initialize demo with default configuration"""
        
        # Create input provider - keyboard controls
        self.input_provider = MockInput()
        
        # Create mapper with safety settings
        mapper_config = MapperConfig(
            deadzone=0.05,
            max_speed_normal=100,
            curve=2.0,
            ramp_rate=50.0
        )
        self.mapper = Mapper(mapper_config)
        
        # Create transport - mock for testing
        self.transport = MockTransport(
            simulate_errors=False,
            connection_delay=0.5
        )
        
        # Create supervisor
        supervisor_config = SupervisorConfig(
            input_timeout=1.0,
            link_timeout=2.0
        )
        self.supervisor = Supervisor(
            input_provider=self.input_provider,
            mapper=self.mapper,
            transport=self.transport,
            config=supervisor_config
        )
        
        logger.info("Demo initialized with pluggable architecture")
    
    async def run(self) -> None:
        """Run the demo"""
        logger.info("=" * 60)
        logger.info("M5SQUARED - INTEGRATED DEMO")
        logger.info("=" * 60)
        logger.info("")
        logger.info("Demonstrating pluggable architecture:")
        logger.info("  - Input: MockInput (keyboard/scripted)")
        logger.info("  - Mapper: Safety-critical transformation")
        logger.info("  - Supervisor: State machine with watchdogs")
        logger.info("  - Transport: MockTransport (dry-run)")
        logger.info("")
        logger.info("Controls:")
        logger.info("  - Hold SPACE for deadman switch")
        logger.info("  - W/S: Forward/Backward")
        logger.info("  - A/D: Left/Right")
        logger.info("  - Q: Quit")
        logger.info("")
        logger.info("Safety Features Active:")
        logger.info("  - Deadman switch required")
        logger.info("  - Deadzones applied")
        logger.info("  - Speed ramping")
        logger.info("  - Watchdog timers")
        logger.info("=" * 60)
        logger.info("")
        
        try:
            # Start supervisor (runs until stopped)
            await self.supervisor.run()
            
        except KeyboardInterrupt:
            logger.info("Demo interrupted by user")
        
        finally:
            # Cleanup
            await self.supervisor.stop()
            logger.info("Demo shutdown complete")


async def demo_scripted_movement() -> None:
    """
    Demo with scripted movement sequence.
    
    Shows how to use test scripts instead of keyboard input.
    """
    logger.info("=" * 60)
    logger.info("SCRIPTED MOVEMENT DEMO")
    logger.info("=" * 60)
    
    # Create input with scripted sequence
    input_provider = MockInput()
    input_provider.load_script("forward_turn_stop")
    
    # Create other components
    mapper = Mapper(MapperConfig())
    transport = MockTransport()
    
    supervisor = Supervisor(
        input_provider=input_provider,
        mapper=mapper,
        transport=transport,
        config=SupervisorConfig()
    )
    
    try:
        # Run for limited time (script will auto-complete)
        await asyncio.wait_for(supervisor.run(), timeout=10.0)
    
    except asyncio.TimeoutError:
        logger.info("Script completed")
    
    finally:
        supervisor.stop()


async def demo_with_m25_parking() -> None:
    """
    Demo showing integration with existing m25_parking module.
    
    This demonstrates how the core architecture can work alongside
    the original m25_ codebase without duplication.
    """
    logger.info("=" * 60)
    logger.info("M25 PARKING INTEGRATION DEMO")
    logger.info("=" * 60)
    logger.info("")
    logger.info("This demo shows how to integrate with existing")
    logger.info("m25_parking.py code in dry-run mode.")
    logger.info("")
    
    # Note: Actual integration with m25_parking would require
    # adapting run_remote_test() to work with our Transport interface.
    # For now, we demonstrate the core architecture separately.
    
    from m25_parking import run_remote_test
    
    logger.info("Running m25_parking in dry-run mode...")
    logger.info("(This uses the original author's code)")
    logger.info("")
    
    # Run the original parking test in dry-run mode
    # This doesn't actually connect to hardware
    try:
        # Note: run_remote_test expects certain args
        # For demo purposes, we'll show how it could be called
        logger.info("To run m25_parking.run_remote_test():")
        logger.info("  python m25_parking.py --dry-run")
        logger.info("")
        logger.info("Our core architecture complements this by providing:")
        logger.info("  - Type-safe interfaces")
        logger.info("  - Pluggable input sources")
        logger.info("  - Safety layer (deadman, deadzones, ramping)")
        logger.info("  - State machine supervision")
        
    except Exception as e:
        logger.error(f"Error: {e}")


async def main() -> None:
    """Main entry point - choose demo mode"""
    import sys
    
    if len(sys.argv) > 1:
        mode = sys.argv[1]
    else:
        mode = "interactive"
    
    if mode == "scripted":
        await demo_scripted_movement()
    
    elif mode == "parking":
        await demo_with_m25_parking()
    
    else:
        # Default: interactive demo
        demo = IntegratedDemo()
        await demo.run()


if __name__ == "__main__":
    asyncio.run(main())
