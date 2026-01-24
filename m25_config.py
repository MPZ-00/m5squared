#!/usr/bin/env python3
"""
M25 Environment Configuration Helper

Provides easy access to .env configuration for all m25 tools.
Automatically loads .env file and provides defaults.
"""

import os
from pathlib import Path
from typing import Optional

# Try to load dotenv
try:
    from dotenv import load_dotenv
    HAS_DOTENV = True
except ImportError:
    HAS_DOTENV = False


class M25Config:
    """Configuration manager for M25 tools"""
    
    def __init__(self, env_file: Optional[str] = None):
        """
        Initialize configuration
        
        Args:
            env_file: Path to .env file (default: .env in current directory)
        """
        self._loaded = False
        
        if HAS_DOTENV:
            if env_file is None:
                env_file = Path(".env")
            else:
                env_file = Path(env_file)
            
            if env_file.exists():
                load_dotenv(env_file)
                self._loaded = True
    
    @property
    def left_mac(self) -> Optional[str]:
        """Left wheel MAC address"""
        return os.getenv("M25_LEFT_MAC")
    
    @property
    def right_mac(self) -> Optional[str]:
        """Right wheel MAC address"""
        return os.getenv("M25_RIGHT_MAC")
    
    @property
    def left_key(self) -> Optional[str]:
        """Left wheel encryption key (hex)"""
        return os.getenv("M25_LEFT_KEY")
    
    @property
    def right_key(self) -> Optional[str]:
        """Right wheel encryption key (hex)"""
        return os.getenv("M25_RIGHT_KEY")
    
    @property
    def rfcomm_channel(self) -> int:
        """Bluetooth RFCOMM channel (default: 6)"""
        return int(os.getenv("M25_RFCOMM_CHANNEL", "6"))
    
    @property
    def timeout(self) -> int:
        """Connection timeout in seconds (default: 10)"""
        return int(os.getenv("M25_TIMEOUT", "10"))
    
    @property
    def is_configured(self) -> bool:
        """Check if basic configuration is present"""
        return all([
            self.left_mac,
            self.right_mac,
            self.left_key,
            self.right_key
        ])
    
    def validate(self, require_both_wheels: bool = True) -> tuple[bool, list[str]]:
        """
        Validate configuration
        
        Args:
            require_both_wheels: If True, require both left and right wheel config
            
        Returns:
            (is_valid, list_of_errors)
        """
        errors = []
        
        if not HAS_DOTENV:
            errors.append("python-dotenv not installed (optional but recommended)")
        
        if require_both_wheels:
            if not self.left_mac:
                errors.append("M25_LEFT_MAC not set")
            if not self.left_key:
                errors.append("M25_LEFT_KEY not set")
            if not self.right_mac:
                errors.append("M25_RIGHT_MAC not set")
            if not self.right_key:
                errors.append("M25_RIGHT_KEY not set")
        else:
            # At least one wheel must be configured
            if not (self.left_mac or self.right_mac):
                errors.append("At least one wheel MAC address must be set")
            if not (self.left_key or self.right_key):
                errors.append("At least one wheel key must be set")
        
        # Validate MAC address format (basic check)
        for mac_name, mac_value in [("LEFT", self.left_mac), ("RIGHT", self.right_mac)]:
            if mac_value:
                if not self._is_valid_mac(mac_value):
                    errors.append(f"M25_{mac_name}_MAC has invalid format (expected AA:BB:CC:DD:EE:FF)")
        
        # Validate key format (should be 32 hex chars = 16 bytes)
        for key_name, key_value in [("LEFT", self.left_key), ("RIGHT", self.right_key)]:
            if key_value:
                if not self._is_valid_key(key_value):
                    errors.append(f"M25_{key_name}_KEY has invalid format (expected 32 hex characters)")
        
        return len(errors) == 0, errors
    
    @staticmethod
    def _is_valid_mac(mac: str) -> bool:
        """Check if MAC address has valid format"""
        parts = mac.split(":")
        if len(parts) != 6:
            return False
        for part in parts:
            if len(part) != 2:
                return False
            try:
                int(part, 16)
            except ValueError:
                return False
        return True
    
    @staticmethod
    def _is_valid_key(key: str) -> bool:
        """Check if encryption key has valid format"""
        if len(key) != 32:
            return False
        try:
            int(key, 16)
            return True
        except ValueError:
            return False
    
    def print_status(self):
        """Print configuration status"""
        print("M25 Configuration Status:")
        print(f"  .env loaded: {'Yes' if self._loaded else 'No'}")
        print(f"  Left MAC:    {self.left_mac or '(not set)'}")
        print(f"  Right MAC:   {self.right_mac or '(not set)'}")
        print(f"  Left Key:    {'(set)' if self.left_key else '(not set)'}")
        print(f"  Right Key:   {'(set)' if self.right_key else '(not set)'}")
        print(f"  RFCOMM Ch:   {self.rfcomm_channel}")
        print(f"  Timeout:     {self.timeout}s")
        
        is_valid, errors = self.validate()
        if is_valid:
            print("\n  Status: Configuration is valid")
        else:
            print("\n  Status: Configuration has errors:")
            for error in errors:
                print(f"    - {error}")


# Global config instance
_config = None

def get_config(reload: bool = False) -> M25Config:
    """
    Get the global configuration instance
    
    Args:
        reload: Force reload of .env file
        
    Returns:
        M25Config instance
    """
    global _config
    if _config is None or reload:
        _config = M25Config()
    return _config


def main():
    """Command-line utility to check configuration"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="M25 Configuration Utility",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Check current configuration:
    python m25_config.py
    
  Validate configuration:
    python m25_config.py --validate
    
  Use custom .env file:
    python m25_config.py --env-file /path/to/.env
        """
    )
    
    parser.add_argument("--env-file", help="Path to .env file")
    parser.add_argument("--validate", action="store_true",
                       help="Validate configuration and exit with error if invalid")
    
    args = parser.parse_args()
    
    # Load config
    config = M25Config(args.env_file)
    
    # Print status
    config.print_status()
    
    # Validate if requested
    if args.validate:
        is_valid, errors = config.validate()
        if not is_valid:
            print("\nValidation failed!")
            import sys
            sys.exit(1)
        else:
            print("\nValidation passed!")


if __name__ == "__main__":
    main()
