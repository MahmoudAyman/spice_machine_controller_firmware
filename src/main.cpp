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
    sendBleStatus(); 

    // Update Header on State Change
    String header = "Spice Mixer";
    if (isRecipeMode) header = "MODE: Recipe";
    else if (isInitialCheck) header = "MODE: System Check";
    else header = "MODE: Manual";
    
    lcd.updateHeader(header, bleManager.isConnected());
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
  static bool lastBleConn = false;
  bool currentBleConn = bleManager.isConnected();
  if (currentBleConn != lastBleConn) {
      lastBleConn = currentBleConn;
      lcd.updateHeader(isRecipeMode ? "MODE: Recipe" : "MODE: Manual", currentBleConn);
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
      lcd.updateHeader("ABORTED", bleManager.isConnected());
      lcd.updateContent("Returning Home", "");
      prepareReturnHome();
      return;
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
          lcd.updateContent("System Check", "Initializing...");
          lcd.updateStatus("Powering Motors...", ILI9341_YELLOW);
          changeState(STATE_SYSTEM_CHECK);
      }
      break;

    case STATE_SYSTEM_CHECK:
      if (STATE_TIMEOUT(simulationEnabled ? 50 : 500)) {
          Serial.printf("[BOOT] Scanning Tube %d/20...\n", currentTubeIndex + 1);
          lcd.updateContent("Scanning...", "Tube " + String(currentTubeIndex + 1));
          lcd.drawProgressBar((int)(((float)currentTubeIndex / TOTAL_TUBES) * 100));
          lcd.updateStatus("Verifying Slots", ILI9341_WHITE);
          startIdentifySpice();
          changeState(STATE_IDENTIFYING);
      }
      break;

    case STATE_MAIN_MENU: {
      if (remoteRequestTriggered) {
          Serial.println("[EVENT] Remote Dispense Order Received via BLE!");
          remoteRequestTriggered = false;
          isRecipeMode = true;
          currentRecipeIndex = -1; 
          currentIngredientIndex = 0;
          servingsCount = 1;
          lcd.updateContent("Remote Order", "Processing...");
          lcd.updateStatus("App Controlled", ILI9341_CYAN);
          startRecipeIngredient();
          break;
      }

      char key = getButtonKey();
      if (key == 'U' || key == 'D') {
          currentSelection = (currentSelection == 0) ? 1 : 0;
          lcd.updateContent("MAIN MENU:", currentSelection == 0 ? "> 1. Recipes" : "> 2. Manual");
          lcd.updateStatus("U/D: Nav  N: Select", ILI9341_WHITE);
          delay(200); 
      } else if (key == 'N') {
          if (currentSelection == 0) {
              currentSelection = 0; 
              lcd.updateContent("Select Recipe:", recipes[0].name);
              lcd.updateStatus("U/D: Browse  N: Pick", ILI9341_WHITE);
              changeState(STATE_RECIPE_SELECT);
          } else {
              currentSelection = 0; 
              lcd.updateContent("Select Tube:", String(currentSelection + 1));
              lcd.updateStatus("U/D: Browse  N: Pick", ILI9341_WHITE);
              changeState(STATE_AWAITING_INPUT);
          }
          delay(200);
      }
      break;
    }

    case STATE_RECIPE_SELECT: {
      char key = getButtonKey();
      if (key == 'U') {
          currentSelection = (currentSelection + 1) % activeRecipeCount;
          lcd.updateContent("Select Recipe:", recipes[currentSelection].name);
          delay(200);
      } else if (key == 'D') {
          currentSelection = (currentSelection - 1 + activeRecipeCount) % activeRecipeCount;
          lcd.updateContent("Select Recipe:", recipes[currentSelection].name);
          delay(200);
      } else if (key == 'N') {
          currentRecipeIndex = currentSelection;
          currentSelection = 1; 
          lcd.updateContent("Servings:", String(currentSelection));
          lcd.updateStatus("Set Quantity", ILI9341_WHITE);
          changeState(STATE_RECIPE_SERVINGS_INPUT);
          delay(200);
      } else if (key == 'E') {
          currentSelection = 0;
          lcd.updateContent("MAIN MENU:", "1. Recipes");
          changeState(STATE_MAIN_MENU);
          delay(200);
      }
      break;
    }

    case STATE_RECIPE_SERVINGS_INPUT: {
      char key = getButtonKey();
      if (key == 'U') {
          currentSelection++;
          lcd.updateContent("Servings:", String(currentSelection));
          delay(200);
      } else if (key == 'D') {
          if (currentSelection > 1) currentSelection--;
          lcd.updateContent("Servings:", String(currentSelection));
          delay(200);
      } else if (key == 'N') {
           servingsCount = currentSelection;
           currentIngredientIndex = 0;    
           isRecipeMode = true;
           lcd.updateContent("Starting:", recipes[currentRecipeIndex].name);
           lcd.updateStatus("Preparing Dispenser", ILI9341_WHITE);
           startRecipeIngredient(); 
           delay(200);
      } else if (key == 'E') {
           currentSelection = currentRecipeIndex;
           lcd.updateContent("Select Recipe:", recipes[currentSelection].name);
           changeState(STATE_RECIPE_SELECT);
           delay(200);
      }
      break;
    }

    case STATE_AWAITING_INPUT: {
      char key = getButtonKey();
      if (key == 'U') {
          currentSelection = (currentSelection + 1) % TOTAL_TUBES;
          lcd.updateContent("Select Tube:", String(currentSelection + 1));
          delay(200);
      } else if (key == 'D') {
          currentSelection = (currentSelection - 1 + TOTAL_TUBES) % TOTAL_TUBES;
          lcd.updateContent("Select Tube:", String(currentSelection + 1));
          delay(200);
      } else if (key == 'N') {
          pendingTargetTubeIndex = currentSelection;
          currentSelection = 1; 
          lcd.updateContent("Quantity (Tlp):", String(currentSelection));
          lcd.updateStatus("Set Amount", ILI9341_WHITE);
          changeState(STATE_AWAITING_QUANTITY_INPUT);
          delay(200);
      } else if (key == 'E') {
          currentSelection = 1; 
          lcd.updateContent("MAIN MENU:", "1. Recipes");
          changeState(STATE_MAIN_MENU);
          delay(200);
      }
      break;
    }

    case STATE_AWAITING_QUANTITY_INPUT: {
      char key = getButtonKey();
      if (key == 'U') {
          currentSelection++;
          lcd.updateContent("Quantity (Tlp):", String(currentSelection));
          delay(200);
      } else if (key == 'D') {
          if (currentSelection > 1) currentSelection--;
          lcd.updateContent("Quantity (Tlp):", String(currentSelection));
          delay(200);
      } else if (key == 'N') {
              manualQuantityInput = currentSelection;
              targetDispenseCycles = manualQuantityInput * CYCLES_PER_THEELEPEL;
              
              int tubesToMove = (pendingTargetTubeIndex >= currentTubeIndex) ? 
                                (pendingTargetTubeIndex - currentTubeIndex) : 
                                (TOTAL_TUBES - currentTubeIndex + pendingTargetTubeIndex);
              
              if (tubesToMove > 0) {
                Serial.printf("[MANUAL] Rotating %d tubes to target %d...\n", tubesToMove, pendingTargetTubeIndex + 1);
                lcd.updateContent("Rotating...", "To Slot " + String(pendingTargetTubeIndex + 1));
                lcd.updateStatus("Motor Running", ILI9341_YELLOW);
                if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
                stepper.move(tubesToMove * STEPS_PER_TUBE);
                currentTubeIndex = pendingTargetTubeIndex; 
                changeState(STATE_ROTATING_TO_TARGET);
              } else {
                 Serial.println("[MANUAL] Already at target tube.");
                 lcd.updateContent("Arrived", "Stabilizing...");
                 lcd.updateStatus("At Slot " + String(pendingTargetTubeIndex + 1), ILI9341_GREEN);
                 changeState(STATE_IDENTIFYING); 
              }
              delay(200);
      } else if (key == 'E') {
          currentSelection = pendingTargetTubeIndex;
          lcd.updateContent("Select Tube:", String(currentSelection + 1));
          changeState(STATE_AWAITING_INPUT);
          delay(200);
      }
      break;
    }

    case STATE_ROTATING_TO_TARGET: {
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

    case STATE_IDENTIFYING: {
      if (!isIdentifying()) {
        String detectedSpice = getIdentifiedSpice();
        String expectedSpice = spices[pendingTargetTubeIndex].name;
        
        Serial.printf("[SENSOR] Expected: %s | Detected: %s\n", expectedSpice.c_str(), detectedSpice.c_str());

        if (detectedSpice == expectedSpice) {
           lcd.updateContent("Correct Spice:", detectedSpice);
           lcd.updateStatus("Match Confirmed", ILI9341_GREEN);
           if (isInitialCheck) {
               if (STATE_TIMEOUT(simulationEnabled ? 50 : 1000)) {
                   currentTubeIndex++;
                   if (currentTubeIndex < TOTAL_TUBES) {
                       pendingTargetTubeIndex = currentTubeIndex;
                       if (!simulationEnabled) stepper.move(STEPS_PER_TUBE);
                       changeState(STATE_ROTATING_TO_TARGET);
                   } else {
                       Serial.println("[BOOT] Initial Check Complete. All tubes verified.");
                       lcd.updateContent("System Ready!", "Tubes Verified");
                       lcd.updateStatus("Done", ILI9341_GREEN);
                       isInitialCheck = false;
                       isRecipeMode = false;
                       prepareReturnHome();
                   }
               }
           } else {
               changeState(STATE_CHECKING_FILL); 
           }
        } else {
           Serial.printf("[ERROR] Wrong Spice! Found %s at slot %d\n", detectedSpice.c_str(), pendingTargetTubeIndex + 1);
           lcd.updateContent("WRONG HERB!", "Found: " + detectedSpice);
           lcd.updateStatus("ERROR: MISMATCH", ILI9341_RED);
           bleManager.sendAlert("wrong_spice", pendingTargetTubeIndex + 1);
           if (STATE_TIMEOUT(3000)) {
               if (isInitialCheck) {
                   lcd.updateContent("Manual Fix Req", "Press N to Retry");
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
      if (STATE_TIMEOUT(simulationEnabled ? 50 : 1000)) { 
          int laserState = simulationEnabled ? HIGH : digitalRead(LASER_RX_PIN);
          if (laserState == HIGH) { 
            Serial.println("[SENSOR] Fill sensor: FILLED.");
            lcd.updateContent("Dispenser:", "Tube Ready");
            lcd.updateStatus("Level OK", ILI9341_GREEN);
            startDispense(targetDispenseCycles);
            changeState(STATE_DISPENSING);
          } else {
            Serial.println("[SENSOR] Fill sensor: EMPTY!");
            lcd.updateContent("TUBE EMPTY!", "Needs Refill");
            lcd.updateStatus("ALERT: LOW LEVEL", ILI9341_RED);
            bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);
            changeState(STATE_EMPTY_RETRY);
          }
      }
      break;
    }

    case STATE_EMPTY_RETRY: {
      char key = getButtonKey();
      if (key == 'N') {
          Serial.println("[EVENT] Retrying identification/fill check...");
          if (isInitialCheck) changeState(STATE_SYSTEM_CHECK);
          else changeState(STATE_CHECKING_FILL);
      } 
      else if (key == 'E') {
          Serial.println("[EVENT] User cancelled after empty/wrong spice.");
          isInitialCheck = false;
          isRecipeMode = false;
          prepareReturnHome();
      }
      break;
    }

    case STATE_DISPENSING: {
      static int lastRem = -1;
      if (getRemainingDispenseCycles() != lastRem) {
          lastRem = getRemainingDispenseCycles();
          int progress = (int)((1.0 - (float)lastRem / (targetDispenseCycles > 0 ? targetDispenseCycles : 1)) * 100);
          lcd.updateContent("Dispensing...", spices[pendingTargetTubeIndex].name);
          lcd.drawProgressBar(progress);
          lcd.updateStatus(String(lastRem) + " cycles left", ILI9341_WHITE);
      }
      
      if (!isDispensing()) {
          Serial.println("[DISPENSER] Cycle complete.");
          lastRem = -1;
          float dispensed = isRecipeMode ? currentRecipeGrams : (manualQuantityInput * 5.0); 
          spices[pendingTargetTubeIndex].level -= (int)dispensed;
          if (spices[pendingTargetTubeIndex].level < 0) spices[pendingTargetTubeIndex].level = 0;
          
          saveGlobalSpices(); 
          
          if (spices[pendingTargetTubeIndex].level < 15) {
              bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1);
          }

          if (isRecipeMode) {
            currentIngredientIndex++;
            int count = (currentRecipeIndex == -1) ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount;
            if (currentIngredientIndex < count) {
               Serial.printf("[RECIPE] Moving to next ingredient (%d/%d)...\n", currentIngredientIndex + 1, count);
               startRecipeIngredient();
            } else {
               Serial.println("[RECIPE] All ingredients dispensed.");
               lcd.updateContent("Recipe Done!", "Enjoy!");
               lcd.updateStatus("Success", ILI9341_GREEN);
               prepareReturnHome();
            }
          } else {
            lcd.updateContent("Manual Done!", "Enjoy!");
            lcd.updateStatus("Success", ILI9341_GREEN);
            prepareReturnHome(); 
          }
      }
      break;
    }

    case STATE_RETURNING_HOME: {
      if (simulationEnabled) {
          if (STATE_TIMEOUT(100)) {
              if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
              isRecipeMode = false;
              lcd.updateContent("MAIN MENU:", "Select Operation");
              lcd.updateStatus("System Ready", ILI9341_WHITE);
              changeState(STATE_MAIN_MENU);
          }
      } else {
          stepper.run();
          if (stepper.distanceToGo() == 0) {
            if (STATE_TIMEOUT(500)) {
                Serial.println("[MOTOR] Returned home.");
                if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
                isRecipeMode = false;
                lcd.updateContent("MAIN MENU:", "Select Operation");
                lcd.updateStatus("System Ready", ILI9341_WHITE);
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

   lcd.updateContent("Next Ingredient:", spices[pendingTargetTubeIndex].name);
   lcd.updateStatus("Moving to Slot " + String(pendingTargetTubeIndex + 1), ILI9341_WHITE);

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
        lcd.updateContent("Job Finished", "Returning Home");
        lcd.updateStatus("Homing to Tube 1", ILI9341_YELLOW);
        int tubesToMoveHome = TOTAL_TUBES - currentTubeIndex; 
        if (!simulationEnabled) {
            if (STEP_ENABLE_PIN != -1) stepper.enableOutputs();
            stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
        }
        currentTubeIndex = 0;
        changeState(STATE_RETURNING_HOME);
    } else { 
        isRecipeMode = false;
        lcd.updateContent("MAIN MENU:", "1. Recipes");
        lcd.updateStatus("System Ready", ILI9341_WHITE);
        changeState(STATE_MAIN_MENU);
        if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
    }
}
