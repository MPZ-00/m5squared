#!/usr/bin/env python3
"""Shared helpers and lightweight interfaces for M25 transport selection."""

from typing import Optional, Protocol, runtime_checkable, List, Tuple

M25_VERSION_AUTO = "auto"
M25_VERSION_V1 = "v1"
M25_VERSION_V2 = "v2"

TRANSPORT_AUTO = "auto"
TRANSPORT_RFCOMM = "rfcomm"
TRANSPORT_BLE = "ble"

VALID_M25_VERSIONS = {
    M25_VERSION_AUTO,
    M25_VERSION_V1,
    M25_VERSION_V2,
}


@runtime_checkable
class BluetoothBackend(Protocol):
    """Minimal shared backend interface used by RFCOMM/BLE discovery modules."""

    def scan_devices(self, duration: int = 10, filter_m25: bool = False) -> List[Tuple[str, str]]:
        """Scan nearby devices and return (address, name) pairs."""
        ...

    def load_state(self) -> dict:
        """Load persisted connection state."""
        ...

    def save_state(self, state: dict) -> None:
        """Persist connection state."""
        ...

    def clear_state(self) -> None:
        """Clear persisted connection state."""
        ...


def normalize_m25_version(version: Optional[str]) -> str:
    """Normalize user-provided M25 generation selection."""
    if version is None:
        return M25_VERSION_AUTO

    value = version.strip().lower()
    if value in VALID_M25_VERSIONS:
        return value
    return M25_VERSION_AUTO


def preferred_transport_for_version(version: Optional[str]) -> str:
    """Map an explicit M25 generation to its preferred transport."""
    normalized = normalize_m25_version(version)
    if normalized == M25_VERSION_V1:
        return TRANSPORT_RFCOMM
    if normalized == M25_VERSION_V2:
        return TRANSPORT_BLE
    return TRANSPORT_AUTO


def describe_m25_version(version: Optional[str]) -> str:
    """Human-readable description for logs and UI."""
    normalized = normalize_m25_version(version)
    if normalized == M25_VERSION_V1:
        return "M25V1 (RFCOMM)"
    if normalized == M25_VERSION_V2:
        return "M25V2 (BLE)"
    return "Auto Detect"