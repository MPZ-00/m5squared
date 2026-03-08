/**
 * protocol.h - M25 low-level framing.
 *
 * Responsibilities:
 *   - CRC-16 calculation
 *   - Byte stuffing (add_delimiters) and unstuffing (remove_delimiters)
 *   - Frame parsing: stuffed raw bytes -> unstuffed logical frame + payload
 *   - Frame building: payload -> stuffed wire bytes
 *
 * No crypto, no state, no Serial prints outside debug helpers.
 * Every function takes explicit inputs and returns its result; no globals.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// CRC-16 lookup table (matching m25_protocol.py)
// Stored in flash to save RAM.
// ---------------------------------------------------------------------------
static const uint16_t _crcTable[256] PROGMEM = {
    0,49345,49537,320,49921,960,640,49729,50689,1728,1920,51009,1280,50625,50305,1088,
    52225,3264,3456,52545,3840,53185,52865,3648,2560,51905,52097,2880,51457,2496,2176,51265,
    55297,6336,6528,55617,6912,56257,55937,6720,7680,57025,57217,8000,56577,7616,7296,56385,
    5120,54465,54657,5440,55041,6080,5760,54849,53761,4800,4992,54081,4352,53697,53377,4160,
    61441,12480,12672,61761,13056,62401,62081,12864,13824,63169,63361,14144,62721,13760,13440,62529,
    15360,64705,64897,15680,65281,16320,16000,65089,64001,15040,15232,64321,14592,63937,63617,14400,
    10240,59585,59777,10560,60161,11200,10880,59969,60929,11968,12160,61249,11520,60865,60545,11328,
    58369,9408,9600,58689,9984,59329,59009,9792,8704,58049,58241,9024,57601,8640,8320,57409,
    40961,24768,24960,41281,25344,41921,41601,25152,26112,42689,42881,26432,42241,26048,25728,42049,
    27648,44225,44417,27968,44801,28608,28288,44609,43521,27328,27520,43841,26880,43457,43137,26688,
    30720,47297,47489,31040,47873,31680,31360,47681,48641,32448,32640,48961,32000,48577,48257,31808,
    46081,29888,30080,46401,30464,47041,46721,30272,29184,45761,45953,29504,45313,29120,28800,45121,
    20480,37057,37249,20800,37633,21440,21120,37441,38401,22208,22400,38721,21760,38337,38017,21568,
    39937,23744,23936,40257,24320,40897,40577,24128,23040,39617,39809,23360,39169,22976,22656,38977,
    34817,18624,18816,35137,19200,35777,35457,19008,19968,36545,36737,20288,36097,19904,19584,35905,
    17408,33985,34177,17728,34561,18368,18048,34369,33281,17088,17280,33601,16640,33217,32897,16448
};

// ---------------------------------------------------------------------------
// proto_crc16 - compute CRC-16 over len bytes starting at data
// ---------------------------------------------------------------------------
inline uint16_t proto_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ pgm_read_word(&_crcTable[(crc ^ data[i]) & 0xFF]);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// proto_unstuff - remove byte stuffing from raw wire bytes.
//   Rule: byte 0 is kept as-is; every 0xEF 0xEF pair after pos 0 is
//   collapsed to a single 0xEF.
//   Returns number of bytes written to out (out must be >= inLen).
// ---------------------------------------------------------------------------
inline size_t proto_unstuff(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];   // Header byte: always kept

    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        // Collapse duplicate 0xEF
        if (in[i] == M25_HEADER_MARKER && i + 1 < inLen &&
            in[i + 1] == M25_HEADER_MARKER) {
            i++;           // Skip the stuffed duplicate
        }
    }
    return pos;
}

// ---------------------------------------------------------------------------
// proto_stuff - apply byte stuffing to unstuffed bytes.
//   Rule: byte 0 is kept as-is; every 0xEF in positions 1+ is doubled.
//   out must be large enough (worst case: 2 * inLen).
//   Returns number of bytes written to out.
// ---------------------------------------------------------------------------
inline size_t proto_stuff(const uint8_t* in, size_t inLen, uint8_t* out) {
    if (inLen == 0) return 0;
    size_t pos = 0;
    out[pos++] = in[0];   // Header byte: always kept

    for (size_t i = 1; i < inLen; i++) {
        out[pos++] = in[i];
        if (in[i] == M25_HEADER_MARKER) {
            out[pos++] = M25_HEADER_MARKER;   // Double it
        }
    }
    return pos;
}

// ---------------------------------------------------------------------------
// proto_frame_parse - parse a stuffed raw frame into its payload.
//
//   in / inLen    : stuffed wire bytes (as received from transport)
//   payload       : output buffer for the decrypted-ready payload bytes
//                   (between header and CRC, still encrypted)
//   payloadLen    : number of payload bytes written
//
//   Returns true if the frame is structurally valid and CRC correct.
//   On false the caller should discard the frame (stale / corrupt data).
// ---------------------------------------------------------------------------
inline bool proto_frame_parse(const uint8_t* in, size_t inLen,
                               uint8_t* payload, size_t* payloadLen) {
    // Unstuff into a local buffer (max reasonable frame size)
    uint8_t unstuffed[128];
    const size_t uLen = proto_unstuff(in, inLen, unstuffed);

    // Minimum: header(3) + AES_block(16) + CRC(2) = 21 bytes
    if (uLen < M25_HEADER_SIZE + 16 + M25_CRC_SIZE) return false;

    // Check header marker
    if (unstuffed[0] != M25_HEADER_MARKER) return false;

    // Validate CRC (over everything except final 2 bytes)
    const size_t crcOffset = uLen - M25_CRC_SIZE;
    const uint16_t calcCRC  = proto_crc16(unstuffed, crcOffset);
    const uint16_t frameCRC = ((uint16_t)unstuffed[crcOffset] << 8)
                            |            unstuffed[crcOffset + 1];
    if (calcCRC != frameCRC) return false;

    // Extract payload (between the 3-byte header and the 2-byte CRC)
    *payloadLen = crcOffset - M25_HEADER_SIZE;
    memcpy(payload, unstuffed + M25_HEADER_SIZE, *payloadLen);
    return true;
}

// ---------------------------------------------------------------------------
// proto_frame_build - wrap payload in M25 header + CRC, return stuffed frame.
//
//   payload / payloadLen : raw (encrypted) payload bytes
//   out                  : output buffer (must be >= 2 * (payloadLen + 5))
//
//   Returns number of stuffed bytes written to out.
// ---------------------------------------------------------------------------
inline size_t proto_frame_build(const uint8_t* payload, size_t payloadLen,
                                 uint8_t* out) {
    // 1. Build unstuffed frame: [0xEF][lenH][lenL][payload][crcH][crcL]
    uint8_t frame[128];
    const uint16_t frameLen = (uint16_t)(M25_HEADER_SIZE + payloadLen + M25_CRC_SIZE - 1);

    frame[0] = M25_HEADER_MARKER;
    frame[1] = (frameLen >> 8) & 0xFF;
    frame[2] =  frameLen       & 0xFF;
    memcpy(frame + M25_HEADER_SIZE, payload, payloadLen);

    const size_t   rawLen = M25_HEADER_SIZE + payloadLen;
    const uint16_t crc    = proto_crc16(frame, rawLen);
    frame[rawLen]     = (crc >> 8) & 0xFF;
    frame[rawLen + 1] =  crc       & 0xFF;

    // 2. Byte-stuff and write to out
    return proto_stuff(frame, rawLen + M25_CRC_SIZE, out);
}

// ---------------------------------------------------------------------------
// proto_print_hex - debug helper: print hex dump to Serial
// ---------------------------------------------------------------------------
inline void proto_print_hex(const char* label, const uint8_t* data, size_t len) {
    Serial.print(label);
    Serial.print(": ");
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
}

#endif // PROTOCOL_H
