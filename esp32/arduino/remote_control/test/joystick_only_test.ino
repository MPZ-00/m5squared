#include <Arduino.h>

// Joystick ADC pins (ESP32 ADC1 -> safe with BLE/WiFi disabled in this sketch)
static const uint8_t PIN_JS_X = 32;
static const uint8_t PIN_JS_Y = 33;

static const uint8_t ADC_BITS = 12;
static const int ADC_MAX = (1 << ADC_BITS) - 1; // 4095

// Deadzone used only for normalized display in this test sketch
static const int DEADZONE = 200;

struct AxisStats {
    int minVal;
    int maxVal;
    uint32_t sampleCount;
    double mean;
    double m2;
};

static int centerX = 0;
static int centerY = 0;
static bool hasCalibration = false;

static uint32_t lastLiveMs = 0;
static const uint32_t LIVE_PERIOD_MS = 120;

static int readAxisAveraged(uint8_t pin, uint8_t samples = 8) {
    long sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / samples);
}

static void axisStatsReset(AxisStats& s) {
    s.minVal = ADC_MAX;
    s.maxVal = 0;
    s.sampleCount = 0;
    s.mean = 0.0;
    s.m2 = 0.0;
}

static void axisStatsAdd(AxisStats& s, int value) {
    if (value < s.minVal) s.minVal = value;
    if (value > s.maxVal) s.maxVal = value;

    s.sampleCount++;
    double delta = value - s.mean;
    s.mean += delta / (double)s.sampleCount;
    double delta2 = value - s.mean;
    s.m2 += delta * delta2;
}

static float axisStatsStdDev(const AxisStats& s) {
    if (s.sampleCount < 2) return 0.0f;
    return sqrt((float)(s.m2 / (double)(s.sampleCount - 1)));
}

static float normalizeAxis(int raw, int center) {
    int offset = raw - center;
    if (abs(offset) <= DEADZONE) {
        return 0.0f;
    }

    float norm;
    if (offset > 0) {
        int edgePos = center + DEADZONE;
        int rangePos = ADC_MAX - edgePos;
        if (rangePos <= 0) return 1.0f;
        norm = (float)(raw - edgePos) / (float)rangePos;
    }
    else {
        int edgeNeg = center - DEADZONE;
        int rangeNeg = edgeNeg;
        if (rangeNeg <= 0) return -1.0f;
        norm = -((float)(edgeNeg - raw) / (float)rangeNeg);
    }

    if (norm > 1.0f) norm = 1.0f;
    if (norm < -1.0f) norm = -1.0f;
    return norm;
}

static const char* classifyDirection(float nx, float ny) {
    const float primary = 0.55f;
    const float secondary = 0.25f;

    if (nx == 0.0f && ny == 0.0f) {
        return "CENTER";
    }

    if (ny >= primary && abs(nx) <= secondary) return "UP";
    if (ny <= -primary && abs(nx) <= secondary) return "DOWN";
    if (nx >= primary && abs(ny) <= secondary) return "RIGHT";
    if (nx <= -primary && abs(ny) <= secondary) return "LEFT";

    if (nx >= secondary && ny >= secondary) return "UP-RIGHT";
    if (nx <= -secondary && ny >= secondary) return "UP-LEFT";
    if (nx >= secondary && ny <= -secondary) return "DOWN-RIGHT";
    if (nx <= -secondary && ny <= -secondary) return "DOWN-LEFT";

    if (abs(ny) >= abs(nx)) {
        return (ny > 0.0f) ? "UP-ish" : "DOWN-ish";
    }
    return (nx > 0.0f) ? "RIGHT-ish" : "LEFT-ish";
}

static void calibrateCenter(uint16_t samples = 64) {
    long sx = 0;
    long sy = 0;

    for (uint16_t i = 0; i < samples; i++) {
        sx += analogRead(PIN_JS_X);
        sy += analogRead(PIN_JS_Y);
        delay(4);
    }

    centerX = (int)(sx / samples);
    centerY = (int)(sy / samples);
    hasCalibration = true;

    Serial.printf("[CAL] centerX=%d centerY=%d (samples=%u)\n", centerX, centerY, samples);

    if (centerX < 100 || centerY < 100) {
        Serial.println("[CAL][WARN] Center is near zero. Check joystick power and GND before calibrating.");
    }
}

static void runExtensiveDiagnostic(uint32_t durationMs = 10000) {
    AxisStats xStats, yStats;
    axisStatsReset(xStats);
    axisStatsReset(yStats);

    Serial.println("[TEST] Extensive diagnostic started.");
    Serial.println("[TEST] Move joystick through full range several times for best min/max values.");

    uint32_t start = millis();
    uint32_t lastPrint = 0;

    while ((millis() - start) < durationMs) {
        int x = readAxisAveraged(PIN_JS_X, 6);
        int y = readAxisAveraged(PIN_JS_Y, 6);
        axisStatsAdd(xStats, x);
        axisStatsAdd(yStats, y);

        if (millis() - lastPrint >= 500) {
            lastPrint = millis();
            Serial.printf("[TEST] t=%lus  X[min=%d max=%d] Y[min=%d max=%d]\n",
                (unsigned long)((millis() - start) / 1000),
                xStats.minVal, xStats.maxVal,
                yStats.minVal, yStats.maxVal);
        }

        delay(10);
    }

    float xStd = axisStatsStdDev(xStats);
    float yStd = axisStatsStdDev(yStats);

    Serial.println("[TEST] Extensive diagnostic done.");
    Serial.printf("[RES] X: min=%d max=%d span=%d mean=%.1f std=%.1f\n",
        xStats.minVal, xStats.maxVal, xStats.maxVal - xStats.minVal,
        (float)xStats.mean, xStd);
    Serial.printf("[RES] Y: min=%d max=%d span=%d mean=%.1f std=%.1f\n",
        yStats.minVal, yStats.maxVal, yStats.maxVal - yStats.minVal,
        (float)yStats.mean, yStd);

    if (xStats.maxVal < 200 || yStats.maxVal < 200) {
        Serial.println("[RES][WARN] Axis range too low. ADC pin may be floating or pulled to GND.");
    }
    if ((xStats.maxVal - xStats.minVal) < 500 || (yStats.maxVal - yStats.minVal) < 500) {
        Serial.println("[RES][WARN] Low movement span. Check divider values, joystick wiring, or module pin order.");
    }
}

static void printHelp() {
    Serial.println();
    Serial.println("Joystick-only test commands:");
    Serial.println("  h                -> help");
    Serial.println("  c                -> calibrate center now (joystick untouched)");
    Serial.println("  e                -> extensive 10s diagnostic");
    Serial.println("  e <seconds>      -> extensive diagnostic for custom duration");
    Serial.println("  r                -> one immediate raw snapshot");
    Serial.println();
}

static void handleSerialCommands() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    if (line == "h" || line == "help" || line == "?") {
        printHelp();
        return;
    }

    if (line == "c" || line == "cal") {
        calibrateCenter(64);
        return;
    }

    if (line == "r") {
        int x = readAxisAveraged(PIN_JS_X, 8);
        int y = readAxisAveraged(PIN_JS_Y, 8);
        if (!hasCalibration) {
            Serial.printf("[RAW] X=%d Y=%d (not calibrated)\n", x, y);
        }
        else {
            float nx = normalizeAxis(x, centerX);
            float ny = normalizeAxis(y, centerY);
            Serial.printf("[RAW] X=%d Y=%d | ctrX=%d ctrY=%d | normX=%+.3f normY=%+.3f\n",
                x, y, centerX, centerY, nx, ny);
        }
        return;
    }

    if (line.startsWith("e")) {
        uint32_t sec = 10;
        if (line.length() > 1) {
            sec = (uint32_t)line.substring(1).toInt();
            if (sec == 0) sec = 10;
        }
        runExtensiveDiagnostic(sec * 1000UL);
        return;
    }

    Serial.printf("[CMD] Unknown: %s\n", line.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    analogReadResolution(ADC_BITS);
    analogSetPinAttenuation(PIN_JS_X, ADC_11db);
    analogSetPinAttenuation(PIN_JS_Y, ADC_11db);

    Serial.println("Joystick-only test sketch started.");
    Serial.printf("Pins: X=%u Y=%u\n", PIN_JS_X, PIN_JS_Y);
    Serial.println("Keep joystick untouched for startup calibration...");

    calibrateCenter(64);
    printHelp();
}

void loop() {
    handleSerialCommands();

    uint32_t now = millis();
    if ((now - lastLiveMs) >= LIVE_PERIOD_MS) {
        lastLiveMs = now;

        int x = readAxisAveraged(PIN_JS_X, 8);
        int y = readAxisAveraged(PIN_JS_Y, 8);

        if (!hasCalibration) {
            Serial.printf("[LIVE] rawX=%4d rawY=%4d\n", x, y);
            return;
        }

        float nx = normalizeAxis(x, centerX);
        float ny = normalizeAxis(y, centerY);
        bool inDz = (nx == 0.0f && ny == 0.0f);
        const char* dir = classifyDirection(nx, ny);

        Serial.printf("[LIVE] rawX=%4d rawY=%4d ctrX=%4d ctrY=%4d normX=%+.3f normY=%+.3f dz=%s dir=%s\n",
            x, y, centerX, centerY, nx, ny, inDz ? "yes" : "no", dir);
    }
}
