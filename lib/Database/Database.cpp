#include "Database.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

const int NUM_SPICES = 20;
int activeRecipeCount = 12;
String activeProfileUUID = "";
bool isMachineConfigured = false;

// The working arrays in RAM
Spice spices[NUM_SPICES];
Recipe recipes[MAX_RECIPES];

// The factory default data
const Spice defaultSpices[NUM_SPICES] = {
  {"Paprika",      180, 420, 380, 100}, // 0
  {"Onion Powder", 130, 140, 135, 100}, // 1
  {"Basil",        350, 180, 320, 100}, // 2
  {"Turmeric",     140, 180, 300, 100}, // 3
  {"Oregano",      380, 220, 350, 100}, // 4
  {"Coriander",    250, 280, 300, 100}, // 5
  {"Thyme",        360, 250, 340, 100}, // 6
  {"Ginger",       160, 190, 280, 100}, // 7
  {"Parsley",      340, 170, 330, 100}, // 8
  {"Cardamom",     280, 200, 290, 100}, // 9
  {"Rosemary",     400, 250, 380, 100}, // 10
  {"Clove",        450, 480, 460, 100}, // 11
  {"Garlic",       120, 125, 115, 100}, // 12
  {"Nutmeg",       320, 360, 400, 100}, // 13
  {"Black Pepper", 520, 600, 540, 100}, // 14
  {"Bay Leaves",   330, 230, 310, 100}, // 15
  {"Chili Flakes", 190, 400, 370, 100}, // 16
  {"Star Anise",   420, 490, 470, 100}, // 17
  {"Cumin",        290, 330, 380, 100}, // 18
  {"Cinnamon",     260, 380, 420, 100}  // 19
};

const Recipe defaultRecipes[12] = {
  { "Italian Herbs", 8, { {2, 1.6}, {4, 1.6}, {6, 1.2}, {8, 1.2}, {10, 1.0}, {12, 0.8}, {14, 0.6}, {16, 0.2} }},        
  { "Taco Seas.", 8, { {0, 2.0}, {18, 2.2}, {1, 1.4}, {12, 1.2}, {14, 0.8}, {16, 0.8}, {6, 0.6}, {4, 0.6} }},
  { "Curry Madras", 9, { {3, 2.0}, {5, 1.6}, {18, 1.2}, {0, 0.8}, {7, 0.8}, {12, 0.6}, {14, 0.6}, {16, 0.4}, {9, 0.4} }},
  { "BBQ", 9, { {0, 2.4}, {12, 1.2}, {1, 1.2}, {14, 0.8}, {18, 0.6}, {16, 0.6}, {6, 0.6}, {4, 0.2}, {19, 0.4} }},
  { "Garam Masala", 9, { {9, 2.0}, {18, 1.6}, {5, 1.2}, {19, 0.8}, {14, 0.6}, {11, 0.6}, {13, 0.6}, {15, 0.4}, {17, 0.4} }},
  { "Ras el Hanout", 10, { {18, 1.6}, {5, 1.6}, {19, 0.8}, {0, 0.8}, {7, 0.8}, {9, 0.6}, {11, 0.4}, {13, 0.6}, {3, 0.6}, {14, 0.6} }},
  { "Gyros", 8, { {4, 1.6}, {6, 1.2}, {12, 1.2}, {1, 1.2}, {0, 1.2}, {18, 0.6}, {14, 0.6}, {8, 0.4} }},
  { "Shawarma", 9, { {18, 1.6}, {5, 1.2}, {0, 1.2}, {12, 1.2}, {1, 0.8}, {14, 0.6}, {3, 0.6}, {19, 0.4}, {9, 0.4} }},
  { "ChiliConCarne", 7, { {0, 2.4}, {16, 1.6}, {18, 1.6}, {12, 0.8}, {1, 0.8}, {4, 0.4}, {14, 0.4} }},
  { "Moroc. Tagine", 8, { {18, 1.6}, {5, 1.6}, {0, 1.2}, {3, 1.2}, {19, 0.8}, {7, 0.8}, {14, 0.4}, {9, 0.4} }},
  { "Ind. Tandoori", 8, { {0, 2.0}, {18, 1.4}, {5, 1.2}, {12, 1.2}, {7, 0.8}, {3, 0.6}, {16, 0.6}, {9, 0.2} }},
  { "H. de Provence", 8, { {6, 1.6}, {10, 1.6}, {4, 1.2}, {2, 1.2}, {8, 0.8}, {15, 0.6}, {14, 0.4}, {12, 0.6} }}
};

bool initStorage() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return false;
    }
    Serial.println("LittleFS Mounted Successfully");
    return true;
}

void loadGlobalSpices() {
    if (!LittleFS.exists("/global_spices.json")) {
        Serial.println("No global spice config found. Using factory defaults. Machine unconfigured.");
        isMachineConfigured = false;
        for (int i = 0; i < NUM_SPICES; i++) spices[i] = defaultSpices[i];
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
        spices[i].name = obj["name"] | defaultSpices[i].name;
        spices[i].level = obj["level"] | defaultSpices[i].level;
        spices[i].r_val = defaultSpices[i].r_val; 
        spices[i].g_val = defaultSpices[i].g_val;
        spices[i].b_val = defaultSpices[i].b_val;
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
        Serial.printf("Profile for UUID %s not found. Using default recipes.\n", uuid.c_str());
        activeRecipeCount = 12;
        for (int i = 0; i < activeRecipeCount; i++) recipes[i] = defaultRecipes[i];
        return false; // Does not exist
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
        rObj["name"] = recipes[i].name;
        JsonArray ingredientsArr = rObj["ingredients"].to<JsonArray>();
        for (int j = 0; j < recipes[i].ingredientCount; j++) {
            JsonObject iObj = ingredientsArr.add<JsonObject>();
            iObj["slot"] = recipes[i].ingredients[j].spiceIndex + 1;
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