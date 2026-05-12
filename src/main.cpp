/*
  Spice Mixer - Production V2.4 (Modular Firmware)
*/

#include "Configuration.h"
#include "Database.h"
#include "Globals.h"
#include "Hardware.h"

// --- Global Object Definitions ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
Servo dispenserServo;
ColorDetector colorDetector(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);

char keys[ROWS][COLS] = {
  {'F', 'S', '#', '*'}, {'1', '2', '3', 'U'}, {'4', '5', '6', 'D'},
  {'7', '8', '9', 'E'}, {'L', '0', 'R', 'N'}
};
Keypad customKeypad = Keypad(makeKeymap(keys), (byte*)rowPins, (byte*)colPins, ROWS, COLS);

// --- Logic Variables ---
SystemState currentState = STATE_MAIN_MENU;
String userInput = "";
int currentTubeIndex = 0;
int pendingTargetTubeIndex = 0; 
int manualQuantityInput = 1;      
int targetDispenseCycles = 0;     
float currentRecipeGrams = 0.0;   
int servingsCount = 1; 
bool isTubeCurrentlyFilled = false;
bool isRecipeMode = false;
int currentRecipeIndex = 0;     
int currentIngredientIndex = 0; 
unsigned long timeStatusStarted = 0;

// --- Helper Functions in this scope ---
void prepareReturnHome();
void startRecipeIngredient();

void setup() {
  Serial.begin(115200);
  initHardware(); // Defined in Hardware.cpp
  updateLcd("Spice Mixer", "Booting...");
  delay(2000);
  
  verifySystemIntegrity(); // Defined in Hardware.cpp
  
  currentState = STATE_MAIN_MENU;
  updateLcd("1. Recipes", "2. Manual");
}

void loop() {
  // --- Asynchronous Ticks ---
  tickDispenser();
  
  switch (currentState) {
    case STATE_MAIN_MENU: {
      char key = customKeypad.getKey();
      if (key == '1') {
        userInput = "";
        currentState = STATE_RECIPE_SELECT;
        updateLcd("Recipe #", "(1-12)");
      } else if (key == '2') {
        isRecipeMode = false;
        userInput = "";
        currentState = STATE_AWAITING_INPUT;
        updateLcd("Tube (1-20):", "");
      }
      break;
    }

    case STATE_RECIPE_SELECT: {
      char key = customKeypad.getKey();
      if (key) {
        if (key >= '0' && key <= '9') {
          if (userInput.length() < 2) { 
            userInput += key;
            updateLcd("Recipe #:", userInput);
          }
        } else if (key == 'N') { 
           if (userInput.length() > 0) {
             int rNum = userInput.toInt();
             if (rNum >= 1 && rNum <= 12) {
               currentRecipeIndex = rNum - 1; 
               userInput = "";
               updateLcd("Servings?", ""); 
               currentState = STATE_RECIPE_SERVINGS_INPUT;
             } else {
               updateLcd("Invalid", "1-12 Only");
               delay(1000); userInput = ""; updateLcd("Recipe #", "(1-12)");
             }
           }
        } else if (key == 'E') { 
           currentState = STATE_MAIN_MENU;
           updateLcd("1. Recipes", "2. Manual");
        }
      }
      break;
    }

    case STATE_RECIPE_SERVINGS_INPUT: {
      char key = customKeypad.getKey();
      if (key) {
        if (key >= '0' && key <= '9') {
          if (userInput.length() < 2) { 
            userInput += key;
            updateLcd("Servings:", userInput);
          }
        } else if (key == 'N') {
           if (userInput.length() > 0) {
             int sNum = userInput.toInt();
             if (sNum >= 1) {
               servingsCount = sNum;
               currentIngredientIndex = 0;    
               isRecipeMode = true;
               updateLcd("Starting:", recipes[currentRecipeIndex].name);
               delay(1500);
               startRecipeIngredient(); 
             } else {
               updateLcd("Invalid", "Min 1");
               delay(1000); userInput = ""; updateLcd("Servings?", "");
             }
           }
        } else if (key == 'E') {
           userInput = "";
           currentState = STATE_RECIPE_SELECT;
           updateLcd("Recipe #", "(1-12)");
        }
      }
      break;
    }

    case STATE_AWAITING_INPUT: {
      char key = customKeypad.getKey();
      if (key) {
        if (key >= '0' && key <= '9') {
          if (userInput.length() < 2) { 
            userInput += key;
            updateLcd("Tube:", userInput);
          }
        } 
        else if (key == 'N') {
          if (userInput.length() > 0) {
            int targetTube = userInput.toInt();
            if (targetTube >= 1 && targetTube <= 20) {
              pendingTargetTubeIndex = targetTube - 1; 
              userInput = ""; 
              updateLcd("Theelepels?", ""); 
              currentState = STATE_AWAITING_QUANTITY_INPUT;
            } else {
              updateLcd("Invalid Tube", "1-20 Only");
              delay(1500); userInput = ""; updateLcd("Tube (1-20):", "");
            }
          }
        } 
        else if (key == 'E') {
          if (userInput.length() == 0) {
             currentState = STATE_MAIN_MENU;
             updateLcd("1. Recipes", "2. Manual");
          } else {
             userInput = "";
             updateLcd("Tube (1-20):", "");
          }
        }
      }
      break;
    }

    case STATE_AWAITING_QUANTITY_INPUT: {
      char key = customKeypad.getKey();
      if (key) {
        if (key >= '0' && key <= '9') {
          if (userInput.length() < 2) { 
            userInput += key;
            updateLcd("Theelepels:", userInput); 
          }
        } 
        else if (key == 'N') {
          if (userInput.length() > 0) {
            int qty = userInput.toInt();
            if (qty > 0) {
              manualQuantityInput = qty;
              targetDispenseCycles = manualQuantityInput * CYCLES_PER_THEELEPEL;
              
              updateLcd("Qty Set:", String(manualQuantityInput) + " Tlp");
              delay(1000);

              int tubesToMove = 0;
              if (pendingTargetTubeIndex >= currentTubeIndex) {
                tubesToMove = pendingTargetTubeIndex - currentTubeIndex;
              } else {
                tubesToMove = (TOTAL_TUBES - currentTubeIndex) + pendingTargetTubeIndex;
              }
              
              if (tubesToMove > 0) {
                updateLcd("Rotating...", "");
                stepper.enableOutputs();
                stepper.move(tubesToMove * STEPS_PER_TUBE);
                currentTubeIndex = pendingTargetTubeIndex; 
                currentState = STATE_ROTATING_TO_TARGET;
              } else {
                 updateLcd("Arrived", "Stabilizing...");
                 delay(1000); 
                 currentState = STATE_IDENTIFYING; 
              }
            } else {
               updateLcd("Invalid Qty", "Min 1");
               delay(1000); userInput = ""; updateLcd("Theelepels?", "");
            }
          }
        }
        else if (key == 'E') {
          userInput = "";
          updateLcd("Tube (1-20):", "");
          currentState = STATE_AWAITING_INPUT;
        }
      }
      break;
    }

    case STATE_ROTATING_TO_TARGET: {
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        updateLcd("Arrived", "Stabilizing...");
        delay(1000); 
        currentState = STATE_IDENTIFYING; 
      }
      break;
    }

    case STATE_IDENTIFYING: {
      updateLcd("Identifying...", "");
      delay(500); 
      
      String detectedSpice = identifySpice(); // Hardware.cpp
      String expectedSpice = spices[pendingTargetTubeIndex].name;
      
      if (detectedSpice == expectedSpice) {
         updateLcd("Correct:", detectedSpice);
         delay(2000); 
         currentState = STATE_CHECKING_FILL;
      } else {
         updateLcd("WRONG HERB!", "Found: " + detectedSpice);
         delay(3000);
         updateLcd("Expected:", expectedSpice);
         delay(3000);
         prepareReturnHome();
      }
      break;
    }

    case STATE_CHECKING_FILL: {
      updateLcd("Tube Fill Check...", "");
      delay(500);
      
      int laserState = digitalRead(LASER_RX_PIN);
      isTubeCurrentlyFilled = (laserState == HIGH); 
      
      if (isTubeCurrentlyFilled) {
        updateLcd("Status:", "Tube Filled");
        delay(1000); 
        startDispense(targetDispenseCycles);
        currentState = STATE_DISPENSING;
      } else {
        updateLcd("TUBE EMPTY!", "Fill & Press Enter");
        currentState = STATE_EMPTY_RETRY;
      }
      break;
    }

    case STATE_EMPTY_RETRY: {
      char key = customKeypad.getKey();
      if (key == 'N') { 
        updateLcd("Re-checking...", "");
        delay(1000);
        currentState = STATE_CHECKING_FILL; 
      } 
      else if (key == 'E') { 
        updateLcd("Cancelled", "Returning...");
        delay(1000);
        prepareReturnHome();
      }
      break;
    }

    case STATE_DISPENSING: {
      if (isRecipeMode) {
        updateLcd("Dispensing...", String(currentRecipeGrams, 1) + "g");
      } else {
        updateLcd("Dispensing...", String(manualQuantityInput) + " Tlp");
      }
      
      // Wait for the non-blocking dispenser to finish
      if (!isDispensing()) {
          if (isRecipeMode) {
            currentIngredientIndex++;
            
            if (currentIngredientIndex < recipes[currentRecipeIndex].ingredientCount) {
               updateLcd("Next Ingr...", "");
               delay(1000);
               startRecipeIngredient();
            } else {
               updateLcd("Recipe Done!", "Returning...");
               delay(1500);
               prepareReturnHome();
            }
          } else {
            updateLcd("Done!", "Returning...");
            delay(1000);
            prepareReturnHome(); 
          }
      }
      break;
    }

    case STATE_SHOWING_FILL_STATUS: {
      if (millis() - timeStatusStarted >= WAIT_DURATION_EMPTY) {
         prepareReturnHome();
      }
      break;
    }
    
    case STATE_RETURNING_HOME: {
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        if (isRecipeMode) {
           currentState = STATE_MAIN_MENU;
           updateLcd("1. Recipes", "2. Manual");
        } else {
           updateLcd("Tube (1-20):", "");
           userInput = "";
           currentState = STATE_AWAITING_INPUT;
        }
        stepper.disableOutputs();
      }
      break;
    }
  }
}

// --- Local Helper Functions ---

void startRecipeIngredient() {
   RecipeItem item = recipes[currentRecipeIndex].ingredients[currentIngredientIndex];
   
   pendingTargetTubeIndex = item.spiceIndex;
   
   // Logic: Base Grams * Servings
   currentRecipeGrams = item.quantityGrams * servingsCount; 
   
   // Logic: Grams / 0.2 = Cycles
   targetDispenseCycles = (int)(currentRecipeGrams / GRAMS_PER_CYCLE);
   
   String spiceName = spices[pendingTargetTubeIndex].name;
   updateLcd("Next:", spiceName);
   delay(1000);

   int tubesToMove = 0;
   if (pendingTargetTubeIndex >= currentTubeIndex) {
     tubesToMove = pendingTargetTubeIndex - currentTubeIndex;
   } else {
     tubesToMove = (TOTAL_TUBES - currentTubeIndex) + pendingTargetTubeIndex;
   }
   
   if (tubesToMove > 0) {
     updateLcd("Rotating...", "");
     stepper.enableOutputs();
     stepper.move(tubesToMove * STEPS_PER_TUBE);
     currentTubeIndex = pendingTargetTubeIndex; 
     currentState = STATE_ROTATING_TO_TARGET;
   } else {
      currentState = STATE_IDENTIFYING; 
   }
}

void prepareReturnHome() {
    if (currentTubeIndex != 0) { 
        updateLcd("Returning to", "Tube 1");
        int tubesToMoveHome = TOTAL_TUBES - currentTubeIndex; 
        stepper.enableOutputs();
        stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
        currentTubeIndex = 0;
        currentState = STATE_RETURNING_HOME;
    } else { 
        if (isRecipeMode) {
           currentState = STATE_MAIN_MENU;
           updateLcd("1. Recipes", "2. Manual");
        } else {
           userInput = "";
           updateLcd("Tube (1-20):", "");
           currentState = STATE_AWAITING_INPUT;
        }
        stepper.disableOutputs();
    }
}
