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
BLEManager bleManager;

char keys[ROWS][COLS] = {
  {'F', 'S', '#', '*'}, {'1', '2', '3', 'U'}, {'4', '5', '6', 'D'},
  {'7', '8', '9', 'E'}, {'L', '0', 'R', 'N'}
};
Keypad customKeypad = Keypad(makeKeymap(keys), (byte*)rowPins, (byte*)colPins, ROWS, COLS);

// --- Logic Variables ---
bool simulationEnabled = (SIMULATION_MODE == 1);
SystemState currentState = STATE_BOOT;
String userInput = "";
int currentTubeIndex = 0;
int pendingTargetTubeIndex = 0; 
int manualQuantityInput = 1;      
int targetDispenseCycles = 0;     
float currentRecipeGrams = 0.0;   
int servingsCount = 1; 
bool isTubeCurrentlyFilled = false;
bool isRecipeMode = false;
bool isInitialCheck = false;
bool abortTriggered = false;
bool remoteRequestTriggered = false;
Recipe remoteRecipe;
int currentRecipeIndex = 0;     
int currentIngredientIndex = 0; 

// --- Timer Variables ---
unsigned long stateStartTime = 0;
unsigned long lastStatusTime = 0;
#define STATE_TIMEOUT(ms) (millis() - stateStartTime >= (ms))

// --- Helper Functions ---
void prepareReturnHome();
void startRecipeIngredient();
void changeState(SystemState newState) {
    if (simulationEnabled) {
        Serial.printf("[DEBUG] State Change: %d -> %d\n", currentState, newState);
    }
    currentState = newState;
    stateStartTime = millis();
}

void sendBleStatus() {
    JsonDocument doc;
    doc["type"] = "status";
    
    String stateStr = "idle";
    switch (currentState) {
        case STATE_MAIN_MENU: stateStr = "idle"; break;
        case STATE_BOOT: 
        case STATE_SYSTEM_CHECK: 
        case STATE_ROTATING_TO_TARGET:
        case STATE_IDENTIFYING:
        case STATE_CHECKING_FILL:
            stateStr = isInitialCheck ? "booting" : "busy"; 
            break;
        case STATE_ERROR_RETURN: stateStr = "error"; break;
        default: stateStr = "busy"; break;
    }
    doc["state"] = stateStr;
    
    if (isRecipeMode) {
        if (isInitialCheck) {
            doc["active_recipe"] = "System Check";
            doc["progress"] = (int)(((float)currentTubeIndex / TOTAL_TUBES) * 100);
        } else {
            if (currentRecipeIndex == -1) {
                doc["active_recipe"] = "Remote";
            } else {
                doc["active_recipe"] = recipes[currentRecipeIndex].name;
            }
            
            int total = (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount);
            if (total > 0) {
                doc["progress"] = (int)(((float)currentIngredientIndex / total) * 100);
            } else {
                doc["progress"] = 0;
            }
        }
    } else if (currentState == STATE_DISPENSING || currentState == STATE_ROTATING_TO_TARGET) {
        doc["active_recipe"] = "Manual";
        doc["progress"] = isDispensing() ? 50 : 0; // Simplified progress for manual
    }
    
    bleManager.notifyStatus(doc);
}

void setup() {
  Serial.begin(115200);

  if (!initStorage()) {
      Serial.println("Storage Initialization Failed!");
  }
  loadDatabase();

  initHardware(); 
  colorDetector.setCalibration(WHITE_R, WHITE_G, WHITE_B, BLACK_R, BLACK_G, BLACK_B);
  bleManager.begin("Spice Dispenser");
  
  updateLcd("Spice Mixer", "Booting...");
  changeState(STATE_BOOT); 
}

void loop() {
  // --- Asynchronous Ticks ---
  tickDispenser();
  colorDetector.tick();
  bleManager.tick();
  
  // --- High Priority BLE Interrupts ---
  if (abortTriggered) {
      abortTriggered = false;
      isRecipeMode = false;
      isInitialCheck = false;
      emergencyStopHardware();
      updateLcd("ABORTED", "Returning Home");
      prepareReturnHome();
      return;
  }

  switch (currentState) {
    case STATE_BOOT:
      if (STATE_TIMEOUT(2000)) {
          isInitialCheck = true;
          isRecipeMode = true; // Use recipe-style progress for check
          currentRecipeIndex = -1;
          currentTubeIndex = 0;
          pendingTargetTubeIndex = 0;
          stepper.enableOutputs();
          updateLcd("System Check", "Starting...");
          changeState(STATE_SYSTEM_CHECK);
      }
      break;

    case STATE_SYSTEM_CHECK:
      updateLcd("Scanning...", "Tube " + String(currentTubeIndex + 1));
      if (STATE_TIMEOUT(500)) {
          startIdentifySpice();
          changeState(STATE_IDENTIFYING);
      }
      break;

    case STATE_MAIN_MENU: {
      // BLE Trigger
      if (remoteRequestTriggered) {
          remoteRequestTriggered = false;
          isRecipeMode = true;
          currentRecipeIndex = -1; // -1 indicates remoteRecipe
          currentIngredientIndex = 0;
          servingsCount = 1;
          updateLcd("Remote Order", "Starting...");
          startRecipeIngredient();
          break;
      }

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
      if (simulationEnabled) {
          if (STATE_TIMEOUT(100)) { // 100ms mock rotation
              startIdentifySpice();
              changeState(STATE_IDENTIFYING);
          }
      } else {
          stepper.run();
          if (stepper.distanceToGo() == 0) {
            if (STATE_TIMEOUT(500)) { 
                startIdentifySpice();
                changeState(STATE_IDENTIFYING);
            }
          }
      }
      break;
    }

    case STATE_IDENTIFYING: {
      if (!isIdentifying()) {
        String detectedSpice = getIdentifiedSpice();
        String expectedSpice = spices[pendingTargetTubeIndex].name;
        
        if (simulationEnabled) {
            Serial.printf("[DEBUG] Identification Step: Expected %s, Detected %s\n", expectedSpice.c_str(), detectedSpice.c_str());
        }

        if (detectedSpice == expectedSpice) {
           updateLcd("Correct:", detectedSpice);
           if (isInitialCheck) {
               if (STATE_TIMEOUT(simulationEnabled ? 50 : 1000)) {
                   currentTubeIndex++;
                   if (currentTubeIndex < TOTAL_TUBES) {
                       pendingTargetTubeIndex = currentTubeIndex;
                       if (simulationEnabled) Serial.printf("[DEBUG] System Check Step: Moving to Tube %d\n", currentTubeIndex + 1);
                       if (!simulationEnabled) stepper.move(STEPS_PER_TUBE);
                       changeState(STATE_ROTATING_TO_TARGET);
                   } else {
                       updateLcd("Check Done!", "Returning...");
                       isInitialCheck = false;
                       isRecipeMode = false;
                       prepareReturnHome();
                   }
               }
           } else {
               if (simulationEnabled) Serial.println("[DEBUG] Identification Success. Moving to Fill Check.");
               changeState(STATE_CHECKING_FILL); 
           }
        } else {
           updateLcd("WRONG HERB!", "Found: " + detectedSpice);
           bleManager.sendAlert("wrong_spice", pendingTargetTubeIndex + 1);
           if (STATE_TIMEOUT(3000)) {
               if (isInitialCheck) {
                   updateLcd("Fix & Press N", "Tube " + String(currentTubeIndex + 1));
                   changeState(STATE_EMPTY_RETRY); 
               } else {
                   prepareReturnHome();
               }
           }
        }
      }
      break;
    }

    case STATE_CHECKING_FILL: {
      if (STATE_TIMEOUT(1000)) { 
          int laserState = simulationEnabled ? HIGH : digitalRead(LASER_RX_PIN);
          if (laserState == HIGH) { 
            updateLcd("Status:", "Tube Filled");
            if (simulationEnabled) Serial.printf("[DEBUG] Fill Check Step: Tube %d is FILLED. Starting Dispense.\n", pendingTargetTubeIndex + 1);
            startDispense(targetDispenseCycles);
            changeState(STATE_DISPENSING);
          } else {
            updateLcd("TUBE EMPTY!", "N:Retry E:Exit");
            if (simulationEnabled) Serial.printf("[DEBUG] Fill Check Step: Tube %d is EMPTY!\n", pendingTargetTubeIndex + 1);
            bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);
            changeState(STATE_EMPTY_RETRY);
          }
      }
      break;
    }

    case STATE_EMPTY_RETRY: {
      char key = customKeypad.getKey();
      if (key == 'N') {
          if (isInitialCheck) changeState(STATE_SYSTEM_CHECK);
          else changeState(STATE_CHECKING_FILL);
      } 
      else if (key == 'E') {
          isInitialCheck = false;
          isRecipeMode = false;
          prepareReturnHome();
      }
      break;
    }

    case STATE_DISPENSING: {
      updateLcd("Dispensing...", isRecipeMode ? String(currentRecipeGrams, 1) + "g" : String(manualQuantityInput) + " Tlp");
      
      if (!isDispensing()) {
          float dispensed = isRecipeMode ? currentRecipeGrams : (manualQuantityInput * 5.0); // Assume 5g per teaspoon
          spices[pendingTargetTubeIndex].level -= (int)dispensed;
          if (spices[pendingTargetTubeIndex].level < 0) spices[pendingTargetTubeIndex].level = 0;
          
          saveDatabase(); // Commit to LittleFS
          
          // Trigger low spice alert if below 15%
          if (spices[pendingTargetTubeIndex].level < 15) {
              bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);
          }

          if (isRecipeMode) {
            currentIngredientIndex++;
            int count = (currentRecipeIndex == -1) ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount;
            if (currentIngredientIndex < count) {
               startRecipeIngredient();
            } else {
               updateLcd("Recipe Done!", "Returning...");
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
            isRecipeMode = false;
            if (currentRecipeIndex != -1) { // Normal mode
                 updateLcd("1. Recipes", "2. Manual");
                 changeState(STATE_MAIN_MENU);
            } else {
                 changeState(STATE_MAIN_MENU);
            }
        }
      }
      break;
    }
    
    default: break;
  }
}

void startRecipeIngredient() {
   RecipeItem item;
   if (currentRecipeIndex == -1) {
       item = remoteRecipe.ingredients[currentIngredientIndex];
   } else {
       item = recipes[currentRecipeIndex].ingredients[currentIngredientIndex];
   }
   
   pendingTargetTubeIndex = item.spiceIndex;
   currentRecipeGrams = item.quantityGrams * servingsCount; 
   targetDispenseCycles = (int)(currentRecipeGrams / GRAMS_PER_CYCLE);
   
   updateLcd("Next:", spices[pendingTargetTubeIndex].name);

   int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                     (pendingTargetTubeIndex - currentTubeIndex) : 
                     (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
   
   if (tubesToMove > 0) {
     if (!simulationEnabled) {
         stepper.enableOutputs();
         stepper.move(tubesToMove * STEPS_PER_TUBE);
     }
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
        if (!simulationEnabled) {
            stepper.enableOutputs();
            stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
        }
        currentTubeIndex = 0;
        changeState(STATE_RETURNING_HOME);
    } else { 
        isRecipeMode = false;
        updateLcd("1. Recipes", "2. Manual");
        changeState(STATE_MAIN_MENU);
        if (!simulationEnabled) stepper.disableOutputs();
    }
}
