#!/usr/bin/env python3
"""
Bluetooth RFCOMM Server for Mock Wheel Simulator

Implements a Bluetooth RFCOMM/SPP server using PyBluez to emulate
an M25 wheel that can be discovered and connected to via Bluetooth.

This uses classic Bluetooth (not BLE) which is simpler and more reliable
for testing purposes. The M25 wheels support both BLE and RFCOMM.
"""

import logging
import socket
import threading
from typing import Optional, Callable
from pathlib import Path
import sys

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    import bluetooth
    HAS_PYBLUEZ = True
except ImportError:
    HAS_PYBLUEZ = False
    print("ERROR: pybluez not installed. Install with: pip install pybluez", file=sys.stderr)


logger = logging.getLogger(__name__)


class RFCOMMServer:
    """
    Bluetooth RFCOMM/SPP server for mock wheel simulator.
    
    Appears as a standard Bluetooth serial port device, just like the real M25 wheels.
    """
    
    def __init__(
        self,
        device_name: str = "e-motion M25 Left",
        data_handler: Optional[Callable[[bytes], bytes]] = None,
        debug: bool = False
    ):
        """
        Initialize RFCOMM server.
        
        Args:
            device_name: Advertised device name
            data_handler: Callback function(data: bytes) -> response: bytes
            debug: Enable debug logging
        """
        if not HAS_PYBLUEZ:
            raise RuntimeError(
                "PyBluez not available. Install: pip install pybluez\n"
                "Note: On Linux, also requires: sudo apt-get install libbluetooth-dev"
            )
        
        self.device_name = device_name
        self.data_handler = data_handler
        self.debug = debug
        
        self.server_sock: Optional[bluetooth.BluetoothSocket] = None
        self.client_sock: Optional[bluetooth.BluetoothSocket] = None
        self.is_running = False
        self.client_thread: Optional[threading.Thread] = None
        
        logger.info(f"RFCOMM Server initialized: {device_name}")
    
    def start(self):
        """Start the RFCOMM server (blocking)"""
        if not HAS_PYBLUEZ:
            raise RuntimeError("PyBluez library required for Bluetooth RFCOMM mode")
        
        logger.info(f"Starting Bluetooth RFCOMM server: {self.device_name}")
        
        # Create server socket
        self.server_sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        
        # Bind to any available port
        # On Windows, we need to get the local adapter address
        try:
            # Try to get local Bluetooth address
            local_addr = bluetooth.read_local_bdaddr()[0]
        except:
            # Fallback to using empty string (works on Linux/macOS)
            local_addr = ""
        
        try:
            self.server_sock.bind((local_addr, bluetooth.PORT_ANY))
        except (SystemError, OSError) as e:
            # Windows workaround: bind without specifying address
            logger.warning(f"Standard bind failed ({e}), trying alternative method")
            # On Windows, we can just bind to port 0 to get any available port
            self.server_sock.bind((bluetooth.BDADDR_ANY, 0))
        
        port = self.server_sock.getsockname()[1]
        
        self.server_sock.listen(1)
        
        # Advertise service
        uuid = "00001101-0000-1000-8000-00805F9B34FB"  # Serial Port Profile UUID
        
        bluetooth.advertise_service(
            self.server_sock,
            self.device_name,
            service_id=uuid,
            service_classes=[uuid, bluetooth.SERIAL_PORT_CLASS],
            profiles=[bluetooth.SERIAL_PORT_PROFILE]
        )
        
        logger.info(f"Bluetooth server started on RFCOMM port {port}")
        logger.info(f"Device name: {self.device_name}")
        logger.info("Waiting for connections...")
        logger.info("Clients can discover and pair with this device")
        
        self.is_running = True
        
        try:
            while self.is_running:
                logger.info("Waiting for connection...")
                
                # Accept connections
                self.client_sock, client_info = self.server_sock.accept()
                logger.info(f"Client connected: {client_info}")
                
                # Handle client in current thread (blocking)
                try:
                    self._handle_client()
                except Exception as e:
                    logger.error(f"Error handling client: {e}", exc_info=True)
                finally:
                    if self.client_sock:
                        self.client_sock.close()
                        self.client_sock = None
                    logger.info("Client disconnected")
        
        except KeyboardInterrupt:
            logger.info("Server interrupted")
        finally:
            self.stop()
    
    def _handle_client(self):
        """Handle connected client"""
        while self.is_running and self.client_sock:
            try:
                # Receive data
                data = self.client_sock.recv(1024)
                if not data:
                    break
                
                if self.debug:
                    logger.debug(f"Received {len(data)} bytes")
                
                # Process data with handler
                if self.data_handler:
                    response = self.data_handler(data)
                    if response:
                        self.client_sock.send(response)
                        if self.debug:
                            logger.debug(f"Sent {len(response)} bytes")
            
            except bluetooth.BluetoothError as e:
                logger.error(f"Bluetooth error: {e}")
                break
            except Exception as e:
                logger.error(f"Error handling data: {e}", exc_info=True)
                break
    
    def stop(self):
        """Stop the RFCOMM server"""
        logger.info("Stopping Bluetooth server...")
        self.is_running = False
        
        if self.client_sock:
            try:
                self.client_sock.close()
            except:
                pass
            self.client_sock = None
        
        if self.server_sock:
            try:
                bluetooth.stop_advertising(self.server_sock)
                self.server_sock.close()
            except:
                pass
            self.server_sock = None
        
        logger.info("Bluetooth server stopped")


def test_rfcomm_server():
    """Test RFCOMM server with echo handler"""
    
    def echo_handler(data: bytes) -> bytes:
        """Echo back received data"""
        print(f"Received: {data.hex()}")
        return data
    
    server = RFCOMMServer(
        device_name="Test M25 Wheel",
        data_handler=echo_handler,
        debug=True
    )
    
    try:
        server.start()
    except KeyboardInterrupt:
        server.stop()


if __name__ == '__main__':
    # Test the RFCOMM server
    if not HAS_PYBLUEZ:
        print("ERROR: PyBluez not available")
        print("Install: pip install pybluez")
        sys.exit(1)
    
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    test_rfcomm_server()
