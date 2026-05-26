# Changelog

## [Unreleased]


## 1.2.0 - 2026-05-26

### Added
- New `gui/` package with modular GUI components (`app`, `theme`, `transport`, `widgets`).
- New `m25_transport.py` transport helper to improve backend selection and reuse.

### Changed
- Major Python refactor across control and transport code paths, including `m25_gui.py`, `m25_ecs.py`, `m25_bluetooth_ble.py`, `m25_spp.py`, and `core/transport/bluetooth.py`.
- Project documentation and launcher/setup scripts were updated to reflect a Python-focused workflow.
- Repository scope was clarified toward the standalone Python remote/control stack (`MPZ-00/m25-remote`) and away from mixed hardware-focused content.

### Removed
- Outdated ESP32 documentation/examples (`esp32/README.md`, `esp32/config.py`, `esp32/examples/basic_joystick.py`).
- Legacy Windows WinRT RFCOMM implementation (`m25_bluetooth_winrt.py`) and related test (`test_winrt_bluetooth.py`).
- WinRT optional dependency set in `pyproject.toml`; Windows transport now relies on the BLE path.


## v1.1.0-stable - 2026-03-14

### Added
- RFCOMM transport support for device communication and BLE wheel handle management.
- RFCOMM server channel info commands and dynamic SPP channel re-binding.
- New CLI device information commands.
- Runtime wheel-side configuration, environment profile handling, and NVS MAC/key storage.
- BLE telemetry/debug improvements including error code translations and traffic recording.

### Changed
- Improved BLE connection lifecycle (retries, disconnect handling, advertising restart, stack reset).
- Refined BLE TX/RX handling, response parsing, and write-mode detection.
- Expanded serial command interface and runtime debug controls.
- Improved safety/control behavior with timeout, idle handling, and supervisor flow refinements.

### Fixed
- BLE dropped-connection and stale-packet edge cases.
- Silent/failed BLE writes and several null/error handling crash paths.
- Connection-state isolation and reliability issues across reconnect scenarios.