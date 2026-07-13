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
            spices[i].level = 0.0f;
            spices[i].r_val = 0;
            spices[i].g_val = 0;
            spices[i].b_val = 0;
            spices[i].expiry = 0;
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
        spices[i].level = obj["level"] | 0.0f;
        spices[i].r_val = obj["r_val"] | 0; 
        spices[i].g_val = obj["g_val"] | 0;
        spices[i].b_val = obj["b_val"] | 0;
        spices[i].expiry = obj["e"] | 0;
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
        obj["e"] = spices[i].expiry;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write global spices to file");
    } else {
        isMachineConfigured = true; // Saving implies configuration occurred
        Serial.println("Global Spices saved to LittleFS.");
    }
    file.close();
}

void loadRecipesCache() {
    if (!LittleFS.exists("/recipes_cache.json")) {
        Serial.println("[DB] No recipes cache found. Initializing default diagnostic recipe.");
        activeRecipeCount = 1;
        recipes[0].id = "def_1";
        recipes[0].name = "Motion Test";
        recipes[0].ingredientCount = 2;
        recipes[0].ingredients[0].spiceName = "BLACK";
        recipes[0].ingredients[0].quantityGrams = 1.0;
        recipes[0].ingredients[1].spiceName = "BLUE";
        recipes[0].ingredients[1].quantityGrams = 1.0;
        return;
    }

    File file = LittleFS.open("/recipes_cache.json", "r");
    if (!file) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("[DB] Failed to parse recipes_cache.json");
        return;
    }

    JsonArray arr = doc["recipes"];
    activeRecipeCount = 0;
    for (JsonObject rObj : arr) {
        if (activeRecipeCount >= MAX_RECIPES) break;
        String fallbackId = "temp_" + String(activeRecipeCount);
        recipes[activeRecipeCount].id = rObj["id"] | fallbackId;
        recipes[activeRecipeCount].name = rObj["name"].as<String>();
        
        JsonArray ingArr = rObj["ingredients"];
        recipes[activeRecipeCount].ingredientCount = 0;
        for (JsonObject iObj : ingArr) {
            if (recipes[activeRecipeCount].ingredientCount >= 10) break;
            int idx = recipes[activeRecipeCount].ingredientCount;
            recipes[activeRecipeCount].ingredients[idx].spiceName = iObj["name"] | "";
            recipes[activeRecipeCount].ingredients[idx].quantityGrams = iObj["grams"].as<float>();
            recipes[activeRecipeCount].ingredientCount++;
        }
        activeRecipeCount++;
    }
    Serial.printf("[DB] Loaded %d cached recipes from LittleFS.\n", activeRecipeCount);
}

void saveRecipesCache() {
    File file = LittleFS.open("/recipes_cache.json", "w");
    if (!file) {
        Serial.println("[DB] Failed to open recipes_cache.json for writing");
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc["recipes"].to<JsonArray>();
    for (int i = 0; i < activeRecipeCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = recipes[i].id;
        obj["name"] = recipes[i].name;
        JsonArray ingArr = obj["ingredients"].to<JsonArray>();
        for (int j = 0; j < recipes[i].ingredientCount; j++) {
            JsonObject ing = ingArr.add<JsonObject>();
            ing["name"] = recipes[i].ingredients[j].spiceName;
            ing["grams"] = recipes[i].ingredients[j].quantityGrams;
        }
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("[DB] Failed to serialize recipes cache");
    } else {
        Serial.println("[DB] Saved recipes cache to LittleFS");
    }
    file.close();
}

void factoryResetDatabase() {
    Serial.println("[DB] Executing Factory Reset...");
    if (LittleFS.exists("/global_spices.json")) {
        LittleFS.remove("/global_spices.json");
    }
    if (LittleFS.exists("/recipes_cache.json")) {
        LittleFS.remove("/recipes_cache.json");
    }
    isMachineConfigured = false;
    Serial.println("[DB] Factory Reset complete. Rebooting...");
    delay(500);
    ESP.restart();
}