/**
 * transport_ble.h - BLE GATT transport (optional; controlled by config.h).
 *
 * Mirrors the real M25 wheel's BLE interface.
 * Enable with TRANSPORT_BLE_ENABLED = 1 in config.h.
 *
 * Implementation uses a FreeRTOS queue to decouple the BLE callback thread
 * from the main loop (calling notify() from within onWrite() causes rc=-1
 * errors on the client side with Bluedroid).
 *
 * API:
 *   ble_init(name, serviceUUID, txUUID, rxUUID)  - set up BLE server
 *   ble_connected()                               - true if client connected
 *   ble_send(data, len)                           - notify client
 *   ble_poll(out, outLen)                         - dequeue one received frame
 *   ble_start_advertising()                       - (re)start advertising
 *   ble_check_events(onConnect, onDisconnect)     - handle state changes
 */

#ifndef TRANSPORT_BLE_H
#define TRANSPORT_BLE_H

#if TRANSPORT_BLE_ENABLED

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/queue.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static BLEServer*         _bleServer   = nullptr;
static BLECharacteristic* _bleTxChar   = nullptr;
static BLECharacteristic* _bleRxChar   = nullptr;
static bool               _bleConnected    = false;
static bool               _bleWasConnected = false;

struct _BleRxPacket { uint8_t data[128]; size_t len; };
static QueueHandle_t _bleRxQueue = nullptr;

// Stale-packet tracking (reset on each new connection)
static bool          _bleFirstValid   = false;
static uint16_t      _bleStaleCount   = 0;
static unsigned long _bleConnTime     = 0;

// ---------------------------------------------------------------------------
// BLE server callbacks - connection events
// ---------------------------------------------------------------------------
class _BleServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        _bleConnected = true;
        if (_bleRxQueue) xQueueReset(_bleRxQueue); // Flush packets queued from previous session
        if (_bleRxChar) _bleRxChar->setValue("");  // Clear stale GATT value
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer*) override {
        _bleConnected = false;
        if (_bleRxQueue) xQueueReset(_bleRxQueue); // Discard any unprocessed packets
        if (_bleRxChar) _bleRxChar->setValue("");
        Serial.println("[BLE] Client disconnected");
    }
};

// ---------------------------------------------------------------------------
// BLE characteristic callbacks - receive data
// ---------------------------------------------------------------------------
class _BleRxCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* ch) override {
        size_t len = ch->getValue().length();
        if (len == 0 || len > 128) return;

        _BleRxPacket pkt;
        memcpy(pkt.data, ch->getData(), len);
        pkt.len = len;

        if (_bleRxQueue) xQueueSend(_bleRxQueue, &pkt, 0);
    }
};

// ---------------------------------------------------------------------------
// ble_init - initialise BLE device, service, and characteristics.
// ---------------------------------------------------------------------------
inline bool ble_init(const char* name,
                     const char* serviceUUID,
                     const char* txUUID,
                     const char* rxUUID) {
    _bleRxQueue = xQueueCreate(8, sizeof(_BleRxPacket));
    if (!_bleRxQueue) {
        Serial.println("[BLE] ERROR: Queue create failed");
        return false;
    }

    BLEDevice::init(name);
    Serial.printf("[BLE] MAC: %s\n", BLEDevice::getAddress().toString().c_str());

    _bleServer = BLEDevice::createServer();
    _bleServer->setCallbacks(new _BleServerCB());

    BLEService* svc = _bleServer->createService(serviceUUID);

    // TX characteristic: wheel -> client (notify)
    _bleTxChar = svc->createCharacteristic(txUUID,
                    BLECharacteristic::PROPERTY_NOTIFY);
    _bleTxChar->addDescriptor(new BLE2902());

    // RX characteristic: client -> wheel (write)
    _bleRxChar = svc->createCharacteristic(rxUUID,
                    BLECharacteristic::PROPERTY_WRITE);
    _bleRxChar->setCallbacks(new _BleRxCB());

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(serviceUUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE] Advertising as \"%s\"\n", name);
    return true;
}

// ---------------------------------------------------------------------------
// ble_on_connect - call from ble_check_events() when a connection is detected.
//   Resets stale-packet counters (analogous to rfcomm_on_connect).
// ---------------------------------------------------------------------------
inline void ble_on_connect() {
    _bleFirstValid = false;
    _bleStaleCount = 0;
    _bleConnTime   = millis();
}

// ble_on_disconnect - call from ble_check_events() on disconnect.
//   Restarts advertising so the next client can find and connect to us.
//   The short delay lets the Bluedroid stack finish its cleanup before
//   advertising is re-enabled (matches fake_m25_wheel behaviour).
// ---------------------------------------------------------------------------
inline void ble_on_disconnect() {
    // Queue already flushed by the ISR callback.
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted after disconnect");
}

// ---------------------------------------------------------------------------
// ble_connected - return true if a BLE client is connected.
// ---------------------------------------------------------------------------
inline bool ble_connected() {
    return _bleConnected;
}

// ---------------------------------------------------------------------------
// ble_first_valid / ble_mark_valid - stale-packet state accessors.
// ---------------------------------------------------------------------------
inline bool     ble_first_valid()  { return _bleFirstValid; }
inline void     ble_mark_valid()   { _bleFirstValid = true; }
inline uint16_t ble_stale_count()  { return _bleStaleCount; }
inline void     ble_stale_inc()    { ++_bleStaleCount; }
inline unsigned long ble_conn_time() { return _bleConnTime; }

// ---------------------------------------------------------------------------
// ble_disconnect - force-disconnect the current BLE client.
// ---------------------------------------------------------------------------
inline void ble_disconnect() {
    if (_bleServer) _bleServer->disconnect(_bleServer->getConnId());
}

// ---------------------------------------------------------------------------
// ble_rx_queue_waiting - number of frames sitting in the RX queue.
// ---------------------------------------------------------------------------
inline UBaseType_t ble_rx_queue_waiting() {
    if (!_bleRxQueue) return 0;
    return uxQueueMessagesWaiting(_bleRxQueue);
}

// ---------------------------------------------------------------------------
// ble_send - send bytes to connected client via notify.
// ---------------------------------------------------------------------------
inline void ble_send(const uint8_t* data, size_t len) {
    if (!_bleTxChar || !_bleConnected) return;
    _bleTxChar->setValue(const_cast<uint8_t*>(data), len);
    _bleTxChar->notify();
}

// ---------------------------------------------------------------------------
// ble_start_advertising - (re)start BLE advertising.
// ---------------------------------------------------------------------------
inline void ble_start_advertising() {
    BLEDevice::startAdvertising();
}

// ---------------------------------------------------------------------------
// ble_poll - dequeue one received frame (non-blocking).
//   Returns true and populates out/outLen when a frame is ready.
// ---------------------------------------------------------------------------
inline bool ble_poll(uint8_t* out, size_t* outLen) {
    if (!_bleRxQueue) return false;
    _BleRxPacket pkt;
    if (xQueueReceive(_bleRxQueue, &pkt, 0) != pdTRUE) return false;
    memcpy(out, pkt.data, pkt.len);
    *outLen = pkt.len;
    return true;
}

// ---------------------------------------------------------------------------
// ble_check_events - detect connect/disconnect transitions.
//   Calls internal on_connect/on_disconnect first (stale state, queue flush),
//   then the user-supplied callbacks.  Mirrors rfcomm_check_events().
//   Returns true if state changed.
// ---------------------------------------------------------------------------
inline bool ble_check_events(void (*onConnect)(), void (*onDisconnect)()) {
    if (_bleConnected == _bleWasConnected) return false;
    _bleWasConnected = _bleConnected;
    if (_bleConnected) {
        ble_on_connect();
        if (onConnect)    onConnect();
    } else {
        ble_on_disconnect();
        if (onDisconnect) onDisconnect();
    }
    return true;
}

#endif // TRANSPORT_BLE_ENABLED
#endif // TRANSPORT_BLE_H
