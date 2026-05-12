#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

struct Spice {
  String name;
  int r_val, g_val, b_val;
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
extern const Spice spices[];
extern const Recipe recipes[];

#endif