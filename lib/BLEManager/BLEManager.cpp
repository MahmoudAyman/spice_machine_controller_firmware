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
    
    // We no longer fallback to a default profile. The machine continues 
    // using the global configurations for physical keypad usage, while
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

        Serial.printf("[BLE RECV] %s\n", _rxBuffer.c_str());
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

            String statusStr = "success";
            if (!isMachineConfigured) {
                statusStr = "unconfigured";
            } else if (currentState == STATE_ERROR_RETURN) {
                statusStr = "error";
            }

            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "handshake";
            ack["status"] = statusStr;
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
        else if (strcmp(type, "get_manifest") == 0) {
            Serial.println("BLE: GET_MANIFEST (Streaming)");
            
            // 1. Send Start packet
            JsonDocument startDoc;
            startDoc["type"] = "manifest_start";
            startDoc["total"] = NUM_SPICES;
            notifyLevels(startDoc);
            delay(15);
            
            // 2. Stream individual items (using abbreviated keys 's', 'n', 'l', 'e')
            for (int i = 0; i < NUM_SPICES; i++) {
                JsonDocument itemDoc;
                itemDoc["type"] = "manifest_item";
                itemDoc["s"] = i + 1;
                itemDoc["n"] = spices[i].name;
                itemDoc["l"] = spices[i].level;
                itemDoc["e"] = spices[i].expiry;
                notifyLevels(itemDoc);
                delay(15); // Essential delay to let the BLE queue clear and transmit
            }
            
            // 3. Send End packet
            JsonDocument endDoc;
            endDoc["type"] = "manifest_end";
            notifyLevels(endDoc);
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
                    remoteRecipe.ingredients[i].spiceName = items[i]["name"] | "";
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
        else if (strcmp(type, "sync_recipes_start") == 0) {
            Serial.println("BLE: SYNC_RECIPES_START");
            activeRecipeCount = 0;
            
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "sync_recipes_start";
            ack["status"] = "success";
            notifyStatus(ack);
        }
        else if (strcmp(type, "sync_recipe_item") == 0) {
            int idx = doc["index"];
            Serial.printf("BLE: SYNC_RECIPE_ITEM [%d]\n", idx);
            if (idx >= 0 && idx < MAX_RECIPES) {
                JsonArray ingredientsArr = doc["i"];
                
                // --- Atomic Ingredient Validation (Case-Insensitive Substring Match) ---
                bool allIngredientsValid = true;
                String invalidSpiceName = "";
                
                if (!ingredientsArr.isNull()) {
                    for (JsonObject iObj : ingredientsArr) {
                        String ingName = iObj["n"] | "";
                        ingName.trim();
                        ingName.toUpperCase();
                        
                        if (ingName.length() == 0) continue;
                        
                        bool found = false;
                        for (int s = 0; s < NUM_SPICES; s++) {
                            String registeredName = spices[s].name;
                            registeredName.trim();
                            registeredName.toUpperCase();
                            
                            if (registeredName.length() > 0 && 
                                (registeredName == ingName || 
                                 registeredName.indexOf(ingName) != -1 || 
                                 ingName.indexOf(registeredName) != -1)) {
                                found = true;
                                break;
                            }
                        }
                        
                        if (!found) {
                            allIngredientsValid = false;
                            invalidSpiceName = iObj["n"] | "";
                            break;
                        }
                    }
                }
                
                if (!allIngredientsValid) {
                    Serial.printf("[ERROR] sync_recipe_item failed: Spice '%s' not available on machine.\n", invalidSpiceName.c_str());
                    JsonDocument nak;
                    nak["type"] = "ack";
                    nak["command"] = "sync_recipe_item";
                    nak["index"] = idx;
                    nak["status"] = "fail";
                    nak["reason"] = "invalid_ingredient";
                    nak["detail"] = invalidSpiceName;
                    notifyStatus(nak);
                    return;
                }
                
                // --- Validation Passed: Write to RAM ---
                String rId = "temp_" + String(idx);
                if (doc["id"].is<const char*>()) {
                    rId = doc["id"].as<String>();
                }
                recipes[idx].id = rId;
                recipes[idx].name = doc["n"].as<String>();
                
                recipes[idx].ingredientCount = 0;
                if (!ingredientsArr.isNull()) {
                    for (JsonObject iObj : ingredientsArr) {
                        if (recipes[idx].ingredientCount >= 10) break;
                        int iIdx = recipes[idx].ingredientCount;
                        recipes[idx].ingredients[iIdx].spiceName = iObj["n"] | "";
                        recipes[idx].ingredients[iIdx].quantityGrams = iObj["g"].as<float>();
                        recipes[idx].ingredientCount++;
                    }
                }
                
                if (idx + 1 > activeRecipeCount) {
                    activeRecipeCount = idx + 1;
                }
                
                JsonDocument ack;
                ack["type"] = "ack";
                ack["command"] = "sync_recipe_item";
                ack["index"] = idx;
                ack["status"] = "success";
                notifyStatus(ack);
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "sync_recipe_item";
                nak["index"] = idx;
                nak["status"] = "fail";
                nak["reason"] = "out_of_bounds";
                notifyStatus(nak);
            }
        }
        else if (strcmp(type, "sync_recipes_end") == 0) {
            Serial.println("BLE: SYNC_RECIPES_END");
            saveRecipesCache(); // Save cached recipes persistently
            
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "sync_recipes_end";
            ack["status"] = "success";
            notifyStatus(ack);
        }
        else if (strcmp(type, "setup_slot_name") == 0) {
            Serial.println("BLE: SETUP_SLOT_NAME");
            const char* name = doc["name"];
            Serial.printf("[BLE SETUP DEBUG] currentState: %d (Expected STATE_INITIAL_SETUP: 1), Name provided: '%s'\n", currentState, name ? name : "null");
            
            if (currentState == STATE_INITIAL_SETUP) {
                if (name && strlen(name) > 0) {
                    bleSetupNameReceived = String(name);
                    bleSetupNamePending = true;
                } else {
                    JsonDocument nak;
                    nak["type"] = "ack";
                    nak["command"] = "setup_slot_name";
                    nak["status"] = "fail";
                    nak["reason"] = "empty_name";
                    notifyStatus(nak);
                }
            } else {
                JsonDocument nak;
                nak["type"] = "ack";
                nak["command"] = "setup_slot_name";
                nak["status"] = "fail";
                nak["reason"] = "invalid_state";
                notifyStatus(nak);
            }
        }
        else if (strcmp(type, "update_slot") == 0) {
            Serial.println("BLE: UPDATE_SLOT");
            int slot = doc["slot"];
            const char* name = doc["name"];
            if (slot >= 1 && slot <= NUM_SPICES && name) {
                Serial.printf("[DEBUG] Updating slot %d to %s. Saving to GLOBAL config.\n", slot, name);
                spices[slot - 1].name = String(name);
                
                // Read optional expiry epoch timestamp
                if (doc["expiry"].is<uint32_t>()) {
                    spices[slot - 1].expiry = doc["expiry"];
                } else if (doc["e"].is<uint32_t>()) {
                    spices[slot - 1].expiry = doc["e"];
                }
                
                saveGlobalSpices();
                
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
                
                // Read optional expiry epoch timestamp for the newly refilled spice
                if (doc["expiry"].is<uint32_t>()) {
                    spices[slot - 1].expiry = doc["expiry"];
                } else if (doc["e"].is<uint32_t>()) {
                    spices[slot - 1].expiry = doc["e"];
                }
                
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
        else if (strcmp(type, "print_spices") == 0) {
            Serial.println("--- CURRENT SPICES DEBUG ---");
            for (int i = 0; i < NUM_SPICES; i++) {
                Serial.printf("Slot %2d | Name: %-15s | Level: %3d%% | Raw Calibration: R:%3d, G:%3d, B:%3d\n",
                              i + 1, 
                              spices[i].name.c_str(), 
                              spices[i].level,
                              spices[i].r_val, 
                              spices[i].g_val, 
                              spices[i].b_val);
            }
            Serial.println("-----------------------------");
            JsonDocument ack;
            ack["type"] = "ack";
            ack["command"] = "print_spices";
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
            delay(500); // Give time for ACK to send
            factoryResetDatabase(); // Wipes configuration and reboots
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
        Serial.printf("[BLE SEND STATUS] %s\n", buffer);
        _pStatusChar->setValue(buffer);
        _pStatusChar->notify();
    }
}

void BLEManager::notifyLevels(JsonDocument& doc) {
    if (_deviceConnected) {
        char buffer[1024];
        serializeJson(doc, buffer);
        Serial.printf("[BLE SEND SYNC] %s\n", buffer);
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
