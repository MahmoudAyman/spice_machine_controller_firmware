/*
  Spice Mixer - Production V3.4
*/

#include "Configuration.h"
#include "Database.h"
#include "Globals.h"
#include "Hardware.h"

// --- Global Object Definitions ---
LCDManager lcd;
AccelStepper stepper = AccelStepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);
Servo dispenserServo;
ColorDetector colorDetector = ColorDetector(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);
BLEManager bleManager;

// --- Logic Variables ---
bool simulationEnabled = (SIMULATION_MODE == 1);
SystemState currentState = STATE_BOOT;
String userInput = "";
int currentTubeIndex = 0;
int pendingTargetTubeIndex = 0; 
int currentSelection = 0; 
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
int lastSelectionDrawn = -1; 

// --- Timer Variables ---
unsigned long stateStartTime = 0;
unsigned long lastStatusTime = 0;
#define STATE_TIMEOUT(ms) (millis() - stateStartTime >= (ms))

// --- Helper Functions ---
void prepareReturnHome();
void startRecipeIngredient();

void changeState(SystemState newState) {
    Serial.printf("[STATE] %d -> %d\n", currentState, newState);
    currentState = newState;
    stateStartTime = millis();
    lastSelectionDrawn = -1; // Reset tracker for UI redraws
    sendBleStatus(); 

    // Update Header on State Change
    String header = "Spice Mixer";
    if (isInitialCheck) header = "MODE: System Check";
    else if (currentState == STATE_MAIN_MENU) header = "MAIN MENU";
    else if (isRecipeMode || currentState == STATE_DISPENSING || currentState == STATE_ROTATING_TO_TARGET) header = "DISPENSING...";
    else header = "MODE: Manual";
    
    lcd.updateHeader(header, bleManager.getBleStatus());
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
        doc["progress"] = isDispensing() ? 50 : 0;
    }
    
    bleManager.notifyStatus(doc);
}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- Spice Dispenser Production V3.4 ---");

  Serial.println("[INIT] Initializing Storage...");
  if (!initStorage()) {
      Serial.println("[ERROR] Storage Initialization Failed!");
  }
  
  Serial.println("[INIT] Loading Global Spices...");
  loadGlobalSpices();

  if (activeProfileUUID == "") {
      activeRecipeCount = 12;
      for (int i = 0; i < activeRecipeCount; i++) recipes[i] = defaultRecipes[i];
  }

  Serial.println("[INIT] Initializing Hardware Drivers...");
  initHardware(); 
  lcd.begin(); 
  
  Serial.println("[INIT] Configuring Color Detector...");
  colorDetector.setCalibration(WHITE_R, WHITE_G, WHITE_B, BLACK_R, BLACK_G, BLACK_B);
  
  Serial.println("[INIT] Initializing BLE...");
  bleManager.begin("Spice Dispenser");
  
  Serial.println("[BOOT] Updating LCD...");
  lcd.updateContent("Spice Mixer", "Booting...");
  
  Serial.println("[BOOT] Entering STATE_BOOT...");
  changeState(STATE_BOOT); 
}

void loop() {
  // --- Asynchronous Ticks ---
  tickDispenser();
  colorDetector.tick();
  bleManager.tick();
  
  // Safe Edge-Triggered BLE Status Update
  static int lastBleStatus = -1;
  int currentBleStatus = bleManager.getBleStatus();
  if (currentBleStatus != lastBleStatus) {
      lastBleStatus = currentBleStatus;
      String header = "Spice Mixer";
      if (isInitialCheck) header = "MODE: System Check";
      else if (currentState == STATE_MAIN_MENU) header = "MAIN MENU";
      else if (isRecipeMode || currentState == STATE_DISPENSING || currentState == STATE_ROTATING_TO_TARGET) header = "DISPENSING...";
      else header = "MODE: Manual";
      lcd.updateHeader(header, currentBleStatus);
  }

  // Periodic status push
  if (millis() - lastStatusTime >= 2000) {
      sendBleStatus();
      lastStatusTime = millis();
  }

  // --- High Priority BLE Interrupts ---
  if (abortTriggered) {
      Serial.println("[EVENT] Abort Triggered!");
      abortTriggered = false;
      if (currentState == STATE_DISPENSING) {
          int remainingCycles = getRemainingDispenseCycles(); 
          int completedCycles = targetDispenseCycles - remainingCycles;
          float percentageCompleted = (float)completedCycles / (targetDispenseCycles > 0 ? targetDispenseCycles : 1);
          float totalExpected = isRecipeMode ? currentRecipeGrams : (manualQuantityInput * 5.0);
          float actualDispensed = totalExpected * percentageCompleted;
          spices[pendingTargetTubeIndex].level -= (int)actualDispensed;
          if (spices[pendingTargetTubeIndex].level < 0) spices[pendingTargetTubeIndex].level = 0;
          saveGlobalSpices(); 
      }
      isRecipeMode = false;
      isInitialCheck = false;
      emergencyStopHardware();
      lcd.showOperationView("ABORTED", "Returning Home", 0, "Stopping...", "");
      prepareReturnHome();
      return;
  }

  // --- Physical Button Input (Edge Triggered) ---
  static char lastKey = 0;
  char rawKey = getButtonKey();
  char key = 0;
  if (rawKey != lastKey) {
      if (rawKey != 0) key = rawKey; // Key pressed
      lastKey = rawKey;
  }

  // Handle Physical Abort (Left Button)
  if (key == 'L' && !isInitialCheck) {
      if (currentState == STATE_DISPENSING || currentState == STATE_ROTATING_TO_TARGET || 
          currentState == STATE_IDENTIFYING || currentState == STATE_CHECKING_FILL) {
          Serial.println("[EVENT] Physical Abort Triggered!");
          abortTriggered = true;
      }
  }

  switch (currentState) {
    case STATE_BOOT:
      if (STATE_TIMEOUT(2000)) {
          Serial.println("[BOOT] Starting Initial System Check...");
          isInitialCheck = true;
          isRecipeMode = false; 
          currentRecipeIndex = -1;
          currentTubeIndex = 0;
          pendingTargetTubeIndex = 0;
          if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
          lcd.showOperationView("BOOTING", "System Check", 0, "Initializing...", "");
          changeState(STATE_SYSTEM_CHECK);
      }
      break;

    case STATE_SYSTEM_CHECK:
      if (STATE_TIMEOUT(simulationEnabled ? 50 : 500)) {
          Serial.printf("[BOOT] Scanning Tube %d/20...\n", currentTubeIndex + 1);
          
          // 1. Static Header
          lcd.updateHeader("SYSTEM CHECK...", bleManager.getBleStatus());
          
          // 2. Setup Layout or Update Surgically
          if (currentTubeIndex == 0) {
              lcd.updateContent(spices[currentTubeIndex].name, "Scanning...");
              lcd.drawProgressBar(0, 160, true); 
          } else {
              lcd.updateSpiceName(spices[currentTubeIndex].name);
              lcd.updateTask("Scanning...");
              lcd.updateDetail("");
          }

          startIdentifySpice();
          changeState(STATE_IDENTIFYING);
      }
      break;

    case STATE_IDENTIFYING: {
      if (!isIdentifying()) {
        String detectedSpice = getIdentifiedSpice();
        String expectedSpice = spices[pendingTargetTubeIndex].name;
        
        Serial.printf("[SENSOR] Expected: %s | Detected: %s\n", expectedSpice.c_str(), detectedSpice.c_str());

        if (detectedSpice == expectedSpice) {
           if (isInitialCheck) {
               // Surgical updates for boot
               lcd.updateSpiceName(detectedSpice);
               lcd.updateTask("VERIFIED");
               
               int nextProgress = (int)(((float)(currentTubeIndex + 1) / TOTAL_TUBES) * 100);
               lcd.drawProgressBar(nextProgress, 160);
               
               delay(100);

               if (STATE_TIMEOUT(simulationEnabled ? 50 : 800)) {
                   currentTubeIndex++;
                   if (currentTubeIndex < TOTAL_TUBES) {
                       pendingTargetTubeIndex = currentTubeIndex;
                       if (!simulationEnabled) stepper.move(STEPS_PER_TUBE);
                       changeState(STATE_ROTATING_TO_TARGET);
                   } else {
                       Serial.println("[BOOT] Initial Check Complete. All tubes verified.");
                       lcd.updateSpiceName("System Ready");
                       lcd.updateTask("All Slots OK");
                       lcd.drawProgressBar(100, 160);
                       delay(500);
                       isInitialCheck = false;
                       isRecipeMode = false;
                       prepareReturnHome();
                   }
               }
           } else {
               lcd.updateTask("IDENTIFIED");
               lcd.updateDetail("Match Confirmed");
               delay(200);
               changeState(STATE_CHECKING_FILL); 
           }
        } else {
           if (isInitialCheck) {
                lcd.updateContent("MISMATCH!", "Found: " + detectedSpice);
                lcd.updateStatus("Expected: " + expectedSpice, ILI9341_RED);
           } else {
                lcd.showOperationView("MISMATCH!", "Found: " + detectedSpice, 0, "ERROR: WRONG SPICE", "Exit");
           }
           bleManager.sendAlert("wrong_spice", pendingTargetTubeIndex + 1);
           if (STATE_TIMEOUT(3000)) {
               if (isInitialCheck) changeState(STATE_EMPTY_RETRY); 
               else prepareReturnHome();
           }
        }
      } else if (!isInitialCheck) {
          lcd.updateTask("IDENTIFYING...");
          lcd.updateDetail("Scanning Label");
      }
      break;
    }

    case STATE_MAIN_MENU: {
      if (remoteRequestTriggered) {
          Serial.println("[EVENT] Remote Dispense Order Received via BLE!");
          remoteRequestTriggered = false;
          isRecipeMode = true;
          currentRecipeIndex = -1; 
          currentIngredientIndex = 0;
          servingsCount = 1;
          lcd.showOperationView("REMOTE ORDER", "Processing...", 0, "App Controlled", "Abort");
          startRecipeIngredient();
          break;
      }

      if (currentSelection != lastSelectionDrawn) {
          const char* menuOptions[] = {"Recipes", "Manual Control"};
          lcd.showMenu("MAIN MENU", menuOptions, 2, currentSelection, "", "Select");
          lastSelectionDrawn = currentSelection;
      }

      if (key == 'U') {
          currentSelection = 0;
      } else if (key == 'D') {
          currentSelection = 1;
      } else if (key == 'N') {
          if (currentSelection == 0) {
              currentSelection = 0; 
              changeState(STATE_RECIPE_SELECT);
          } else {
              currentSelection = 0; 
              changeState(STATE_AWAITING_INPUT);
          }
      }
      break;
    }

    case STATE_RECIPE_SELECT: {
      if (currentSelection != lastSelectionDrawn) {
          const char* recipeNames[activeRecipeCount];
          for (int i = 0; i < activeRecipeCount; i++) recipeNames[i] = recipes[i].name.c_str();
          lcd.showMenu("SELECT RECIPE", recipeNames, activeRecipeCount, currentSelection, "Back", "Select");
          lastSelectionDrawn = currentSelection;
      }

      if (key == 'U') {
          if (currentSelection > 0) currentSelection--;
      } else if (key == 'D') {
          if (currentSelection < activeRecipeCount - 1) currentSelection++;
      } else if (key == 'N') {
          currentRecipeIndex = currentSelection;
          currentSelection = 1; 
          changeState(STATE_RECIPE_SERVINGS_INPUT);
      } else if (key == 'L') {
          currentSelection = 0;
          changeState(STATE_MAIN_MENU);
      }
      break;
    }

    case STATE_RECIPE_SERVINGS_INPUT: {
      if (currentSelection != lastSelectionDrawn) {
          lcd.showNumericSelection("SERVINGS", String(currentSelection), "Portions", "Back", "Start");
          lastSelectionDrawn = currentSelection;
      }

      if (key == 'U') {
          currentSelection++;
      } else if (key == 'D') {
          if (currentSelection > 1) currentSelection--;
      } else if (key == 'N') {
           servingsCount = currentSelection;
           currentIngredientIndex = 0;    
           isRecipeMode = true;
           lcd.showOperationView("PREPARING", recipes[currentRecipeIndex].name, 0, "Initializing...", "Abort");
           startRecipeIngredient(); 
      } else if (key == 'L') {
           currentSelection = currentRecipeIndex;
           changeState(STATE_RECIPE_SELECT);
      }
      break;
    }

    case STATE_AWAITING_INPUT: {
      if (currentSelection != lastSelectionDrawn) {
          lcd.showNumericSelection("SELECT TUBE", String(currentSelection + 1), "Slot Number", "Back", "Select");
          lastSelectionDrawn = currentSelection;
      }

      if (key == 'U') {
          if (currentSelection > 0) currentSelection--;
      } else if (key == 'D') {
          if (currentSelection < TOTAL_TUBES - 1) currentSelection++;
      } else if (key == 'N') {
          pendingTargetTubeIndex = currentSelection;
          currentSelection = 1; 
          changeState(STATE_AWAITING_QUANTITY_INPUT);
      } else if (key == 'L') {
          currentSelection = 1; 
          changeState(STATE_MAIN_MENU);
      }
      break;
    }

    case STATE_AWAITING_QUANTITY_INPUT: {
      if (currentSelection != lastSelectionDrawn) {
          lcd.showNumericSelection("QUANTITY", String(currentSelection), "Theelepels", "Back", "Start");
          lastSelectionDrawn = currentSelection;
      }

      if (key == 'U') {
          currentSelection++;
      } else if (key == 'D') {
          if (currentSelection > 1) currentSelection--;
      } else if (key == 'N') {
              manualQuantityInput = currentSelection;
              targetDispenseCycles = manualQuantityInput * CYCLES_PER_THEELEPEL;
              
              int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                                (pendingTargetTubeIndex - currentTubeIndex) : 
                                (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
              
              if (tubesToMove > 0) {
                Serial.printf("[MANUAL] Rotating %d tubes to target %d...\n", tubesToMove, pendingTargetTubeIndex + 1);
                lcd.showOperationView("ROTATING", "Slot " + String(pendingTargetTubeIndex + 1), 0, "Motor Running", "Abort");
                if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
                stepper.move(tubesToMove * STEPS_PER_TUBE);
                currentTubeIndex = pendingTargetTubeIndex; 
                changeState(STATE_ROTATING_TO_TARGET);
              } else {
                 Serial.println("[MANUAL] Already at target tube.");
                 lcd.showOperationView("ARRIVED", "Slot " + String(pendingTargetTubeIndex + 1), 100, "Stabilizing...", "Abort");
                 changeState(STATE_IDENTIFYING); 
              }
      } else if (key == 'L') {
          currentSelection = pendingTargetTubeIndex;
          changeState(STATE_AWAITING_INPUT);
      }
      break;
    }

    case STATE_ROTATING_TO_TARGET: {
      if (!isInitialCheck) {
          lcd.updateHeader("DISPENSING...", bleManager.getBleStatus());
          lcd.updateTask("Moving to Slot " + String(pendingTargetTubeIndex + 1));
          lcd.updateDetail(""); 
      }
      if (simulationEnabled) {
          if (STATE_TIMEOUT(50)) { 
              startIdentifySpice();
              changeState(STATE_IDENTIFYING);
          }
      } else {
          stepper.run();
          if (stepper.distanceToGo() == 0) {
            if (STATE_TIMEOUT(500)) { 
                Serial.println("[MOTOR] Rotation complete.");
                startIdentifySpice();
                changeState(STATE_IDENTIFYING);
            }
          }
      }
      break;
    }

    case STATE_CHECKING_FILL: {
      if (!isInitialCheck) {
          lcd.updateTask("VERIFYING LEVEL");
          lcd.updateDetail("Checking Sensor...");
      }
      if (STATE_TIMEOUT(simulationEnabled ? 50 : 1000)) { 
          int laserState = simulationEnabled ? HIGH : digitalRead(LASER_RX_PIN);
          if (laserState == HIGH) { 
            if (!isInitialCheck) {
                lcd.updateTask("READY");
                lcd.updateDetail("Level OK");
                delay(200);
            }
            startDispense(targetDispenseCycles);
            changeState(STATE_DISPENSING);
          } else {
            if (!isInitialCheck) {
                lcd.showOperationView("EMPTY!", spices[pendingTargetTubeIndex].name, 0, "Needs Refill", "Exit");
            }
            bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);
            changeState(STATE_EMPTY_RETRY);
          }
      }
      break;
    }

    case STATE_EMPTY_RETRY: {
      lcd.showOperationView("ALERT", "Refill Needed", 0, "Press OK to Retry", "Back");
      char key = getButtonKey();
      if (key == 'N') {
          Serial.println("[EVENT] Retrying identification/fill check...");
          if (isInitialCheck) changeState(STATE_SYSTEM_CHECK);
          else changeState(STATE_CHECKING_FILL);
      } 
      else if (key == 'L') {
          Serial.println("[EVENT] User cancelled after empty/wrong spice.");
          isInitialCheck = false;
          isRecipeMode = false;
          prepareReturnHome();
      }
      break;
    }

    case STATE_DISPENSING: {
      static int lastRem = -1;
      int currentRem = getRemainingDispenseCycles();
      
      if (!isInitialCheck) {
          lcd.updateTask("DISPENSING...");
          if (currentRem != lastRem) {
              lastRem = currentRem;
              lcd.updateDetail("Cycles Left: " + String(lastRem));
              
              int totalItems = isRecipeMode ? (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount) : 1;
              float baseProgress = ((float)currentIngredientIndex / totalItems) * 100.0;
              float itemContribution = (1.0 / totalItems) * 100.0;
              float itemProgress = (1.0 - (float)currentRem / (targetDispenseCycles > 0 ? targetDispenseCycles : 1));
              int globalProgress = (int)(baseProgress + (itemProgress * itemContribution));
              
              lcd.drawProgressBar(globalProgress, 160);
          }
      }
      
      if (!isDispensing()) {
          Serial.println("[DISPENSER] Cycle complete.");
          lastRem = -1;
          float dispensed = isRecipeMode ? currentRecipeGrams : (manualQuantityInput * 5.0); 
          spices[pendingTargetTubeIndex].level -= (int)dispensed;
          if (spices[pendingTargetTubeIndex].level < 0) spices[pendingTargetTubeIndex].level = 0;
          saveGlobalSpices(); 
          
          if (spices[pendingTargetTubeIndex].level < 15) bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);

          if (isRecipeMode) {
            currentIngredientIndex++;
            int count = (currentRecipeIndex == -1) ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount;
            if (currentIngredientIndex < count) {
               startRecipeIngredient();
            } else {
               lcd.showOperationView("COMPLETE", "Recipe Finished", 100, "Enjoy!", "Back");
               delay(1500);
               prepareReturnHome();
            }
          } else {
            lcd.showOperationView("COMPLETE", "Manual Done", 100, "Enjoy!", "Back");
            delay(1500);
            prepareReturnHome(); 
          }
      }
      break;
    }

    case STATE_RETURNING_HOME: {
      lcd.showOperationView("HOMING", "Moving to Slot 1", 50, "Clearing Area", "");
      if (simulationEnabled) {
          if (STATE_TIMEOUT(100)) {
              if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
              isRecipeMode = false;
              changeState(STATE_MAIN_MENU);
          }
      } else {
          stepper.run();
          if (stepper.distanceToGo() == 0) {
            if (STATE_TIMEOUT(500)) {
                Serial.println("[MOTOR] Returned home.");
                if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
                isRecipeMode = false;
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
   
   Serial.printf("[RECIPE] Target Tube: %d | Grams: %.1fg | Cycles: %d\n", 
       pendingTargetTubeIndex + 1, currentRecipeGrams, targetDispenseCycles);

   if (!isInitialCheck) {
       // Enhanced Stability: Use surgical updates to keep progress bar drawn
       lcd.updateHeader("DISPENSING...", bleManager.getBleStatus());
       lcd.setActionBar("Abort"); // Ensure button is visible
       if (currentIngredientIndex == 0) {
           // First ingredient: Full setup
           lcd.updateContent(spices[pendingTargetTubeIndex].name, "PREPARING");
           lcd.drawProgressBar(0, 160, true);
       } else {
           // Subsequent ingredients: Surgical updates to preserve Progress Bar
           lcd.updateSpiceName(spices[pendingTargetTubeIndex].name);
           lcd.updateTask("PREPARING");
           lcd.updateDetail("");
       }
   }

   int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                     (pendingTargetTubeIndex - currentTubeIndex) : 
                     (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
   
   if (tubesToMove > 0) {
     if (!simulationEnabled) {
         if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
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
        Serial.printf("[MOTOR] Returning home from Tube %d...\n", currentTubeIndex + 1);
        lcd.showOperationView("FINISHING", "Returning Home", 0, "Homing to Slot 1", "");
        int tubesToMoveHome = TOTAL_TUBES - currentTubeIndex; 
        if (!simulationEnabled) {
            if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
            stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
        }
        currentTubeIndex = 0;
        changeState(STATE_RETURNING_HOME);
    } else { 
        isRecipeMode = false;
        changeState(STATE_MAIN_MENU);
        if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
    }
}