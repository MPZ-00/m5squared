#!/usr/bin/env python3
"""
m5squared Launcher - Easy start for wheelchair control

Usage:
    python launch.py              # Start GUI
    python launch.py --gamepad    # Start with gamepad control
    python launch.py --mock       # Start with mock transport (testing)
    python launch.py --demo       # Run core demo
"""

import sys
import argparse
import asyncio
import logging
from pathlib import Path


def setup_logging(level: str = "INFO") -> None:
    """Configure logging"""
    logging.basicConfig(
        level=getattr(logging, level.upper()),
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        stream=sys.stdout
    )


def launch_gui() -> None:
    """Launch the GUI application"""
    print("Starting m5squared GUI...")
    from m25_gui import main
    main()


def launch_gamepad(use_mock: bool = False) -> None:
    """Launch gamepad control mode with core architecture"""
    print("Starting gamepad control mode...")
    print("Hold A button as deadman switch, use left stick to drive")
    
    try:
        import pygame  # noqa: F401
    except ImportError:
        print("\nERROR: pygame not installed")
        print("Install with: pip install pygame")
        sys.exit(1)
    
    from core.types import MapperConfig, SupervisorConfig
    from core.mapper import Mapper
    from core.supervisor import Supervisor
    from input.gamepad_input import GamepadInput
    
    if use_mock:
        from core.transport import MockTransport
        print("Using MOCK transport (no actual hardware)")
        transport = MockTransport(simulate_errors=False)
    else:
        # TODO: Import real Bluetooth transport when ready
        print("ERROR: Real Bluetooth transport not yet integrated")
        print("Use --mock flag for testing")
        sys.exit(1)
    
    # Create components
    input_provider = GamepadInput(deadzone=0.1)
    
    mapper_config = MapperConfig(
        deadzone=0.1,
        curve=2.0,
        max_speed_normal=100,
        ramp_rate=50.0
    )
    mapper = Mapper(mapper_config)
    
    supervisor_config = SupervisorConfig(
        input_timeout=0.5,
        link_timeout=2.0,
        heartbeat_interval=0.5
    )
    supervisor = Supervisor(
        input_provider=input_provider,
        mapper=mapper,
        transport=transport,
        config=supervisor_config
    )
    
    # Run async main loop
    async def run():
        try:
            # Start supervisor
            supervisor_task = asyncio.create_task(supervisor.run())
            await asyncio.sleep(0.2)
            
            # Connect (using mock addresses for now)
            print("Connecting to vehicles...")
            supervisor.request_connect(
                left_addr="MOCK:LEFT" if use_mock else None,
                right_addr="MOCK:RIGHT" if use_mock else None,
                left_key=b"mock_key_left_16",
                right_key=b"mock_key_rght_16"
            )
            await asyncio.sleep(1.0)
            
            # Arm system
            print("System ready - press A button to enable driving")
            supervisor.request_arm()
            
            # Wait for Ctrl+C
            await supervisor_task
            
        except KeyboardInterrupt:
            print("\nShutting down...")
            supervisor.request_disconnect()
            await asyncio.sleep(0.5)
            supervisor.stop()
    
    try:
        asyncio.run(run())
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


def launch_demo() -> None:
    """Launch core architecture demo"""
    print("Starting core architecture demo...")
    from demos.demo_core import main
    main()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description="m5squared - Wheelchair Control System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python launch.py              Start GUI
  python launch.py --gamepad --mock    Test gamepad with mock transport
  python launch.py --demo       Run core demo
        """
    )
    
    parser.add_argument(
        "--gamepad",
        action="store_true",
        help="Use gamepad control (requires pygame)"
    )
    parser.add_argument(
        "--mock",
        action="store_true",
        help="Use mock transport for testing (no hardware needed)"
    )
    parser.add_argument(
        "--demo",
        action="store_true",
        help="Run core architecture demo"
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Set logging level"
    )
    
    args = parser.parse_args()
    
    # Setup logging
    setup_logging(args.log_level)
    
    # Route to appropriate launcher
    if args.demo:
        launch_demo()
    elif args.gamepad:
        launch_gamepad(use_mock=args.mock)
    else:
        # Default to GUI
        launch_gui()


if __name__ == "__main__":
    main()
