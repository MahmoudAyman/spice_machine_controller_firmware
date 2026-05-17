#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

struct Spice {
  String name;
  int r_val, g_val, b_val;
  int level; // Current fill level (0-100)
};

struct RecipeItem {
  int spiceIndex; 
  float quantityGrams; 
};

struct Recipe {
  String name;
  int ingredientCount;
  RecipeItem ingredients[10]; 
};

// --- DATA DECLARATIONS ---
extern const int NUM_SPICES;
extern Spice spices[];
extern const Recipe recipes[];

// --- PERSISTENCE METHODS ---
bool initStorage();
void loadDatabase();
void saveDatabase();
void resetDatabase();
bool checkProfile(String uuid);

#endif