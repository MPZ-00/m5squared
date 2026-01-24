# BLE Notifications - How It Works

## Overview

BLE (Bluetooth Low Energy) supports two methods for receiving data from a device:

1. **Polling (Read)**: Client constantly requests data from device
2. **Notifications (Push)**: Device sends data to client when available

This document explains how notifications work and why they're better for battery-powered devices.

## Polling vs Notifications

### Polling Approach (Less Efficient)

```python
# Client must constantly read
while True:
    data = await bt.receive_packet()  # Device must wake up for every read
    await asyncio.sleep(0.1)          # Still polling 10 times per second
```

**How it works:**
- Client sends read request
- Device wakes from sleep
- Device prepares response
- Device sends data
- Device goes back to sleep
- **Repeat constantly**

**Impact:**
- Device stays partially awake to respond to reads
- Radio must be active for each request/response cycle
- Battery drains faster even when no data to send

### Notifications Approach (More Efficient)

```python
# Device pushes data only when needed
await bt.start_notifications(callback)
# Client waits passively
```

**How it works:**
- Client subscribes to notifications once
- Device goes to deep sleep
- Device wakes **only** when it has data to send
- Device pushes data to client
- Device goes back to deep sleep

**Impact:**
- Device sleeps deeply between events
- Radio only active when sending data
- Battery lasts longer during operation

## BLE Technical Details

### GATT Characteristics

BLE devices expose **characteristics** with different properties:

- **Read**: Client can request data
- **Write**: Client can send data
- **Notify**: Device can push data to client

The M25 wheels use Nordic UART Service with:
- TX Characteristic (Write): Client sends commands
- RX Characteristic (Notify): Device sends status/responses

### CCCD (Client Characteristic Configuration Descriptor)

When you call `start_notifications()`:
1. Client writes to CCCD descriptor (0x2902)
2. This tells device "I want notifications"
3. Device enables notifications for this client
4. Data flows automatically when device has something to send

When you call `stop_notifications()`:
1. Client writes 0x0000 to CCCD
2. Device disables notifications
3. Connection returns to idle state

## M25 Wheelchair Specifications

Based on manufacturer specifications:

**Battery:**
- Type: Lithium-ion 10ICR19/66-2
- Voltage: 36.5V
- Charging temperature: 0°C - 40°C
- Operating temperature: -25°C to +50°C

**Motor:**
- Output: 280W
- Voltage: 36.5V

**Range:**
- 25 kilometers per charge (ISO 7176-4)
- 6 km/h maximum speed with Mobility Plus Package

**Weight:**
- Wheel: 7.8 kg
- Charger: 1.2 kg
- Total system: 15.6 kg

## Why Notifications Matter for M25

The M25 wheels are battery-powered. Every milliamp saved extends:
- Operating time between charges
- Battery lifespan (fewer charge cycles)
- User independence

**Key benefit:** Notifications let the wheel's BLE radio sleep deeply between events, conserving battery for driving rather than communication overhead.

## Implementation in m25_bluetooth_ble.py

### Three Usage Patterns

#### 1. Callback Pattern (Event-Driven)

Best for: Real-time processing, immediate response to wheel events

```python
def handle_wheel_status(data: bytes):
    # Process immediately
    print(f"Wheel status: {data.hex()}")

await bt.start_notifications(handle_wheel_status)
# Callback fires automatically when data arrives
```

#### 2. Queue Pattern (Sequential Processing)

Best for: Async workflows, ordered processing

```python
await bt.start_notifications()  # No callback = uses queue

while True:
    data = await bt.wait_notification(timeout=5.0)
    if data:
        # Process in order
        await process_status(data)
```

#### 3. Polling Pattern (Not Recommended)

Only use for testing or when notifications unavailable:

```python
while True:
    data = await bt.receive_packet()  # Forces device awake
    await asyncio.sleep(0.1)
```

### Internal Flow

```
┌─────────────────────────────────────────────────┐
│ Application calls start_notifications()         │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ Bleak enables BLE notifications                 │
│ (writes 0x0100 to CCCD descriptor)              │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ Device: "Notifications enabled, going to sleep" │
└─────────────────┬───────────────────────────────┘
                  │
                  │ (Device sleeps deeply)
                  │
┌─────────────────▼───────────────────────────────┐
│ Device wakes: "I have data to send!"            │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ Device sends notification (pushes data)         │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ Bleak calls notification_handler()              │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ Decrypt data (if encryptor present)             │
└─────────────────┬───────────────────────────────┘
                  │
          ┌───────┴────────┐
          │                │
┌─────────▼──────┐  ┌─────▼──────────┐
│ Call callback  │  │ Add to queue   │
│ (if provided)  │  │ (if no callback)│
└────────────────┘  └────────────────┘
```

## Testing Notifications

Use the test utility:

```bash
# Test callback pattern
python demos/test_notifications.py callback

# Test queue pattern
python demos/test_notifications.py queue

# Compare with polling (shows difference)
python demos/test_notifications.py polling
```

## Platform Support

Notifications work on all platforms via Bleak:

- **Windows 10+**: Native BLE notifications
- **Linux**: Via BlueZ (requires bluetooth group membership)
- **macOS 10.13+**: Native BLE notifications

## Common Issues

### "No RX characteristic found"

Device doesn't support notifications. Check:
- Device advertises Nordic UART Service
- RX characteristic has Notify property
- Device is compatible with BLE (not just Bluetooth Classic)

### Notifications not firing

Check:
- `start_notifications()` returned True
- Device is sending data (not just sleeping)
- Connection is still active (`bt.is_connected()`)

### Data corruption

Ensure:
- Decryption key matches device
- Notification handler is thread-safe
- Not calling blocking code in callback

## Best Practices

1. **Always use notifications for production** - Only use polling for testing
2. **Check return value** - `start_notifications()` can fail if device doesn't support it
3. **Stop notifications on disconnect** - Prevents resource leaks
4. **Don't block in callbacks** - Process quickly or queue for later
5. **Handle connection loss** - Notifications stop if connection drops

## References

- BLE Specification: Notifications (Vol 3, Part G, Section 4.10)
- Nordic UART Service: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/libraries/bluetooth_services/services/nus.html
- Bleak Documentation: https://bleak.readthedocs.io/
- M25 Specifications: Alber e-motion manufacturer documentation
