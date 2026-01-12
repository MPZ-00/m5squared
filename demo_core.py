#!/usr/bin/env python3
"""
M5Squared Core Demo - Simple example application.

Demonstrates the new core architecture with mock components.
"""

import asyncio
import logging
import sys

from core.types import MapperConfig, SupervisorConfig, SupervisorState
from core.mapper import Mapper
from core.supervisor import Supervisor
from core.transport import MockTransport
from input import MockInput, TestScripts


# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    stream=sys.stdout
)

logger = logging.getLogger(__name__)


async def run_demo():
    """Run a simple demo with mock components"""
    
    logger.info("=" * 60)
    logger.info("M5Squared Core Architecture Demo")
    logger.info("=" * 60)
    
    # Create components
    logger.info("Creating components...")
    
    # Mock input with scripted test
    input_provider = MockInput(states=TestScripts.forward_drive())
    
    # Mock transport (no actual hardware needed)
    transport = MockTransport(simulate_errors=False, connection_delay=0.1)
    
    # Mapper with default config
    mapper_config = MapperConfig(
        deadzone=0.1,
        curve=2.0,
        max_speed_slow=30,
        max_speed_normal=60,
        max_speed_fast=100,
        ramp_rate=50.0
    )
    mapper = Mapper(mapper_config)
    
    # Supervisor with default config
    supervisor_config = SupervisorConfig(
        loop_interval=0.1,  # 10 Hz for demo
        input_timeout=1.0,
        link_timeout=2.0,
        heartbeat_interval=0.5
    )
    supervisor = Supervisor(
        input_provider=input_provider,
        mapper=mapper,
        transport=transport,
        config=supervisor_config
    )
    
    # Add state change callback for monitoring
    def on_state_change(old_state: SupervisorState, new_state: SupervisorState):
        logger.info(f"🔄 STATE CHANGE: {old_state.value} -> {new_state.value}")
    
    supervisor.add_state_callback(on_state_change)
    
    # Start supervisor in background
    logger.info("Starting supervisor...")
    supervisor_task = asyncio.create_task(supervisor.run())
    
    # Give it a moment to start
    await asyncio.sleep(0.2)
    
    # Request connection
    logger.info("Requesting connection to mock vehicles...")
    supervisor.request_connect(
        left_addr="MOCK:LEFT",
        right_addr="MOCK:RIGHT",
        left_key=b"mock_key_left_16",
        right_key=b"mock_key_rght_16"
    )
    
    # Wait for connection
    await asyncio.sleep(0.5)
    
    # Arm the system
    logger.info("Arming system...")
    supervisor.request_arm()
    
    # Let the scripted input play out
    logger.info("Running scripted input (forward drive test)...")
    logger.info("Watch the command log below:")
    logger.info("-" * 60)
    
    # Run for a few seconds
    for i in range(30):
        await asyncio.sleep(0.2)
        
        # Print status every second
        if i % 5 == 0:
            state = supervisor.state
            vehicle = supervisor.vehicle_state
            
            if vehicle:
                logger.info(
                    f"📊 Status: {state.value} | "
                    f"Battery: {vehicle.battery_min}% | "
                    f"Speed: {vehicle.speed_kmh:.1f} km/h"
                )
    
    logger.info("-" * 60)
    logger.info("Demo complete. Shutting down...")
    
    # Request disconnect
    supervisor.request_disconnect()
    await asyncio.sleep(0.5)
    
    # Stop supervisor
    supervisor.stop()
    await supervisor_task
    
    logger.info("=" * 60)
    logger.info("Demo finished successfully!")
    logger.info("=" * 60)


def main():
    """Main entry point"""
    try:
        asyncio.run(run_demo())
    except KeyboardInterrupt:
        logger.info("\nDemo interrupted by user")
    except Exception as e:
        logger.error(f"Demo failed: {e}", exc_info=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
