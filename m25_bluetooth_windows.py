#!/usr/bin/env python3
"""
M25 Windows Bluetooth Wrapper - Backward Compatibility

This module exists for backward compatibility only.
New code should use m25_bluetooth_ble.py instead.
"""

import sys
from m25_bluetooth_ble import M25BluetoothBLE as M25WindowsBluetooth
from m25_bluetooth_ble import scan_devices, connect_device

# Show deprecation warning
print("WARNING: m25_bluetooth_windows is deprecated. Use m25_bluetooth_ble instead.", file=sys.stderr)

__all__ = ['M25WindowsBluetooth', 'scan_devices', 'connect_device']
