#include "BLEManager.h"
#include "Globals.h"
#include "Hardware.h"

BLEManager::BLEManager() : _pServer(NULL), _pCommandChar(NULL), _pStatusChar(NULL), _pSyncChar(NULL) {}

void BLEManager::begin(const char* deviceName) {
    BLEDevice::init(deviceName);
    _pServer = BLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(this));

    BLEService *pService = _pServer->createService(SERVICE_UUID);

    // Command Characteristic (Write)
    _pCommandChar = pService->createCharacteristic(
        COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    _pCommandChar->setCallbacks(this);

    // Status Characteristic (Notify)
    _pStatusChar = pService->createCharacteristic(
        STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _pStatusChar->addDescriptor(new BLE2902());

    // Sync Characteristic (Read/Write/Notify)
    _pSyncChar = pService->createCharacteristic(
        SYNC_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    _pSyncChar->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.printf("BLE Started: %s\n", deviceName);
}

void BLEManager::tick() {
    if (!_deviceConnected && _oldDeviceConnected) {
        if (millis() - _disconnectAtMs >= 500) {
            _pServer->startAdvertising();
            Serial.println("Re-advertising...");
            _oldDeviceConnected = _deviceConnected;
        }
    }
    if (_deviceConnected && !_oldDeviceConnected) {
        _oldDeviceConnected = _deviceConnected;
        Serial.println("Device connected.");
    }
}

void BLEManager::onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
        Serial.printf("BLE Received: %s\n", value.c_str());
        
        // Use a larger document for complex recipes
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        const char* type = doc["type"];
        if (!type) return;

        // HIGH PRIORITY: Abort
        if (strcmp(type, "abort") == 0) {
            Serial.println("BLE: ABORT COMMAND");
            abortTriggered = true; 
            
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "abort";
            ack["status"] = "success";
            notifyStatus(ack);
            return;
        } 

        if (strcmp(type, "handshake") == 0) {
            Serial.println("BLE: HANDSHAKE");
            const char* uuid = doc["uuid"];
            int version = doc["version"];
            Serial.printf("App UUID: %s, Version: %d\n", uuid ? uuid : "null", version);
            
            bool exists = false;
            if (uuid) {
                exists = checkProfile(String(uuid));
            }

            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "handshake";
            ack["status"] = "success";
            ack["new_device"] = !exists;
            notifyStatus(ack);
        }
        else if (strcmp(type, "ping") == 0) {
            Serial.println("BLE: PING");
            JsonDocument pong;
            pong["type"] = "pong";
            pong["timestamp"] = millis();
            notifyStatus(pong);
        }
        else if (strcmp(type, "get_status") == 0) {
            Serial.println("BLE: GET_STATUS");
            sendBleStatus();
        }
        else if (strcmp(type, "get_levels") == 0) {
            Serial.println("BLE: GET_LEVELS");
            JsonDocument levels;
            levels["type"] = "levels";
            JsonArray data = levels["data"].to<JsonArray>();
            for (int i = 0; i < NUM_SPICES; i++) {
                JsonObject entry = data.add<JsonObject>();
                entry["slot"] = i + 1;
                entry["name"] = spices[i].name;
                entry["level"] = spices[i].level;
            }
            notifyLevels(levels);
        }
        else if (strcmp(type, "toggle_sim") == 0) {
            simulationEnabled = !simulationEnabled;
            Serial.printf("BLE: TOGGLE_SIM -> %s\n", simulationEnabled ? "ENABLED" : "DISABLED");
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "toggle_sim";
            ack["status"] = "success";
            ack["enabled"] = simulationEnabled;
            notifyStatus(ack);
        }
        else if (strcmp(type, "dispense") == 0) {
            Serial.println("BLE: DISPENSE COMMAND");
            if (currentState != STATE_MAIN_MENU) {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "dispense";
                nak["status"] = "fail";
                nak["reason"] = "busy";
                notifyStatus(nak);
                return;
            }

            JsonArray items = doc["items"];
            if (items.isNull() || items.size() == 0) {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "dispense";
                nak["status"] = "fail";
                nak["reason"] = "empty_recipe";
                notifyStatus(nak);
                return;
            }

            remoteRecipe.name = "Remote Recipe";
            remoteRecipe.ingredientCount = items.size();
            for (int i = 0; i < remoteRecipe.ingredientCount && i < 10; i++) {
                remoteRecipe.ingredients[i].spiceIndex = items[i]["slot"].as<int>() - 1;
                remoteRecipe.ingredients[i].quantityGrams = items[i]["grams"].as<float>();
            }
            remoteRequestTriggered = true;

            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "dispense";
            ack["status"] = "success";
            notifyStatus(ack);
        }
        else if (strcmp(type, "update_slot") == 0) {
            Serial.println("BLE: UPDATE_SLOT");
            int slot = doc["slot"];
            const char* name = doc["name"];
            if (slot >= 1 && slot <= NUM_SPICES && name) {
                Serial.printf("Updating slot %d to %s\n", slot, name);
                spices[slot - 1].name = String(name);
                saveDatabase();
                
                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "update_slot";
                ack["status"] = "success";
                notifyStatus(ack);
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "update_slot";
                nak["status"] = "fail";
                notifyStatus(nak);
            }
        }
        else if (strcmp(type, "refill") == 0) {
            Serial.println("BLE: REFILL");
            int slot = doc["slot"];
            if (slot >= 1 && slot <= NUM_SPICES) {
                Serial.printf("Refilling slot %d\n", slot);
                spices[slot - 1].level = 100;
                saveDatabase();
                
                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "refill";
                ack["status"] = "success";
                notifyStatus(ack);
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "refill";
                nak["status"] = "fail";
                notifyStatus(nak);
            }
        }
        else {
            Serial.printf("Unknown command type: %s\n", type);
        }
    }
}

void BLEManager::notifyStatus(JsonDocument& doc) {
    if (_deviceConnected) {
        char buffer[1024];
        serializeJson(doc, buffer);
        _pStatusChar->setValue(buffer);
        _pStatusChar->notify();
    }
}

void BLEManager::notifyLevels(JsonDocument& doc) {
    if (_deviceConnected) {
        char buffer[1024];
        serializeJson(doc, buffer);
        _pSyncChar->setValue(buffer);
        _pSyncChar->notify();
    }
}

void BLEManager::sendAlert(const char* code, int slot) {
    if (_deviceConnected) {
        JsonDocument doc;
        doc["type"] = "alert";
        doc["code"] = code;
        doc["slot"] = slot;
        notifyStatus(doc);
    }
}
