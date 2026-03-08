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

// ---------------------------------------------------------------------------
// BLE server callbacks - connection events
// ---------------------------------------------------------------------------
class _BleServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        _bleConnected = true;
        if (_bleRxChar) _bleRxChar->setValue("");   // Flush stale data
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer*) override {
        _bleConnected = false;
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
// ble_connected - return true if a BLE client is connected.
// ---------------------------------------------------------------------------
inline bool ble_connected() {
    return _bleConnected;
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
//   Provide callbacks (may be nullptr) for side-effects in the main sketch.
//   Returns true if state changed.
// ---------------------------------------------------------------------------
inline bool ble_check_events(void (*onConnect)(), void (*onDisconnect)()) {
    if (_bleConnected == _bleWasConnected) return false;
    _bleWasConnected = _bleConnected;
    if (_bleConnected) { if (onConnect)    onConnect(); }
    else               { if (onDisconnect) onDisconnect(); }
    return true;
}

#endif // TRANSPORT_BLE_ENABLED
#endif // TRANSPORT_BLE_H
