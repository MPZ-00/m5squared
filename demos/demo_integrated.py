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
    logger.info("Running m25_parking.run_remote_test() in DRY-RUN mode")
    logger.info("(This uses the original author's code)")
    logger.info("")
    
    try:
        import os
        from pathlib import Path
        from m25_parking import run_remote_test
        from m25_spp import BluetoothConnection
        from m25_utils import parse_key
        
        # Try to load .env file if it exists
        try:
            from dotenv import load_dotenv
            env_path = Path(__file__).parent.parent / '.env'
            if env_path.exists():
                load_dotenv(env_path)
                logger.info(f"Loaded environment from {env_path}")
        except ImportError:
            logger.debug("python-dotenv not installed, using environment variables only")
        
        # Try to load from .env or use mock values
        left_addr = os.getenv("M25_LEFT_MAC", "AA:BB:CC:DD:EE:FF")
        right_addr = os.getenv("M25_RIGHT_MAC", "FF:EE:DD:CC:BB:AA")
        left_key_str = os.getenv("M25_LEFT_KEY", "0123456789ABCDEF0123456789ABCDEF")
        right_key_str = os.getenv("M25_RIGHT_KEY", "FEDCBA9876543210FEDCBA9876543210")
        
        logger.info(f"Using addresses: L={left_addr}, R={right_addr}")
        logger.info(f"Using mock keys (dry-run, won't actually connect)")
        logger.info("")
        
        # Parse keys
        left_key = parse_key(left_key_str)
        right_key = parse_key(right_key_str)
        
        # Create connection objects (won't actually connect in dry-run mode)
        left_conn = BluetoothConnection(left_addr, left_key)
        right_conn = BluetoothConnection(right_addr, right_key)
        
        # Run the parking test in dry-run mode
        logger.info("Executing parking sequence...")
        logger.info("")
        run_remote_test(
            left_conn=left_conn,
            right_conn=right_conn,
            speed=80,  # Slow speed
            duration_ms=500,
            dry_run=True  # Won't send actual Bluetooth packets
        )
        
        logger.info("")
        logger.info("=" * 60)
        logger.info("Dry-run completed successfully!")
        logger.info("")
        logger.info("Our core architecture complements this by providing:")
        logger.info("  - Type-safe interfaces")
        logger.info("  - Pluggable input sources")
        logger.info("  - Safety layer (deadman, deadzones, ramping)")
        logger.info("  - State machine supervision")
        
    except Exception as e:
        logger.error(f"Error running parking demo: {e}")
        import traceback
        traceback.print_exc()


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
