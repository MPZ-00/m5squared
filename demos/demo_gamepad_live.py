"""
Live Gamepad Drive Test

Connect to actual wheelchair wheels and drive with gamepad.
Includes a timed forward drive test and dry-run mode.
"""

import asyncio
import logging
import sys
import argparse
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from core.types import MapperConfig, SupervisorConfig, ControlState, DriveMode
from core.supervisor import Supervisor
from core.mapper import Mapper
from core.transport import BluetoothTransport, MockTransport

try:
    from input.gamepad_input import GamepadInput
except ImportError:
    print("ERROR: pygame not installed")
    print("Install with: pip install pygame")
    sys.exit(1)

try:
    from dotenv import load_dotenv
    import os
except ImportError:
    print("ERROR: python-dotenv not installed")
    print("Install with: pip install python-dotenv")
    sys.exit(1)


# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class TimedForwardInput:
    """Input provider that drives forward for a specified duration"""
    
    def __init__(self, duration: float = 2.0, speed: float = 0.3):
        """
        Args:
            duration: How long to drive forward (seconds)
            speed: Forward speed (0.0 to 1.0)
        """
        self._duration = duration
        self._speed = speed
        self._start_time = None
        self._running = False
    
    async def start(self) -> None:
        """Start input provider"""
        logger.info(f"Timed forward drive: {self._duration}s at {self._speed*100:.0f}% speed")
        self._running = True
        self._start_time = asyncio.get_event_loop().time()
    
    async def stop(self) -> None:
        """Stop input provider"""
        self._running = False
    
    async def read_control_state(self) -> ControlState:
        """Return forward drive command for duration, then stop"""
        if not self._running:
            return ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.STOP)
        
        elapsed = asyncio.get_event_loop().time() - self._start_time
        
        if elapsed < self._duration:
            # Drive forward
            return ControlState(
                vx=self._speed,  # Forward
                vy=0.0,          # No turning
                deadman=True,    # Deadman active
                mode=DriveMode.SLOW  # Use slow mode for safety
            )
        else:
            # Time's up - stop
            logger.info("Timed drive complete - stopping")
            return ControlState(vx=0.0, vy=0.0, deadman=False, mode=DriveMode.STOP)


async def timed_drive_test(duration: float, dry_run: bool):
    """Run a timed forward drive test"""
    logger.info("=" * 70)
    logger.info("M5SQUARED - TIMED DRIVE TEST")
    logger.info("=" * 70)
    logger.info("")
    logger.info(f"Test: Drive forward for {duration} seconds")
    logger.info(f"Mode: {'DRY-RUN (Mock Transport)' if dry_run else 'LIVE (Bluetooth)'}")
    logger.info("")
    
    if not dry_run:
        logger.info("⚠️  WARNING: This will ACTUALLY MOVE the wheelchair!")
        logger.info("⚠️  Make sure the wheels are OFF THE GROUND or have clearance!")
        logger.info("")
        logger.info("Press Ctrl+C now to cancel...")
        await asyncio.sleep(3)
    
    logger.info("=" * 70)
    logger.info("")
    
    # Load environment variables
    load_dotenv()
    left_mac = os.getenv('M25_LEFT_MAC')
    right_mac = os.getenv('M25_RIGHT_MAC')
    left_key = bytes.fromhex(os.getenv('M25_LEFT_KEY', ''))
    right_key = bytes.fromhex(os.getenv('M25_RIGHT_KEY', ''))
    
    if not dry_run and (not left_mac or not right_mac or not left_key or not right_key):
        logger.error("Missing configuration in .env file")
        logger.error("Required: M25_LEFT_MAC, M25_RIGHT_MAC, M25_LEFT_KEY, M25_RIGHT_KEY")
        return 1
    
    try:
        # Create timed input
        input_provider = TimedForwardInput(duration=duration, speed=0.3)
        
        # Create mapper with conservative settings
        mapper = Mapper(MapperConfig(
            deadzone=0.05,
            max_speed_slow=30,   # Only 30% max in slow mode
            max_speed_normal=60,
            curve=2.0,
            ramp_rate=50.0
        ))
        
        # Create transport
        if dry_run:
            transport = MockTransport()
        else:
            transport = BluetoothTransport()
        
        # Create supervisor
        supervisor = Supervisor(
            input_provider=input_provider,
            mapper=mapper,
            transport=transport,
            config=SupervisorConfig()
        )
        
        # Start supervisor
        supervisor_task = asyncio.create_task(supervisor.run())
        await asyncio.sleep(0.5)
        
        # Connect
        logger.info("Connecting to vehicles...")
        if dry_run:
            supervisor.request_connect("mock_left", "mock_right", b"key", b"key")
        else:
            supervisor.request_connect(left_mac, right_mac, left_key, right_key)
        
        await asyncio.sleep(2.0)  # Wait for connection
        
        # Arm
        logger.info("Arming system...")
        supervisor.request_arm()
        await asyncio.sleep(0.5)
        
        # Wait for timed drive to complete (duration + buffer)
        logger.info("Starting timed drive...")
        await asyncio.sleep(duration + 2.0)
        
        # Stop
        logger.info("Test complete - shutting down")
        supervisor.stop()
        
        try:
            await asyncio.wait_for(supervisor_task, timeout=5.0)
        except asyncio.TimeoutError:
            logger.warning("Supervisor didn't stop cleanly")
        
        logger.info("✓ Test completed successfully")
        return 0
        
    except KeyboardInterrupt:
        logger.info("")
        logger.info("Interrupted by user")
        if 'supervisor' in locals():
            supervisor.stop()
        return 1
    
    except Exception as e:
        logger.error(f"Error: {e}", exc_info=True)
        return 1


async def gamepad_drive(dry_run: bool):
    """Drive with gamepad control"""
    logger.info("=" * 70)
    logger.info("M5SQUARED - LIVE GAMEPAD CONTROL")
    logger.info("=" * 70)
    logger.info("")
    logger.info(f"Mode: {'DRY-RUN (Mock Transport)' if dry_run else 'LIVE (Bluetooth)'}")
    logger.info("")
    logger.info("Controls:")
    logger.info("  - Left stick: Movement (forward/back, left/right)")
    logger.info("  - A button (button 0): Deadman switch (HOLD to enable)")
    logger.info("  - Press Ctrl+C to exit")
    logger.info("")
    
    if not dry_run:
        logger.info("⚠️  WARNING: This will ACTUALLY MOVE the wheelchair!")
        logger.info("⚠️  Make sure you have a clear area!")
        logger.info("")
        logger.info("Press Ctrl+C now to cancel...")
        await asyncio.sleep(3)
    
    logger.info("=" * 70)
    logger.info("")
    
    # Load environment variables
    load_dotenv()
    left_mac = os.getenv('M25_LEFT_MAC')
    right_mac = os.getenv('M25_RIGHT_MAC')
    left_key = bytes.fromhex(os.getenv('M25_LEFT_KEY', ''))
    right_key = bytes.fromhex(os.getenv('M25_RIGHT_KEY', ''))
    
    if not dry_run and (not left_mac or not right_mac or not left_key or not right_key):
        logger.error("Missing configuration in .env file")
        logger.error("Required: M25_LEFT_MAC, M25_RIGHT_MAC, M25_LEFT_KEY, M25_RIGHT_KEY")
        return 1
    
    try:
        # Create gamepad input
        input_provider = GamepadInput(
            deadzone=0.15,
            invert_y=False
        )
        
        # Create mapper
        mapper = Mapper(MapperConfig(
            deadzone=0.05,
            max_speed_slow=30,
            max_speed_normal=60,
            max_speed_fast=100,
            curve=2.0,
            ramp_rate=50.0
        ))
        
        # Create transport
        if dry_run:
            transport = MockTransport()
        else:
            transport = BluetoothTransport()
        
        # Create supervisor
        supervisor = Supervisor(
            input_provider=input_provider,
            mapper=mapper,
            transport=transport,
            config=SupervisorConfig()
        )
        
        # Start supervisor
        supervisor_task = asyncio.create_task(supervisor.run())
        await asyncio.sleep(0.5)
        
        # Connect
        logger.info("Connecting to vehicles...")
        if dry_run:
            supervisor.request_connect("mock_left", "mock_right", b"key", b"key")
        else:
            supervisor.request_connect(left_mac, right_mac, left_key, right_key)
        
        await asyncio.sleep(2.0)
        
        # Arm
        logger.info("Arming system - ready to drive!")
        logger.info("HOLD button 0 (A) to enable movement")
        supervisor.request_arm()
        
        # Wait for user to stop
        await supervisor_task
        
        return 0
        
    except KeyboardInterrupt:
        logger.info("")
        logger.info("Interrupted by user")
        if 'supervisor' in locals():
            supervisor.stop()
        return 0
    
    except Exception as e:
        logger.error(f"Error: {e}", exc_info=True)
        return 1


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='M5Squared Live Drive Test',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Timed forward drive test (dry-run)
  python %(prog)s --test --duration 2 --dry-run
  
  # Timed forward drive test (LIVE)
  python %(prog)s --test --duration 2
  
  # Manual gamepad control (dry-run)
  python %(prog)s --dry-run
  
  # Manual gamepad control (LIVE)
  python %(prog)s
"""
    )
    
    parser.add_argument(
        '--test',
        action='store_true',
        help='Run timed forward drive test'
    )
    
    parser.add_argument(
        '--duration',
        type=float,
        default=2.0,
        help='Duration for timed test in seconds (default: 2.0)'
    )
    
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Use mock transport (no actual Bluetooth connection)'
    )
    
    args = parser.parse_args()
    
    if args.test:
        exit_code = asyncio.run(timed_drive_test(args.duration, args.dry_run))
    else:
        exit_code = asyncio.run(gamepad_drive(args.dry_run))
    
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
