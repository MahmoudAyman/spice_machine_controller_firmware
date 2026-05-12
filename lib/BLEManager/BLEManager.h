#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// UUIDs (Generated for this project)
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define COMMAND_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_CHAR_UUID       "d670f5e7-3f36-4c9c-b1cc-1365532587f1"
#define SYNC_CHAR_UUID         "672a952d-8889-4824-9118-2e0e09252c84"

class BLEManager : public BLECharacteristicCallbacks {
public:
    BLEManager();
    void begin(const char* deviceName);
    void tick();
    
    // Status reporting
    void notifyStatus(JsonDocument& doc);
    void notifyLevels(JsonDocument& doc);
    void sendAlert(const char* code, int slot);
    
    bool isConnected() { return _deviceConnected; }

    // Callbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;

private:
    BLEServer* _pServer;
    BLECharacteristic* _pCommandChar;
    BLECharacteristic* _pStatusChar;
    BLECharacteristic* _pSyncChar;
    
    bool _deviceConnected = false;
    bool _oldDeviceConnected = false;
    unsigned long _disconnectAtMs = 0;
    
    class ServerCallbacks : public BLEServerCallbacks {
        BLEManager* _parent;
    public:
        ServerCallbacks(BLEManager* parent) : _parent(parent) {}
        void onConnect(BLEServer* pServer) override { _parent->_deviceConnected = true; }
        void onDisconnect(BLEServer* pServer) override { _parent->_deviceConnected = false; _parent->_disconnectAtMs = millis(); }
    };
};

#endif
