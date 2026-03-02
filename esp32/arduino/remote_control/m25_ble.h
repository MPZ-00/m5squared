/*
 * m25_ble.h - BLE client for Alber e-motion M25 wheels
 *
 * Declares the complete M25 SPP-over-BLE communication stack:
 *   - AES-128-CBC encryption (IV encrypted via ECB, per m25_crypto.py)
 *   - CRC-16 frame checksum (per m25_protocol.py)
 *   - Byte stuffing of 0xEF markers (add_delimiters in m25_protocol.py)
 *   - SPP packet builder (per m25_spp.py PacketBuilder)
 *   - BLE GATT client connection to both wheels
 *   - Response parsing with ACK/NACK handling
 *
 * Protocol constants match m25_protocol_data.py exactly.
 *
 * Connection sequence (m25_parking.py):
 *   1. BLE GATT connect
 *   2. WRITE_SYSTEM_MODE(0x01)  -> init communication
 *   3. WRITE_DRIVE_MODE(0x04)   -> enable remote control bit
 *   4. WRITE_REMOTE_SPEED(spd)  -> every 50 ms while operating
 *   5. WRITE_REMOTE_SPEED(0) + WRITE_DRIVE_MODE(0x00) on disconnect
 *
 * Speed sign convention (m25_parking.py left_wheel_speed = -actual_left):
 *   LEFT  wheel: send NEGATED speed (wheels face opposite directions)
 *   RIGHT wheel: send AS-IS speed
 */

#ifndef M25_BLE_H
#define M25_BLE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <mbedtls/aes.h>
#include <esp_system.h>     // esp_fill_random()
#include <stddef.h>         // offsetof()
#include "device_config.h"

// External debug flags (defined in serial_commands.h)
extern uint8_t debugFlags;

// ---------------------------------------------------------------------------
// M25 SPP Service / Characteristic UUIDs (m25_bluetooth.py)
// ---------------------------------------------------------------------------
#define M25_SPP_SERVICE_UUID  "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_TX_UUID      "00001101-0000-1000-8000-00805F9B34FB"
#define M25_CHAR_RX_UUID      "00001102-0000-1000-8000-00805F9B34FB"

// ---------------------------------------------------------------------------
// Protocol constants (m25_protocol.py / m25_protocol_data.py)
// ---------------------------------------------------------------------------
#define M25_HEADER_MARKER     0xEF
#define M25_HEADER_SIZE       3     // [0xEF, len_hi, len_lo]
#define M25_CRC_SIZE          2

// SPP header fixed field values
#define M25_PROTOCOL_ID_STANDARD    0x01
#define M25_SRC_SMARTPHONE          0x05
#define M25_DEST_WHEEL_COMMON       0x01

// Starting telegram ID (DEFAULT_TELEGRAM_ID in m25_protocol_data.py = -128 = 0x80)
#define M25_TELEGRAM_ID_START       0x80

// Service ID for all remote commands
#define M25_SRV_APP_MGMT            0x01

// Parameter IDs (APP_MGMT service, m25_protocol_data.py)
#define M25_PARAM_WRITE_SYSTEM_MODE   0x10
#define M25_PARAM_WRITE_DRIVE_MODE    0x20
#define M25_PARAM_WRITE_REMOTE_SPEED  0x30
#define M25_PARAM_WRITE_ASSIST_LEVEL  0x40

// Parameter IDs - Read commands (APP_MGMT)
#define M25_PARAM_READ_ASSIST_LEVEL   0x42
#define M25_PARAM_READ_DRIVE_MODE     0x22
#define M25_PARAM_READ_CRUISE_VALUES  0xD1

// Parameter IDs - Status responses (APP_MGMT)
#define M25_PARAM_STATUS_ASSIST_LEVEL 0x41
#define M25_PARAM_STATUS_DRIVE_MODE   0x21
#define M25_PARAM_CRUISE_VALUES       0xD0

// Parameter IDs - Battery (BATT_MGMT service 0x08)
#define M25_PARAM_READ_SOC            0x01
#define M25_PARAM_STATUS_SOC          0x00

// Parameter IDs - Version (VERSION_MGMT service 0x0A)
#define M25_PARAM_READ_SW_VERSION     0x01
#define M25_PARAM_STATUS_SW_VERSION   0x00

// Service IDs
#define M25_SRV_BATT_MGMT             0x08
#define M25_SRV_VERSION_MGMT          0x0A

// ACK/NACK response codes
#define M25_PARAM_ACK                 0xFF
#define M25_NACK_GENERAL              0x80  // General error
#define M25_NACK_SID                  0x81  // Invalid service ID
#define M25_NACK_PID                  0x82  // Invalid parameter ID
#define M25_NACK_LENGTH               0x83  // Invalid length
#define M25_NACK_CHKSUM               0x84  // Checksum error
#define M25_NACK_COND                 0x85  // Condition not met
#define M25_NACK_SEC_ACC              0x86  // Security/access denied
#define M25_NACK_CMD_NOT_EXEC         0x87  // Command not executed
#define M25_NACK_CMD_INTERNAL_ERROR   0x88  // Internal error

// Drive mode bit flags (DRIVE_MODE_BIT_* in m25_protocol_data.py)
#define M25_DRIVE_MODE_NORMAL     0x00
#define M25_DRIVE_MODE_AUTO_HOLD  0x01   // hill hold
#define M25_DRIVE_MODE_CRUISE     0x02
#define M25_DRIVE_MODE_REMOTE     0x04   // remote control

// System mode values
#define M25_SYSTEM_MODE_CONNECT   0x01
#define M25_SYSTEM_MODE_STANDBY   0x02

// Assist levels sent to wheel: ASSIST_LEVEL_1=0, _2=1, _3=2 (m25_protocol_data.py)
// Mapping: ASSIST_INDOOR -> 0, ASSIST_OUTDOOR -> 1, ASSIST_LEARNING -> 2
static const uint8_t M25_ASSIST_LEVEL_MAP[ASSIST_COUNT] = { 0, 1, 2 };

// Speed scaling: percent (-100..+100) to M25 raw signed int16 units.
// SPEED_FAST in m25_parking.py is ~250.  100 % -> 250 raw.
#define M25_SPEED_SCALE  2.5f

// Reconnect retry interval
#define BLE_RECONNECT_DELAY_MS    5000
// Stop auto-reconnect after this many consecutive failures per wheel
#define BLE_MAX_RECONNECT_FAILS   5

// ---------------------------------------------------------------------------
// Wheel slot indices
// ---------------------------------------------------------------------------
#define WHEEL_LEFT  0
#define WHEEL_RIGHT 1
#define WHEEL_COUNT 2

// ---------------------------------------------------------------------------
// CRC-16 lookup table (m25_protocol.py CRC_TABLE, init value 0xFFFF)
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
// Response parsing structures (forward declarations for function signatures)
// ---------------------------------------------------------------------------

// SPP packet header (6 bytes minimum)
struct ResponseHeader {
    uint8_t protocolId;  // Protocol version (0x01 = standard)
    uint8_t telegramId;  // Sequence number
    uint8_t sourceId;    // Source device ID
    uint8_t destId;      // Destination device ID
    uint8_t serviceId;   // Service/subsystem ID
    uint8_t paramId;     // Parameter/command ID
    const uint8_t* payload;  // Pointer to payload start
    size_t payloadLen;   // Payload length in bytes
};

// Parsed response data (union for different types)
struct ResponseData {
    bool isAck;
    bool isNack;
    uint8_t nackCode;  // Only valid if isNack=true
    
    union {
        struct {
            uint8_t batteryPercent;
        } soc;
        
        struct {
            uint8_t level;  // 0=indoor, 1=outdoor, 2=learning
        } assistLevel;
        
        struct {
            uint8_t mode;
            bool autoHold;
            bool cruise;
            bool remote;
        } driveMode;
        
        struct {
            uint8_t devState;
            uint8_t major;
            uint8_t minor;
            uint8_t patch;
        } swVersion;
        
        struct {
            uint32_t distanceMm;     // Overall distance in 0.01m
            float distanceKm;        // Converted to km
            uint16_t speed;          // Current speed
            uint16_t pushCounter;    // Push count
        } cruiseValues;
    };
};

// ---------------------------------------------------------------------------
// Type-safe payload parsing helpers
// ---------------------------------------------------------------------------

static inline uint8_t _parseUint8(const uint8_t* payload, size_t offset) {
    return payload[offset];
}

static inline int16_t _parseInt16BE(const uint8_t* payload, size_t offset) {
    return (int16_t)(((uint16_t)payload[offset] << 8) | payload[offset + 1]);
}

static inline uint16_t _parseUint16BE(const uint8_t* payload, size_t offset) {
    return ((uint16_t)payload[offset] << 8) | payload[offset + 1];
}

static inline uint32_t _parseUint32BE(const uint8_t* payload, size_t offset) {
    return ((uint32_t)payload[offset] << 24) |
           ((uint32_t)payload[offset + 1] << 16) |
           ((uint32_t)payload[offset + 2] << 8) |
           (uint32_t)payload[offset + 3];
}

static inline int32_t _parseInt32BE(const uint8_t* payload, size_t offset) {
    return (int32_t)_parseUint32BE(payload, offset);
}

// ---------------------------------------------------------------------------
// Per-wheel connection state
// ---------------------------------------------------------------------------
struct WheelConnState_t {
    char                         mac[18];   // runtime-mutable "XX:XX:XX:XX:XX:XX\0"
    const char*                  name;
    uint8_t                      key[16];   // runtime-mutable AES-128 key
    bool                         connected;
    bool                         protocolReady;      // SYSTEM_MODE + DRIVE_MODE acked
    uint8_t                      telegramId;         // SPP sequence counter
    uint8_t                      driveModeBits;      // current DRIVE_MODE byte
    BLEClient*                   client;
    BLERemoteCharacteristic*     rxChar;             // For writing commands to wheel
    BLERemoteCharacteristic*     txChar;             // For receiving responses from wheel
    bool                         receivedFirstAck;   // Track if we got a response (encryption validated)
    uint32_t                     lastConnectAttemptMs;
    uint8_t                      consecutiveFails;   // resets on success; auto-reconnect stops at BLE_MAX_RECONNECT_FAILS
};

// ---------------------------------------------------------------------------
// BLE disconnect callback
// ---------------------------------------------------------------------------
class M25DisconnectCallback : public BLEClientCallbacks {
public:
    uint8_t wheelIdx;
    void onConnect(BLEClient*) override;
    void onDisconnect(BLEClient*) override;
};

// ---------------------------------------------------------------------------
// Compile-time default keys (copied into mutable _wheels storage by bleInit)
// ---------------------------------------------------------------------------
static const uint8_t _keyDefaultLeft[16]  = ENCRYPTION_KEY_LEFT;
static const uint8_t _keyDefaultRight[16] = ENCRYPTION_KEY_RIGHT;

// ---------------------------------------------------------------------------
// Internal storage accessors (defined in m25_ble.cpp)
// ---------------------------------------------------------------------------
WheelConnState_t* _getWheels();
bool& _getBleAutoReconnect();

#define _wheels (_getWheels())
#define _bleAutoReconnect (_getBleAutoReconnect())

// ---------------------------------------------------------------------------
// Internal function declarations
// ---------------------------------------------------------------------------

// CRC-16 calculation
uint16_t _m25Crc16(const uint8_t* data, size_t len);

// Byte stuffing / unstuffing
size_t _addDelimiters(const uint8_t* in, size_t inLen, uint8_t* out);
size_t _removeDelimiters(const uint8_t* in, size_t inLen, uint8_t* out, size_t outMax);

// NACK error code interpretation
const char* _nackCodeToString(uint8_t code);
bool _isNack(uint8_t paramId);
bool _isAck(uint8_t paramId);

// Wheel activity filter (respects WHEEL_MODE)
bool _wheelActive(int idx);

// Encryption/decryption
bool _m25Encrypt(const uint8_t* key, const uint8_t* spp, uint8_t sppLen,
                 uint8_t* out, size_t* outLen);
bool _m25Decrypt(const uint8_t* key, const uint8_t* frame, size_t frameLen,
                 uint8_t* sppOut, size_t* sppLen);

// SPP packet building and sending
size_t _buildAndEncrypt(int idx, uint8_t serviceId, uint8_t paramId,
                        const uint8_t* payload, uint8_t payloadLen,
                        uint8_t* out);
bool _sendCommand(int idx, uint8_t serviceId, uint8_t paramId,
                  const uint8_t* payload = nullptr, uint8_t payloadLen = 0);

// Response parsing
bool _parseResponseHeader(const uint8_t* spp, size_t sppLen, ResponseHeader* hdr);
bool _parseResponseData(const ResponseHeader* hdr, ResponseData* data);
void _printResponse(const char* wheelName, const ResponseHeader* hdr, const ResponseData* data);
void _parseSppPacket(const uint8_t* spp, size_t sppLen, const char* wheelName);

// BLE notification callback
void _notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

// Connection management
bool _connectWheel(int idx);

// ---------------------------------------------------------------------------
// Public API declarations
// ---------------------------------------------------------------------------

// Initialization - call once in setup()
void bleInit(const char* deviceName = "M25-Remote");

// Connect to active wheels (call after bleInit)
void bleConnect();

// Disconnect all wheels - stop motors and disable remote mode first
void bleDisconnect();

// Connection status queries
bool bleIsConnected(int wheelIdx);
bool bleAllConnected();  // True when every active wheel is connected and protocol-ready
bool bleAnyConnected();  // True when at least one active wheel is connected

// Motor commands
bool bleSendStop();
bool bleSendMotorCommand(float leftPercent, float rightPercent);

// Hill hold control
bool bleSendHillHold(bool enable);

// Assist level control
bool bleSendAssistLevel(uint8_t level);

// Background tick: attempt reconnect if a wheel dropped out. Call from loop().
void bleTick();

// Auto-reconnect control
void bleSetAutoReconnect(bool enable);
bool bleGetAutoReconnect();

// Runtime MAC address / key override
void bleSetMac(int idx, const char* mac);
void bleSetKey(int idx, const uint8_t* newKey);

// Verbose per-wheel status dump (called by serial 'wheels' command)
void blePrintWheelDetails();

#endif // M25_BLE_H

