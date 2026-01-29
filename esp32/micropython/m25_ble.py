"""
M25 BLE Connection for MicroPython
Handles BLE SPP connection to M25 wheels
"""
import ubluetooth
import time

class M25BLE:
    def __init__(self, crypto):
        self.ble = ubluetooth.BLE()
        self.ble.active(True)
        self.ble.irq(self._irq_handler)
        self.crypto = crypto
        self.conn_handle = None
        self.spp_char = None
        self._rx_buffer = bytearray()
        
    def _irq_handler(self, event, data):
        if event == 1:  # _IRQ_CENTRAL_CONNECT
            self.conn_handle, _, _ = data
            print("BLE connected")
        elif event == 2:  # _IRQ_CENTRAL_DISCONNECT
            self.conn_handle = None
            print("BLE disconnected")
        elif event == 3:  # _IRQ_GATTS_WRITE
            # Handle received data
            conn_handle, attr_handle = data
            self._rx_buffer = self.ble.gatts_read(attr_handle)
    
    def connect(self, mac_address):
        """Connect to M25 wheel by MAC address"""
        # Scan for device
        self.ble.gap_scan(2000, 30000, 30000)
        # Implementation depends on finding and connecting to SPP service
        # UUID: 00001101-0000-1000-8000-00805F9B34FB
        pass
    
    def send_packet(self, plaintext):
        """Encrypt and send packet to wheel"""
        if not self.conn_handle:
            return False
        encrypted = self.crypto.encrypt_packet(plaintext)
        # Write to SPP characteristic
        # self.ble.gattc_write(self.conn_handle, self.spp_char, encrypted)
        return True
    
    def receive_packet(self):
        """Receive and decrypt packet from wheel"""
        if len(self._rx_buffer) >= 32:
            encrypted = bytes(self._rx_buffer[:32])
            self._rx_buffer = self._rx_buffer[32:]
            return self.crypto.decrypt_packet(encrypted)
        return None

# Example usage:
# from m25_crypto import M25Crypto
# crypto = M25Crypto(KEY)
# ble = M25BLE(crypto)
# ble.connect("AA:BB:CC:DD:EE:FF")
# ble.send_packet(b'\x01' * 16)
