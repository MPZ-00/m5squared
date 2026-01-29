"""
M25 Crypto for MicroPython
Minimal AES-128-CBC encryption/decryption using ucryptolib
"""
import ucryptolib
from ubinascii import hexlify

class M25Crypto:
    def __init__(self, key):
        """key: 16-byte bytearray or bytes"""
        self.key = key
    
    def encrypt_packet(self, plaintext):
        """
        Encrypt M25 packet: IV (ECB) + payload (CBC)
        Returns 32-byte encrypted packet
        """
        # Generate IV (first 16 bytes of plaintext or random)
        iv = plaintext[:16]
        
        # Encrypt IV with ECB
        ecb = ucryptolib.aes(self.key, 1)  # Mode 1 = ECB
        encrypted_iv = ecb.encrypt(iv)
        
        # Encrypt payload with CBC using the IV
        cbc = ucryptolib.aes(self.key, 2, encrypted_iv)  # Mode 2 = CBC
        encrypted_payload = cbc.encrypt(plaintext)
        
        return encrypted_iv + encrypted_payload[16:]
    
    def decrypt_packet(self, ciphertext):
        """
        Decrypt M25 packet: IV (ECB) + payload (CBC)
        Returns 16-byte plaintext
        """
        encrypted_iv = ciphertext[:16]
        encrypted_payload = ciphertext[16:32]
        
        # Decrypt IV with ECB
        ecb = ucryptolib.aes(self.key, 1)
        iv = ecb.decrypt(encrypted_iv)
        
        # Decrypt payload with CBC
        cbc = ucryptolib.aes(self.key, 2, iv)
        plaintext = cbc.decrypt(encrypted_iv + encrypted_payload)
        
        return plaintext

# Example usage:
# crypto = M25Crypto(b'\xAA\xBB\xCC\xDD\xEE\xFF\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99')
# encrypted = crypto.encrypt_packet(b'\x01' * 16)
# decrypted = crypto.decrypt_packet(encrypted)
