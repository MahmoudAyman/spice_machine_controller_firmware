#include "Database.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

const int NUM_SPICES = 20;
int activeRecipeCount = 0;
String activeProfileUUID = "";
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

bool loadProfile(String uuid) {
    String path = "/profile_" + uuid + ".json";
    activeProfileUUID = uuid; // Keep it active even if it's new
    
    if (!LittleFS.exists(path)) {
        Serial.printf("Profile for UUID %s not found. Initializing empty profile.\n", uuid.c_str());
        activeRecipeCount = 0; // Force user to configure their own recipes
        return false; // Does not exist, needs sync from App
    }

    File file = LittleFS.open(path, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Failed to parse profile JSON");
        return false;
    }

    // Load Recipes ONLY (Spices are now global)
    JsonArray recipesArr = doc["recipes"];
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
            recipes[activeRecipeCount].ingredients[idx].spiceName = iObj["name"] | "";
            recipes[activeRecipeCount].ingredients[idx].quantityGrams = iObj["grams"].as<float>();
            recipes[activeRecipeCount].ingredientCount++;
        }
        activeRecipeCount++;
    }

    Serial.printf("Profile %s recipes loaded successfully.\n", uuid.c_str());
    return true;
}

void saveProfile() {
    if (activeProfileUUID == "") {
        Serial.println("Cannot save recipes: No active profile set.");
        return;
    }

    String path = "/profile_" + activeProfileUUID + ".json";
    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.println("Failed to open profile file for writing");
        return;
    }

    JsonDocument doc;
    
    // Save Recipes ONLY
    JsonArray recipesArr = doc["recipes"].to<JsonArray>();
    for (int i = 0; i < activeRecipeCount; i++) {
        JsonObject rObj = recipesArr.add<JsonObject>();
        rObj["id"] = recipes[i].id;
        rObj["name"] = recipes[i].name;
        JsonArray ingredientsArr = rObj["ingredients"].to<JsonArray>();
        for (int j = 0; j < recipes[i].ingredientCount; j++) {
            JsonObject iObj = ingredientsArr.add<JsonObject>();
            iObj["name"] = recipes[i].ingredients[j].spiceName;
            iObj["grams"] = recipes[i].ingredients[j].quantityGrams;
        }
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write profile to file");
    }
    file.close();
    Serial.printf("Profile %s saved to LittleFS\n", activeProfileUUID.c_str());
}

void deleteProfile(String uuid) {
    String path = "/profile_" + uuid + ".json";
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
        Serial.printf("Profile %s deleted.\n", uuid.c_str());
    }
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