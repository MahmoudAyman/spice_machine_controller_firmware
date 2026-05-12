/*
  Spice Mixer - Production V3.0 (Asynchronous Firmware)
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

// --- Timer Variables ---
unsigned long stateStartTime = 0;
#define STATE_TIMEOUT(ms) (millis() - stateStartTime >= (ms))

// --- Helper Functions ---
void prepareReturnHome();
void startRecipeIngredient();
void changeState(SystemState newState) {
    currentState = newState;
    stateStartTime = millis();
}

void setup() {
  Serial.begin(115200);
  initHardware(); 
  changeState(STATE_MAIN_MENU);
  updateLcd("Spice Mixer", "Ready");
}

void loop() {
  // --- Asynchronous Ticks ---
  tickDispenser();
  colorDetector.tick();
  
  switch (currentState) {
    case STATE_MAIN_MENU: {
      char key = customKeypad.getKey();
      if (key == '1') {
        userInput = "";
        updateLcd("Recipe #", "(1-12)");
        changeState(STATE_RECIPE_SELECT);
      } else if (key == '2') {
        isRecipeMode = false;
        userInput = "";
        updateLcd("Tube (1-20):", "");
        changeState(STATE_AWAITING_INPUT);
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
        } else if (key == 'N' && userInput.length() > 0) { 
             int rNum = userInput.toInt();
             if (rNum >= 1 && rNum <= 12) {
               currentRecipeIndex = rNum - 1; 
               userInput = "";
               updateLcd("Servings?", ""); 
               changeState(STATE_RECIPE_SERVINGS_INPUT);
             } else {
               updateLcd("Invalid", "1-12 Only");
               userInput = ""; 
               // Auto-revert after 1s? For now, user just types again
             }
        } else if (key == 'E') { 
           updateLcd("1. Recipes", "2. Manual");
           changeState(STATE_MAIN_MENU);
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
        } else if (key == 'N' && userInput.length() > 0) {
             int sNum = userInput.toInt();
             if (sNum >= 1) {
               servingsCount = sNum;
               currentIngredientIndex = 0;    
               isRecipeMode = true;
               updateLcd("Starting:", recipes[currentRecipeIndex].name);
               startRecipeIngredient(); 
             } else {
               updateLcd("Invalid", "Min 1");
               userInput = "";
             }
        } else if (key == 'E') {
           userInput = "";
           updateLcd("Recipe #", "(1-12)");
           changeState(STATE_RECIPE_SELECT);
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
        } else if (key == 'N' && userInput.length() > 0) {
            int targetTube = userInput.toInt();
            if (targetTube >= 1 && targetTube <= 20) {
              pendingTargetTubeIndex = targetTube - 1; 
              userInput = ""; 
              updateLcd("Theelepels?", ""); 
              changeState(STATE_AWAITING_QUANTITY_INPUT);
            } else {
              updateLcd("Invalid Tube", "1-20 Only");
              userInput = "";
            }
        } else if (key == 'E') {
           updateLcd("1. Recipes", "2. Manual");
           changeState(STATE_MAIN_MENU);
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
        } else if (key == 'N' && userInput.length() > 0) {
            int qty = userInput.toInt();
            if (qty > 0) {
              manualQuantityInput = qty;
              targetDispenseCycles = manualQuantityInput * CYCLES_PER_THEELEPEL;
              
              int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                                (pendingTargetTubeIndex - currentTubeIndex) : 
                                (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
              
              if (tubesToMove > 0) {
                updateLcd("Rotating...", "");
                stepper.enableOutputs();
                stepper.move(tubesToMove * STEPS_PER_TUBE);
                currentTubeIndex = pendingTargetTubeIndex; 
                changeState(STATE_ROTATING_TO_TARGET);
              } else {
                 updateLcd("Arrived", "Stabilizing...");
                 changeState(STATE_IDENTIFYING); 
              }
            }
        } else if (key == 'E') {
          userInput = "";
          updateLcd("Tube (1-20):", "");
          changeState(STATE_AWAITING_INPUT);
        }
      }
      break;
    }

    case STATE_ROTATING_TO_TARGET: {
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        if (STATE_TIMEOUT(500)) { // Stabilization pause
            startIdentifySpice();
            changeState(STATE_IDENTIFYING);
        }
      }
      break;
    }

    case STATE_IDENTIFYING: {
      updateLcd("Identifying...", "");
      if (!isIdentifying()) {
        String detectedSpice = getIdentifiedSpice();
        String expectedSpice = spices[pendingTargetTubeIndex].name;
        
        if (detectedSpice == expectedSpice) {
           updateLcd("Correct:", detectedSpice);
           changeState(STATE_CHECKING_FILL); 
        } else {
           updateLcd("WRONG HERB!", "Found: " + detectedSpice);
           // In async, we'd need a sub-state to show this message for 2s
           // For now, let's just wait in this state before moving home
           if (STATE_TIMEOUT(3000)) prepareReturnHome();
        }
      }
      break;
    }

    case STATE_CHECKING_FILL: {
      if (STATE_TIMEOUT(1000)) { // Show "Correct" for 1s
          int laserState = digitalRead(LASER_RX_PIN);
          if (laserState == HIGH) { // Tube Filled
            updateLcd("Status:", "Tube Filled");
            startDispense(targetDispenseCycles);
            changeState(STATE_DISPENSING);
          } else {
            updateLcd("TUBE EMPTY!", "N:Retry E:Exit");
            changeState(STATE_EMPTY_RETRY);
          }
      }
      break;
    }

    case STATE_EMPTY_RETRY: {
      char key = customKeypad.getKey();
      if (key == 'N') changeState(STATE_CHECKING_FILL); 
      else if (key == 'E') prepareReturnHome();
      break;
    }

    case STATE_DISPENSING: {
      updateLcd("Dispensing...", isRecipeMode ? String(currentRecipeGrams, 1) + "g" : String(manualQuantityInput) + " Tlp");
      
      if (!isDispensing()) {
          if (isRecipeMode) {
            currentIngredientIndex++;
            if (currentIngredientIndex < recipes[currentRecipeIndex].ingredientCount) {
               startRecipeIngredient();
            } else {
               updateLcd("Recipe Done!", "Returning...");
               changeState(STATE_RETURNING_HOME); // Set return home in prepareReturnHome
               prepareReturnHome();
            }
          } else {
            updateLcd("Done!", "Returning...");
            prepareReturnHome(); 
          }
      }
      break;
    }

    case STATE_RETURNING_HOME: {
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        if (STATE_TIMEOUT(500)) {
            stepper.disableOutputs();
            if (isRecipeMode) {
               updateLcd("1. Recipes", "2. Manual");
               changeState(STATE_MAIN_MENU);
            } else {
               updateLcd("Tube (1-20):", "");
               userInput = "";
               changeState(STATE_AWAITING_INPUT);
            }
        }
      }
      break;
    }
    
    default: break;
  }
}

void startRecipeIngredient() {
   RecipeItem item = recipes[currentRecipeIndex].ingredients[currentIngredientIndex];
   pendingTargetTubeIndex = item.spiceIndex;
   currentRecipeGrams = item.quantityGrams * servingsCount; 
   targetDispenseCycles = (int)(currentRecipeGrams / GRAMS_PER_CYCLE);
   
   updateLcd("Next:", spices[pendingTargetTubeIndex].name);

   int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                     (pendingTargetTubeIndex - currentTubeIndex) : 
                     (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
   
   if (tubesToMove > 0) {
     stepper.enableOutputs();
     stepper.move(tubesToMove * STEPS_PER_TUBE);
     currentTubeIndex = pendingTargetTubeIndex; 
     changeState(STATE_ROTATING_TO_TARGET);
   } else {
      startIdentifySpice();
      changeState(STATE_IDENTIFYING); 
   }
}

void prepareReturnHome() {
    if (currentTubeIndex != 0) { 
        updateLcd("Returning to", "Tube 1");
        int tubesToMoveHome = TOTAL_TUBES - currentTubeIndex; 
        stepper.enableOutputs();
        stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
        currentTubeIndex = 0;
        changeState(STATE_RETURNING_HOME);
    } else { 
        if (isRecipeMode) {
           updateLcd("1. Recipes", "2. Manual");
           changeState(STATE_MAIN_MENU);
        } else {
           userInput = "";
           updateLcd("Tube (1-20):", "");
           changeState(STATE_AWAITING_INPUT);
        }
        stepper.disableOutputs();
    }
}
