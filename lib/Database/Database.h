#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

#define MAX_RECIPES 30

struct Spice {
  String name;
  int r_val, g_val, b_val;
  int level; // Current fill level (0-100)
};

struct RecipeItem {
  String spiceName; 
  float quantityGrams; 
};

struct Recipe {
  String id;
  String name;
  int ingredientCount;
  RecipeItem ingredients[10]; 
};

// --- DATA DECLARATIONS ---
extern const int NUM_SPICES;
extern Spice spices[];
extern Recipe recipes[MAX_RECIPES];
extern int activeRecipeCount;
extern String activeProfileUUID;
extern bool isMachineConfigured;

// --- PERSISTENCE METHODS ---
bool initStorage();
void formatStorage();
void loadGlobalSpices();
void saveGlobalSpices();
bool loadProfile(String uuid);
void saveProfile();
void deleteProfile(String uuid);
void factoryResetDatabase();

#endif