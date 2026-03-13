# Changelog

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
