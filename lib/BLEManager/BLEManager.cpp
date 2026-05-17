#include "BLEManager.h"
#include "Globals.h"
#include "Hardware.h"
#include "Database.h"

// --- Server Callbacks Implementation ---
void BLEManager::ServerCallbacks::onConnect(BLEServer* pServer) {
    _parent->_deviceConnected = true;
}

void BLEManager::ServerCallbacks::onDisconnect(BLEServer* pServer) {
    _parent->_deviceConnected = false; 
    _parent->_disconnectAtMs = millis(); 
    
    // Save the profile when the app disconnects
    if (activeProfileUUID != "") {
        saveProfile();
    }
    // We no longer fallback to a default profile. The machine continues 
    // using the last active profile for physical keypad usage, while
    // spice levels remain perfectly in sync globally.
}

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
    BLEDevice::setMTU(512); 
    Serial.printf("BLE Started: %s (MTU 512)\n", deviceName);
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
        _rxBuffer += value;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, _rxBuffer);

        if (error == DeserializationError::IncompleteInput) {
            // Standard BLE fragmentation. Wait for the rest of the payload.
            return;
        }

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            _rxBuffer.clear(); // Clear buffer to recover from corrupt data
            return;
        }

        Serial.printf("BLE Command Parsed: %s\n", _rxBuffer.c_str());
        _rxBuffer.clear(); // Successfully parsed, reset buffer for next command

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
            
            // Ensure global spices are loaded (in case app forces a re-sync)
            loadGlobalSpices();

            if (uuid) {
                String uuidStr = String(uuid);
                loadProfile(uuidStr);
            }

            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "handshake";
            ack["status"] = isMachineConfigured ? "success" : "unconfigured";
            ack["new_device"] = !isMachineConfigured;
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
            Serial.println("BLE: GET_LEVELS (Key-Value)");
            // Using a simple key-value object to minimize payload size.
            // Example: {"type":"levels", "data":{"1":100, "2":85, ...}}
            JsonDocument levels;
            levels["type"] = "levels";
            JsonObject data = levels["data"].to<JsonObject>();
            for (int i = 0; i < NUM_SPICES; i++) {
                data[String(i + 1)] = spices[i].level;
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

            if (doc["recipe_id"].is<const char*>()) {
                String rId = doc["recipe_id"].as<String>();
                bool found = false;
                for (int i = 0; i < activeRecipeCount; i++) {
                    if (recipes[i].id == rId) {
                        remoteRecipe = recipes[i];
                        found = true;
                        Serial.printf("[DEBUG] Loading recipe '%s' from profile.\n", remoteRecipe.name.c_str());
                        break;
                    }
                }
                
                if (!found) {
                    JsonDocument nak;
                    nak["type"] = "ack";
                    nak["command"] = "dispense";
                    nak["status"] = "fail";
                    nak["reason"] = "invalid_recipe_id";
                    notifyStatus(nak);
                    return;
                }
            } else if (doc["items"].is<JsonArray>()) {
                JsonArray items = doc["items"];
                if (items.isNull() || items.size() == 0) {
                    JsonDocument nak;
                    nak["type"] = "ack";
                    nak["command"] = "dispense";
                    nak["status"] = "fail";
                    nak["reason"] = "empty_items";
                    notifyStatus(nak);
                    return;
                }
                remoteRecipe.name = "Remote Custom";
                remoteRecipe.ingredientCount = items.size();
                for (int i = 0; i < remoteRecipe.ingredientCount && i < 10; i++) {
                    remoteRecipe.ingredients[i].spiceIndex = items[i]["slot"].as<int>() - 1;
                    remoteRecipe.ingredients[i].quantityGrams = items[i]["grams"].as<float>();
                }
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "dispense";
                nak["status"] = "fail";
                nak["reason"] = "missing_data";
                notifyStatus(nak);
                return;
            }

            remoteRequestTriggered = true;
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "dispense";
            ack["status"] = "success";
            notifyStatus(ack);
        }
        else if (strcmp(type, "sync_recipes") == 0) {
            Serial.println("BLE: SYNC_RECIPES");
            JsonArray recipesArr = doc["recipes"];
            if (!recipesArr.isNull()) {
                activeRecipeCount = 0;
                for (JsonObject rObj : recipesArr) {
                    if (activeRecipeCount >= MAX_RECIPES) break;
                    String fallbackId = "temp_" + String(activeRecipeCount);
                    recipes[activeRecipeCount].id = rObj["id"] | fallbackId;
                    recipes[activeRecipeCount].name = rObj["name"].as<String>();
                    JsonArray ingredientsArr = rObj["ingredients"];
                    recipes[activeRecipeCount].ingredientCount = 0;
                    for (JsonObject iObj : ingredientsArr) {
                        if (recipes[activeRecipeCount].ingredientCount >= 10) break;
                        int idx = recipes[activeRecipeCount].ingredientCount;
                        recipes[activeRecipeCount].ingredients[idx].spiceIndex = iObj["slot"].as<int>() - 1;
                        recipes[activeRecipeCount].ingredients[idx].quantityGrams = iObj["grams"].as<float>();
                        recipes[activeRecipeCount].ingredientCount++;
                    }
                    activeRecipeCount++;
                }
                saveProfile();
                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "sync_recipes";
                ack["status"] = "success";
                ack["count"] = activeRecipeCount;
                notifyStatus(ack);
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "sync_recipes";
                nak["status"] = "fail";
                notifyStatus(nak);
            }
        }
        else if (strcmp(type, "update_slot") == 0) {
            Serial.println("BLE: UPDATE_SLOT");
            int slot = doc["slot"];
            const char* name = doc["name"];
            if (slot >= 1 && slot <= NUM_SPICES && name) {
                Serial.printf("[DEBUG] Updating slot %d to %s. Saving to active profile.\n", slot, name);
                spices[slot - 1].name = String(name);
                saveProfile();
                
                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "update_slot";
                ack["status"] = "success";
                notifyStatus(ack);
            } else {
                Serial.printf("[ERROR] Failed to update slot. Invalid parameters: slot=%d, name=%s\n", slot, name ? name : "null");
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
                Serial.printf("[DEBUG] Refilling slot %d to 100%%. Saving to GLOBAL profile.\n", slot);
                spices[slot - 1].level = 100;
                saveGlobalSpices();

                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "refill";
                ack["status"] = "success";
                notifyStatus(ack);
            } else {
                Serial.printf("[ERROR] Failed to refill slot. Invalid slot: %d\n", slot);
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "refill";
                nak["status"] = "fail";
                notifyStatus(nak);
            }
        }
        else if (strcmp(type, "print_profile") == 0) {
            Serial.println("--- CURRENT PROFILE DEBUG ---");
            Serial.printf("Active UUID: %s\n", activeProfileUUID.c_str());
            Serial.printf("Total Recipes: %d\n", activeRecipeCount);
            for(int i = 0; i < activeRecipeCount; i++) {
                Serial.printf("[%d] ID: %s | Name: %s | Items: %d\n", i+1, recipes[i].id.c_str(), recipes[i].name.c_str(), recipes[i].ingredientCount);
                for(int j = 0; j < recipes[i].ingredientCount; j++) {
                    Serial.printf("  - Slot %d: %.1fg\n", recipes[i].ingredients[j].spiceIndex + 1, recipes[i].ingredients[j].quantityGrams);
                }
            }
            Serial.println("-----------------------------");
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "print_profile";
            ack["status"] = "success";
            notifyStatus(ack);
        }
        else if (strcmp(type, "factory_reset") == 0) {
            Serial.println("BLE: FACTORY RESET");
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "factory_reset";
            ack["status"] = "success";
            notifyStatus(ack);
            delay(100); // Give time for ACK to send
            formatStorage(); // Wipes LittleFS and reboots
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
