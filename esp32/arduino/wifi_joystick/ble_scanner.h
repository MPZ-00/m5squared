#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

// Declared in main sketch
extern String selectedWheelMAC;
extern DiscoveredWheel discoveredWheels[];
extern int discoveredWheelCount;
extern bool bleScanning;
extern unsigned long scanStartTime;
extern bool bleConnected;
extern BLEClient* pClient;
extern BLERemoteCharacteristic* pTxCharacteristic;
extern BLERemoteCharacteristic* pRxCharacteristic;
extern BLEAdvertisedDevice* targetDevice;

// BLE Scan callback
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String deviceMAC = advertisedDevice.getAddress().toString().c_str();
        deviceMAC.toUpperCase();
        String deviceName = advertisedDevice.getName().c_str();
        int rssi = advertisedDevice.getRSSI();
        
        // Check if this looks like an M25 wheel (contains "M5" or "M25" in name)
        bool isM25Wheel = (deviceName.indexOf("M5") >= 0 || 
                          deviceName.indexOf("M25") >= 0 ||
                          deviceName.indexOf("Wheel") >= 0);
        
        // Also check against configured wheel MACs
        String leftMAC = String(LEFT_WHEEL_MAC);
        String rightMAC = String(RIGHT_WHEEL_MAC);
        leftMAC.toUpperCase();
        rightMAC.toUpperCase();
        
        if (deviceMAC == leftMAC || deviceMAC == rightMAC) {
            isM25Wheel = true;
        }
        
        if (isM25Wheel) {
            // Add to discovered wheels list if not already present
            bool alreadyFound = false;
            for (int i = 0; i < discoveredWheelCount; i++) {
                if (discoveredWheels[i].mac == deviceMAC) {
                    alreadyFound = true;
                    // Update RSSI
                    discoveredWheels[i].rssi = rssi;
                    break;
                }
            }
            
            if (!alreadyFound && discoveredWheelCount < MAX_DISCOVERED_WHEELS) {
                discoveredWheels[discoveredWheelCount].mac = deviceMAC;
                discoveredWheels[discoveredWheelCount].name = deviceName.length() > 0 ? deviceName : "Unknown";
                discoveredWheels[discoveredWheelCount].rssi = rssi;
                discoveredWheels[discoveredWheelCount].valid = true;
                discoveredWheelCount++;
                Serial.println("Found M25 wheel: " + deviceMAC + " (" + deviceName + ") RSSI: " + String(rssi));
            }
            
            // If this is the target device for auto-connect, save it
            if (selectedWheelMAC.length() > 0 && deviceMAC == selectedWheelMAC) {
                targetDevice = new BLEAdvertisedDevice(advertisedDevice);
            }
        }
    }
};

// BLE Client callbacks
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("BLE Connected!");
        bleConnected = true;
        sendBLEStatus();
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("BLE Disconnected!");
        bleConnected = false;
        sendBLEStatus();
        
        if (pClient != nullptr) {
            delete pClient;
            pClient = nullptr;
        }
    }
};

// Start scanning for wheels
void startWheelScan() {
    if (bleScanning) {
        Serial.println("[BLE] Already scanning");
        return;
    }
    
    // Clear previous results
    discoveredWheelCount = 0;
    for (int i = 0; i < MAX_DISCOVERED_WHEELS; i++) {
        discoveredWheels[i].valid = false;
    }
    
    Serial.println("[BLE] Starting wheel scan...");
    bleScanning = true;
    scanStartTime = millis();
    
    // Notify clients that scanning started
    String json = "{\"scanning\":true}";
    webSocket.broadcastTXT(json);
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(BLE_SCAN_TIME, true);
}

// Connect to specific wheel by MAC address
void connectToWheel(String mac) {
    if (bleConnected) {
        Serial.println("[BLE] Already connected, disconnecting first...");
        disconnectBLE();
        delay(1000);
    }
    
    selectedWheelMAC = mac;
    mac.toUpperCase();
    Serial.println("[BLE] Connecting to wheel: " + mac);
    
    // Check if we already discovered this device
    bool foundInCache = false;
    for (int i = 0; i < discoveredWheelCount; i++) {
        if (discoveredWheels[i].valid && discoveredWheels[i].mac == mac) {
            foundInCache = true;
            Serial.println("[BLE] Using cached device info");
            break;
        }
    }
    
    if (!foundInCache) {
        Serial.println("[BLE] Device not in cache, scanning...");
        startWheelScan();
    } else {
        connectToBLE();
    }
}

// Connect to BLE wheel
void connectToBLE() {
    if (bleConnected || bleScanning) {
        return;
    }
    
    // Use selected wheel MAC or fallback to default
    String targetMAC = selectedWheelMAC.length() > 0 ? selectedWheelMAC : String(TARGET_WHEEL_MAC);
    targetMAC.toUpperCase();
    
    Serial.println("\n[BLE] Starting scan for wheel: " + targetMAC);
    bleScanning = true;
    scanStartTime = millis();
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(BLE_SCAN_TIME, true);
    
    // Send discovered wheels to clients
    sendDiscoveredWheels();
    
    if (targetDevice == nullptr) {
        Serial.println("[BLE] Wheel not found!");
        return;
    }
    
    Serial.println("[BLE] Connecting to wheel...");
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    if (!pClient->connect(targetDevice)) {
        Serial.println("[BLE] Connection failed!");
        delete pClient;
        pClient = nullptr;
        delete targetDevice;
        targetDevice = nullptr;
        return;
    }
    
    Serial.println("[BLE] Connected! Getting service...");
    
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService == nullptr) {
        Serial.println("[BLE] Service not found!");
        pClient->disconnect();
        return;
    }
    
    Serial.println("[BLE] Getting characteristics...");
    
    pTxCharacteristic = pRemoteService->getCharacteristic(CHAR_UUID_TX);
    pRxCharacteristic = pRemoteService->getCharacteristic(CHAR_UUID_RX);
    
    if (pTxCharacteristic == nullptr || pRxCharacteristic == nullptr) {
        Serial.println("[BLE] Characteristics not found!");
        pClient->disconnect();
        return;
    }
    
    Serial.println("[BLE] Setup complete!");
    bleConnected = true;
    sendBLEStatus();
}

// Disconnect from BLE
void disconnectBLE() {
    if (pClient != nullptr && bleConnected) {
        pClient->disconnect();
    }
    bleConnected = false;
}

#endif
