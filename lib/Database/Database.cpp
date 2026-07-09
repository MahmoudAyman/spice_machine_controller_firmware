#include "Database.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

const int NUM_SPICES = 20;
int activeRecipeCount = 0;
bool isMachineConfigured = false;

// The working arrays in RAM
Spice spices[NUM_SPICES];
Recipe recipes[MAX_RECIPES];

bool initStorage() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return false;
    }
    Serial.println("LittleFS Mounted Successfully");
    return true;
}

void formatStorage() {
    Serial.println("Formatting LittleFS... Please wait.");
    LittleFS.format();
    Serial.println("Format complete. Restarting device...");
    delay(500);
    ESP.restart();
}

void loadGlobalSpices() {
    if (!LittleFS.exists("/global_spices.json")) {
        Serial.println("No global spice config found. Machine unconfigured.");
        isMachineConfigured = false;
        for (int i = 0; i < NUM_SPICES; i++) {
            spices[i].name = "";
            spices[i].level = 0;
            spices[i].r_val = 0;
            spices[i].g_val = 0;
            spices[i].b_val = 0;
        }
        return;
    }

    File file = LittleFS.open("/global_spices.json", "r");
    if (!file) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Failed to parse global_spices.json");
        return;
    }

    isMachineConfigured = true;
    JsonArray spicesArr = doc["spices"];
    int i = 0;
    for (JsonObject obj : spicesArr) {
        if (i >= NUM_SPICES) break;
        spices[i].name = obj["name"] | "";
        spices[i].level = obj["level"] | 0;
        spices[i].r_val = obj["r_val"] | 0; 
        spices[i].g_val = obj["g_val"] | 0;
        spices[i].b_val = obj["b_val"] | 0;
        i++;
    }
    Serial.println("Global Spices loaded successfully.");
}

void saveGlobalSpices() {
    File file = LittleFS.open("/global_spices.json", "w");
    if (!file) {
        Serial.println("Failed to open global_spices.json for writing");
        return;
    }

    JsonDocument doc;
    JsonArray spicesArr = doc["spices"].to<JsonArray>();
    for (int i = 0; i < NUM_SPICES; i++) {
        JsonObject obj = spicesArr.add<JsonObject>();
        obj["name"] = spices[i].name;
        obj["level"] = spices[i].level;
        obj["r_val"] = spices[i].r_val;
        obj["g_val"] = spices[i].g_val;
        obj["b_val"] = spices[i].b_val;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write global spices to file");
    } else {
        isMachineConfigured = true; // Saving implies configuration occurred
        Serial.println("Global Spices saved to LittleFS.");
    }
    file.close();
}

void factoryResetDatabase() {
    Serial.println("[DB] Executing Factory Reset...");
    if (LittleFS.exists("/global_spices.json")) {
        LittleFS.remove("/global_spices.json");
    }
    isMachineConfigured = false;
    Serial.println("[DB] Factory Reset complete. Rebooting...");
    delay(500);
    ESP.restart();
}