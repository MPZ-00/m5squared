/**
 * transport_rfcomm.h - Bluetooth Classic SPP (RFCOMM) transport.
 *
 * Uses the ESP32 BluetoothSerial library to act as an SPP server.
 * The real M25 wheel advertises its SPP service on RFCOMM channel 6;
 * m25_spp.py hard-codes channel 6 when connecting.
 *
 * Channel note: BluetoothSerial assigns the channel automatically via the
 * Bluetooth stack.  The assigned channel is printed at startup.  If the
 * channel does not match what m25_spp.py expects (6), update RFCOMM_CHANNEL
 * in the Python client or configure the Python side to do an SDP lookup.
 *
 * API:
 *   rfcomm_init(name)         - start SPP server with given device name
 *   rfcomm_connected()        - true if a client is currently connected
 *   rfcomm_send(data, len)    - send raw bytes to connected client
 *   rfcomm_poll(out, outLen)  - try to read one complete M25 frame from buffer
 *                               returns true when a frame is ready in out
 */

#ifndef TRANSPORT_RFCOMM_H
#define TRANSPORT_RFCOMM_H

#include <Arduino.h>
#include <BluetoothSerial.h>
#include "config.h"
#include "protocol.h"

// ---------------------------------------------------------------------------
// Internal state - not accessible outside this header
// ---------------------------------------------------------------------------
static BluetoothSerial _rfBT;
static bool            _rfConnected     = false;
static bool            _rfWasConnected  = false;

// Receive accumulation buffer
static uint8_t  _rfRxBuf[256];
static size_t   _rfRxLen = 0;

// Stale-packet tracking (reset on each new connection)
static bool          _rfFirstValid    = false;
static uint16_t      _rfStaleCount    = 0;
static unsigned long _rfConnTime      = 0;

// ---------------------------------------------------------------------------
// rfcomm_init - start SPP server.
//   Returns true on success.
// ---------------------------------------------------------------------------
inline bool rfcomm_init(const char* name) {
    if (!_rfBT.begin(name)) {
        Serial.println("[RFCOMM] ERROR: BluetoothSerial init failed");
        return false;
    }
    Serial.printf("[RFCOMM] SPP server started as \"%s\"\n", name);
    Serial.println("[RFCOMM] NOTE: Check actual RFCOMM channel in BT stack output.");
    Serial.printf("[RFCOMM] m25_spp.py expects channel %d; update if needed.\n",
                  RFCOMM_CHANNEL);
    return true;
}

// ---------------------------------------------------------------------------
// rfcomm_connected - return true if a client is connected right now.
// ---------------------------------------------------------------------------
inline bool rfcomm_connected() {
    return _rfBT.connected();
}

// ---------------------------------------------------------------------------
// rfcomm_on_connect - call from loop() when a new connection is detected.
// ---------------------------------------------------------------------------
inline void rfcomm_on_connect() {
    _rfFirstValid   = false;
    _rfStaleCount   = 0;
    _rfConnTime     = millis();
    _rfRxLen        = 0;   // Flush stale buffer
    Serial.println("[RFCOMM] Client connected");
}

// ---------------------------------------------------------------------------
// rfcomm_on_disconnect - call from loop() when disconnect is detected.
// ---------------------------------------------------------------------------
inline void rfcomm_on_disconnect() {
    _rfRxLen = 0;
    Serial.println("[RFCOMM] Client disconnected");
}

// ---------------------------------------------------------------------------
// rfcomm_send - send raw bytes to the connected client.
// ---------------------------------------------------------------------------
inline void rfcomm_send(const uint8_t* data, size_t len) {
    _rfBT.write(data, len);
}

// ---------------------------------------------------------------------------
// _rfcomm_drain - read all currently available bytes into _rfRxBuf.
// ---------------------------------------------------------------------------
static void _rfcomm_drain() {
    while (_rfBT.available() && _rfRxLen < sizeof(_rfRxBuf)) {
        _rfRxBuf[_rfRxLen++] = (uint8_t)_rfBT.read();
    }
}

// ---------------------------------------------------------------------------
// _rfcomm_drop_until_header - discard bytes before the first 0xEF.
// ---------------------------------------------------------------------------
static void _rfcomm_drop_until_header() {
    size_t start = 0;
    while (start < _rfRxLen && _rfRxBuf[start] != M25_HEADER_MARKER) start++;
    if (start > 0) {
        memmove(_rfRxBuf, _rfRxBuf + start, _rfRxLen - start);
        _rfRxLen -= start;
    }
}

// ---------------------------------------------------------------------------
// _rfcomm_try_parse - attempt to extract one complete M25 frame.
//
//   Unstuffs the buffer incrementally to know exactly how many stuffed bytes
//   correspond to one complete logical frame, then validates the CRC.
//
//   If a valid frame is found it is copied to out/outLen and consumed from
//   the buffer; returns true.  Otherwise returns false.
// ---------------------------------------------------------------------------
static bool _rfcomm_try_parse(uint8_t* out, size_t* outLen) {
    if (_rfRxLen < 5) return false;   // Too short for any valid frame

    // Unstuff enough bytes to read the 3-byte header and determine total length
    uint8_t unstuffed[128];
    size_t  uPos = 0;
    size_t  sPos = 0;

    // We need at least 3 unstuffed bytes for the header
    while (sPos < _rfRxLen && uPos < 3) {
        unstuffed[uPos++] = _rfRxBuf[sPos++];
        if (uPos > 1 &&
            unstuffed[uPos - 1] == M25_HEADER_MARKER &&
            sPos < _rfRxLen &&
            _rfRxBuf[sPos] == M25_HEADER_MARKER) {
            sPos++;   // Skip stuffed duplicate
        }
    }
    if (uPos < 3) return false;

    // frameLength field tells us total unstuffed bytes = frameLength + 1
    const uint16_t frameField = ((uint16_t)unstuffed[1] << 8) | unstuffed[2];
    const size_t   totalU     = (size_t)frameField + 1;

    if (totalU > sizeof(unstuffed)) return false;   // Sanity guard

    // Continue unstuffing until we have totalU unstuffed bytes
    while (sPos < _rfRxLen && uPos < totalU) {
        unstuffed[uPos++] = _rfRxBuf[sPos++];
        if (uPos > 1 &&
            unstuffed[uPos - 1] == M25_HEADER_MARKER &&
            sPos < _rfRxLen &&
            _rfRxBuf[sPos] == M25_HEADER_MARKER) {
            sPos++;
        }
    }
    if (uPos < totalU) return false;   // Not enough data yet

    // Validate CRC
    const size_t   crcOffset = totalU - M25_CRC_SIZE;
    const uint16_t calcCRC   = proto_crc16(unstuffed, crcOffset);
    const uint16_t frameCRC  = ((uint16_t)unstuffed[crcOffset] << 8)
                             |            unstuffed[crcOffset + 1];

    if (calcCRC != frameCRC) {
        // Bad frame: discard first byte and retry next call
        memmove(_rfRxBuf, _rfRxBuf + 1, _rfRxLen - 1);
        _rfRxLen--;
        return false;
    }

    // Valid frame: copy the consumed stuffed bytes to out; advance buffer
    if (sPos > sizeof(_rfRxBuf)) return false;
    *outLen = sPos;
    memcpy(out, _rfRxBuf, sPos);
    memmove(_rfRxBuf, _rfRxBuf + sPos, _rfRxLen - sPos);
    _rfRxLen -= sPos;
    return true;
}

// ---------------------------------------------------------------------------
// rfcomm_poll - drain serial, attempt to read one complete M25 frame.
//
//   out / outLen : output stuffed frame bytes (caller: >=128 bytes)
//   Returns true when a frame is ready.
// ---------------------------------------------------------------------------
inline bool rfcomm_poll(uint8_t* out, size_t* outLen) {
    _rfcomm_drain();
    _rfcomm_drop_until_header();

    if (_rfRxLen < 5) return false;

    const bool found = _rfcomm_try_parse(out, outLen);

    // Guard: prevent buffer from filling permanently with garbage
    if (!found && _rfRxLen > 192) {
        memmove(_rfRxBuf, _rfRxBuf + 1, _rfRxLen - 1);
        _rfRxLen--;
    }
    return found;
}

// ---------------------------------------------------------------------------
// rfcomm_check_events - detect connect/disconnect transitions.
//   Call each loop(); returns true if state changed.
// ---------------------------------------------------------------------------
inline bool rfcomm_check_events() {
    const bool now = _rfBT.connected();
    if (now == _rfWasConnected) return false;
    _rfWasConnected = now;
    _rfConnected    = now;
    if (now) rfcomm_on_connect();
    else     rfcomm_on_disconnect();
    return true;
}

#endif // TRANSPORT_RFCOMM_H
