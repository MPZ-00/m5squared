"""
Supervisor - State machine, watchdogs, and safety orchestration.

The Supervisor is the main control loop. It:
- Manages state transitions (disconnected -> connecting -> driving -> etc.)
- Runs watchdogs (input timeout, link timeout, heartbeat)
- Coordinates InputProvider, Mapper, and Transport
- Ensures safety at all times

This is safety-critical code.
"""

import asyncio
import logging
import time
from typing import Optional, Callable, Any
from .types import (
    ControlState,
    CommandFrame,
    VehicleState,
    SupervisorState,
    SupervisorConfig,
)
from .interfaces import InputProvider, Transport
from .mapper import Mapper


logger = logging.getLogger(__name__)


class Supervisor:
    """
    Main control loop and safety supervisor.
    
    Orchestrates all components and enforces safety rules through
    state machine and watchdog timers.
    """
    
    def __init__(
        self,
        input_provider: InputProvider,
        mapper: Mapper,
        transport: Transport,
        config: SupervisorConfig,
    ) -> None:
        """
        Initialize supervisor.
        
        Args:
            input_provider: Source of control input
            mapper: Converts input to commands
            transport: Sends commands to vehicles
            config: Supervisor configuration
        """
        self.input = input_provider
        self.mapper = mapper
        self.transport = transport
        self.config = config
        
        self.state = SupervisorState.DISCONNECTED
        self._running = False
        self._stop_requested = False
        
        # Watchdog timers
        self._last_input_time: float = 0.0
        self._last_link_time: float = 0.0
        self._last_heartbeat_time: float = 0.0
        
        # Connection management
        self._reconnect_attempts = 0
        
        # Vehicle state cache
        self._vehicle_state: Optional[VehicleState] = None
        
        # State change callbacks
        self._state_callbacks: list[Callable[[SupervisorState, SupervisorState], Any]] = []
    
    def add_state_callback(self, callback: Callable[[SupervisorState, SupervisorState], Any]) -> None:
        """
        Register callback for state changes.
        
        Callback signature: callback(old_state, new_state)
        
        Args:
            callback: Function to call on state change
        """
        self._state_callbacks.append(callback)
    
    def request_connect(self, left_addr: str, right_addr: str,
                       left_key: bytes, right_key: bytes) -> None:
        """
        Request connection to vehicles.
        
        Args:
            left_addr: Left wheel address
            right_addr: Right wheel address
            left_key: Left wheel encryption key
            right_key: Right wheel encryption key
        """
        self._left_addr = left_addr
        self._right_addr = right_addr
        self._left_key = left_key
        self._right_key = right_key
        
        if self.state == SupervisorState.DISCONNECTED:
            self._transition_to(SupervisorState.CONNECTING)
    
    def request_disconnect(self) -> None:
        """Request disconnect from vehicles"""
        if self.state != SupervisorState.DISCONNECTED:
            # Will be handled in next update cycle
            self._stop_requested = True
    
    def request_arm(self) -> None:
        """Request transition to ARMED state (ready to drive)"""
        if self.state == SupervisorState.PAIRED:
            self._transition_to(SupervisorState.ARMED)
    
    async def run(self) -> None:
        """
        Main control loop - runs until stopped.
        
        This is the heart of the system. Call this from an async context.
        """
        logger.info("Supervisor starting")
        self._running = True
        
        try:
            # Start input provider
            await self.input.start()
            
            # Main loop
            while self._running:
                try:
                    await self._update()
                    await asyncio.sleep(self.config.loop_interval)
                except Exception as e:
                    logger.error(f"Error in supervisor update: {e}", exc_info=True)
                    await self._enter_failsafe(f"Update error: {e}")
        
        finally:
            # Cleanup
            logger.info("Supervisor stopping")
            await self._cleanup()
    
    def stop(self) -> None:
        """Stop the supervisor (call from outside async context)"""
        self._running = False
    
    async def _update(self) -> None:
        """Single iteration of control loop"""
        
        # Handle stop request
        if self._stop_requested:
            await self._handle_stop_request()
            return
        
        # State machine
        if self.state == SupervisorState.DISCONNECTED:
            await self._handle_disconnected()
        
        elif self.state == SupervisorState.CONNECTING:
            await self._handle_connecting()
        
        elif self.state == SupervisorState.PAIRED:
            await self._handle_paired()
        
        elif self.state == SupervisorState.ARMED:
            await self._handle_armed()
        
        elif self.state == SupervisorState.DRIVING:
            await self._handle_driving()
        
        elif self.state == SupervisorState.FAILSAFE:
            await self._handle_failsafe()
        
        # Watchdogs (active in ARMED and DRIVING states)
        if self.state in (SupervisorState.ARMED, SupervisorState.DRIVING):
            await self._check_watchdogs()
    
    async def _handle_disconnected(self) -> None:
        """DISCONNECTED state - idle, waiting for connection request"""
        # Nothing to do, wait for request_connect()
        pass
    
    async def _handle_connecting(self) -> None:
        """CONNECTING state - attempt to connect to vehicles"""
        logger.info(f"Connecting to vehicles (attempt {self._reconnect_attempts + 1})")
        
        try:
            success = await self.transport.connect(
                self._left_addr,
                self._right_addr,
                self._left_key,
                self._right_key,
            )
            
            if success:
                logger.info("Connected successfully")
                self._reconnect_attempts = 0
                self._transition_to(SupervisorState.PAIRED)
                self._last_link_time = time.time()
            else:
                logger.warning("Connection failed")
                self._reconnect_attempts += 1
                
                if self._reconnect_attempts >= self.config.max_reconnect_attempts:
                    logger.error("Max reconnection attempts reached")
                    self._transition_to(SupervisorState.DISCONNECTED)
                else:
                    # Wait before retrying
                    await asyncio.sleep(self.config.reconnect_delay)
        
        except Exception as e:
            logger.error(f"Connection error: {e}", exc_info=True)
            self._reconnect_attempts += 1
            
            if self._reconnect_attempts >= self.config.max_reconnect_attempts:
                self._transition_to(SupervisorState.DISCONNECTED)
            else:
                await asyncio.sleep(self.config.reconnect_delay)
    
    async def _handle_paired(self) -> None:
        """PAIRED state - connected but not armed"""
        # Read vehicle state
        vehicle_state = await self.transport.read_state()
        if vehicle_state:
            self._vehicle_state = vehicle_state
        
        # Wait for arm request
        # User must explicitly request transition to ARMED
    
    async def _handle_armed(self) -> None:
        """ARMED state - ready to drive, waiting for input"""
        # Read input
        control = await self.input.read_control_state()
        if control is None:
            return
        
        self._last_input_time = control.timestamp
        
        # Check if user wants to drive (deadman + movement)
        if control.deadman and not control.is_neutral:
            self._transition_to(SupervisorState.DRIVING)
            # Will process this input in next cycle
        else:
            # Send stop command to ensure wheels are stopped
            await self._send_stop()
    
    async def _handle_driving(self) -> None:
        """DRIVING state - actively controlling vehicles"""
        # 1. Read input
        control = await self.input.read_control_state()
        if control is None:
            logger.warning("No input received while driving")
            await self._send_stop()
            return
        
        self._last_input_time = control.timestamp
        
        # 2. Check if user released controls
        if not control.deadman or control.is_neutral:
            logger.info("User released controls, returning to ARMED")
            await self._send_stop()
            self._transition_to(SupervisorState.ARMED)
            return
        
        # 3. Map to command
        command = self.mapper.map(control)
        if command is None:
            # Safety violation
            logger.warning("Mapper rejected input")
            await self._send_stop()
            return
        
        # 4. Send command
        success = await self.transport.send_command(command)
        if not success:
            logger.error("Failed to send command")
            # Don't failsafe immediately, will be caught by watchdog
            return
        
        self._last_link_time = time.time()
        
        # 5. Read vehicle feedback (non-blocking)
        vehicle_state = await self.transport.read_state()
        if vehicle_state:
            self._vehicle_state = vehicle_state
            
            # Check for errors
            if vehicle_state.has_errors:
                logger.error(f"Vehicle errors: {vehicle_state.errors}")
                await self._enter_failsafe(f"Vehicle error: {vehicle_state.errors[0]}")
    
    async def _handle_failsafe(self) -> None:
        """FAILSAFE state - emergency, send stop commands"""
        logger.debug("FAILSAFE: sending stop command")
        await self._send_stop()
        
        # Check if we can recover
        if self.transport.is_connected:
            # Connected but had error - could try to recover
            # For now, stay in failsafe until manual intervention
            pass
        else:
            # Lost connection
            logger.info("Connection lost in failsafe, disconnecting")
            self._transition_to(SupervisorState.DISCONNECTED)
    
    async def _handle_stop_request(self) -> None:
        """Handle user stop request"""
        logger.info("Stop requested")
        await self._send_stop()
        await self.transport.disconnect()
        self._transition_to(SupervisorState.DISCONNECTED)
        self._stop_requested = False
    
    async def _check_watchdogs(self) -> None:
        """Check all watchdog timers"""
        now = time.time()
        
        # Input watchdog - no input for too long
        if self._last_input_time > 0:  # Only check if we've received input before
            if now - self._last_input_time > self.config.input_timeout:
                logger.error("Input watchdog timeout")
                await self._enter_failsafe("Input timeout")
                return
        
        # Link watchdog - no successful command for too long
        if self._last_link_time > 0:
            if now - self._last_link_time > self.config.link_timeout:
                logger.error("Link watchdog timeout")
                await self._enter_failsafe("Link timeout")
                return
        
        # Heartbeat - send periodic command even if no input change
        if now - self._last_heartbeat_time > self.config.heartbeat_interval:
            await self._send_heartbeat()
    
    async def _send_stop(self) -> None:
        """Send stop command to vehicles"""
        stop_cmd = CommandFrame.stop()
        await self.transport.send_command(stop_cmd)
        self._last_link_time = time.time()
    
    async def _send_heartbeat(self) -> None:
        """Send heartbeat (current command or stop)"""
        # Send last known good command, or stop if none
        cmd = self.mapper._last_command if self.mapper._last_command else CommandFrame.stop()
        await self.transport.send_command(cmd)
        self._last_heartbeat_time = time.time()
        self._last_link_time = time.time()
    
    async def _enter_failsafe(self, reason: str) -> None:
        """Enter FAILSAFE state"""
        logger.critical(f"Entering FAILSAFE: {reason}")
        await self._send_stop()
        self._transition_to(SupervisorState.FAILSAFE)
    
    def _transition_to(self, new_state: SupervisorState) -> None:
        """
        Transition to new state.
        
        Args:
            new_state: State to transition to
        """
        if new_state == self.state:
            return
        
        old_state = self.state
        logger.info(f"State transition: {old_state.value} -> {new_state.value}")
        self.state = new_state
        
        # Reset mapper state on certain transitions
        if new_state in (SupervisorState.DISCONNECTED, SupervisorState.FAILSAFE):
            self.mapper.reset()
        
        # Notify callbacks
        for callback in self._state_callbacks:
            try:
                callback(old_state, new_state)
            except Exception as e:
                logger.error(f"Error in state callback: {e}", exc_info=True)
    
    async def _cleanup(self) -> None:
        """Cleanup on shutdown"""
        try:
            # Send final stop
            if self.transport.is_connected:
                await self._send_stop()
                await self.transport.disconnect()
            
            # Stop input provider
            await self.input.stop()
        
        except Exception as e:
            logger.error(f"Error during cleanup: {e}", exc_info=True)
    
    # Public properties for UI/monitoring
    
    @property
    def vehicle_state(self) -> Optional[VehicleState]:
        """Get cached vehicle state"""
        return self._vehicle_state
    
    @property
    def is_connected(self) -> bool:
        """Check if connected to vehicles"""
        return self.transport.is_connected
    
    @property
    def is_driving(self) -> bool:
        """Check if actively driving"""
        return self.state == SupervisorState.DRIVING
