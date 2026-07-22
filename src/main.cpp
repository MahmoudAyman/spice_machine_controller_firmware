/*
  Spice Mixer - Production V3.4
*/

#include "Configuration.h"
#include "Database.h"
#include "Globals.h"
#include "Hardware.h"
#include "MotionController.h"
#include "ServoController.h"

// --- Global Object Definitions ---
LCDManager lcd;
Servo dispenserServo;
BLEManager bleManager;
LaserLevelSensor laserSensor(LASER_RX_PIN);

// --- Logic Variables ---
bool simulationEnabled = (SIMULATION_MODE == 1);
SystemState currentState = STATE_BOOT;
String userInput = "";
bool ingredientDispensed[10] = {false};
int searchStepCount = 0;

enum SetupState {
    SETUP_HOMING,
    SETUP_INIT_TUBE,
    SETUP_ROTATE_EXITING,
    SETUP_ROTATE_ENTERING,
    SETUP_ROTATE_ALIGNING,
    SETUP_READ_COLOR,
    SETUP_AWAITING_NAME
};
SetupState setupSubState = SETUP_HOMING;
unsigned long setupDebounceTimer = 0;

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
String bleSetupNameReceived = "";
bool bleSetupNamePending = false;
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
int findSlotByName(String targetName);
void startRecipeSearch();
void applyDispensedSpiceLevel(int completedCycles);

void applyDispensedSpiceLevel(int completedCycles) {
    if (pendingTargetTubeIndex >= 0 && pendingTargetTubeIndex < TOTAL_TUBES) {
        float actualDispensedGrams = completedCycles * GRAMS_PER_CYCLE;
        
        Serial.printf("[DISPENSER] Level reduction: %d cycles -> %.2fg reduction on Slot %d '%s' (Previous level: %.1fg)\n",
                      completedCycles, actualDispensedGrams, pendingTargetTubeIndex + 1, spices[pendingTargetTubeIndex].name.c_str(), spices[pendingTargetTubeIndex].level);
                      
        spices[pendingTargetTubeIndex].level -= actualDispensedGrams;
        if (spices[pendingTargetTubeIndex].level < 0.0f) spices[pendingTargetTubeIndex].level = 0.0f;
        saveGlobalSpices();
        
        float percentage = (spices[pendingTargetTubeIndex].level / max_fill) * 100.0f;
        if (percentage < 15.0f) {
            bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1, false);
        }
    }
}

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
    String statusMsg = "Ready";
    String detailMsg = "Awaiting command";
    
    switch (currentState) {
        case STATE_MAIN_MENU: 
        case STATE_RECIPE_SELECT:
            stateStr = "idle"; 
            statusMsg = "Ready";
            detailMsg = "Awaiting command";
            break;
        case STATE_BOOT: 
            stateStr = "booting"; 
            statusMsg = "Booting";
            detailMsg = "Initializing system...";
            break;
        case STATE_SYSTEM_CHECK: 
            stateStr = "booting"; 
            statusMsg = "System Check";
            detailMsg = "Scanning Tube " + String(currentTubeIndex + 1) + "/20...";
            break;
        case STATE_ROTATING_TO_TARGET:
            stateStr = isInitialCheck ? "booting" : "busy"; 
            statusMsg = isRecipeMode ? "Searching Spices" : "Rotating";
            detailMsg = isRecipeMode ? "Aligning to next ingredient..." : "Aligning to Slot " + String(pendingTargetTubeIndex + 1) + "...";
            break;
        case STATE_IDENTIFYING:
            stateStr = isInitialCheck ? "booting" : "busy"; 
            statusMsg = "Identifying";
            detailMsg = "Reading TCS3200 sensor...";
            break;
        case STATE_CHECKING_FILL:
            stateStr = isInitialCheck ? "booting" : "busy"; 
            statusMsg = "Checking Level";
            detailMsg = "Reading laser fill sensor...";
            break;
        case STATE_DISPENSING:
            stateStr = "busy";
            statusMsg = "Dispensing";
            if (isRecipeMode) {
                int totalItems = (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount);
                if (currentIngredientIndex >= 0 && currentIngredientIndex < totalItems) {
                    RecipeItem rItem = (currentRecipeIndex == -1 ? remoteRecipe.ingredients[currentIngredientIndex] : recipes[currentRecipeIndex].ingredients[currentIngredientIndex]);
                    detailMsg = "Dispensing " + rItem.spiceName + " (" + String(rItem.quantityGrams, 1) + "g)...";
                } else {
                    detailMsg = "Sweeping dispenser servo...";
                }
            } else {
                detailMsg = "Dispensing " + spices[pendingTargetTubeIndex].name + " (" + String(manualQuantityInput * 2.0, 1) + "g)...";
            }
            break;
        case STATE_EMPTY_RETRY:
            stateStr = "error";
            statusMsg = "Alert";
            detailMsg = "Refill needed or incorrect spice.";
            break;
        case STATE_RETURNING_HOME:
            stateStr = "busy";
            statusMsg = "Homing";
            detailMsg = "Returning carousel to Slot 1...";
            break;
        case STATE_ERROR_RETURN: 
            stateStr = "error"; 
            statusMsg = "System Error";
            detailMsg = "Execution failed. Check hardware.";
            break;
        default: 
            stateStr = "busy"; 
            statusMsg = "Busy";
            detailMsg = "Processing...";
            break;
    }
    
    doc["state"] = stateStr;
    doc["status_msg"] = statusMsg;
    doc["detail"] = detailMsg;
    
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
            
            // Calculate high-precision progressive recipe progress
            int totalItems = (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount);
            if (totalItems > 0) {
                float baseProgress = ((float)currentIngredientIndex / totalItems) * 100.0;
                float itemContribution = (1.0 / totalItems) * 100.0;
                
                float itemProgress = 0.0;
                if (currentState == STATE_DISPENSING) {
                    int currentRem = getRemainingDispenseCycles();
                    int totalCycles = (targetDispenseCycles > 0 ? targetDispenseCycles : 1);
                    itemProgress = (1.0 - (float)currentRem / totalCycles);
                    if (itemProgress < 0.0) itemProgress = 0.0;
                    if (itemProgress > 1.0) itemProgress = 1.0;
                }
                
                int globalProgress = (int)(baseProgress + (itemProgress * itemContribution));
                if (globalProgress < 0) globalProgress = 0;
                if (globalProgress > 100) globalProgress = 100;
                
                doc["progress"] = globalProgress;
            } else {
                doc["progress"] = 0;
            }
        }
    } else if (currentState == STATE_DISPENSING || currentState == STATE_ROTATING_TO_TARGET) {
        doc["active_recipe"] = "Manual";
        
        // Calculate high-precision manual dispense progress
        if (currentState == STATE_DISPENSING) {
            int currentRem = getRemainingDispenseCycles();
            int totalCycles = (targetDispenseCycles > 0 ? targetDispenseCycles : 1);
            float progressFraction = (1.0 - (float)currentRem / totalCycles);
            int percent = (int)(progressFraction * 100.0);
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            doc["progress"] = percent;
        } else {
            doc["progress"] = 0;
        }
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

  Serial.println("[INIT] Loading Cached Recipes...");
  loadRecipesCache();

  Serial.println("[INIT] Initializing Hardware Drivers...");
  initHardware(); 

  Serial.println("[INIT] Initializing Servo Controller...");
  initServo();
  
  Serial.println("[INIT] Initializing Motion Controller...");
  initMotionController();

  lcd.begin(); 
  
  Serial.println("[INIT] Initializing BLE...");
  bleManager.begin("Spice Dispenser");
  
  Serial.println("[BOOT] Updating LCD...");
  lcd.updateContent("Spice Mixer", "Booting...");
  
  Serial.println("[BOOT] Entering STATE_BOOT...");
  changeState(STATE_BOOT); 
}

void loop() {
  // Sync the legacy currentTubeIndex variable with the encapsulated Motion Controller
  if (currentState != STATE_INITIAL_SETUP) {
      currentTubeIndex = getCurrentSlotIndex();
  }

  // --- Serial Command Parser (Factory Reset & Print Configuration) ---
  if (currentState != STATE_INITIAL_SETUP && Serial.available() > 0) {
      char serCmd = Serial.peek();
      if (serCmd == 'F' || serCmd == 'f') {
          Serial.read(); // Consume
          Serial.println("[SERIAL] Received Factory Reset Command!");
          factoryResetDatabase();
      }
      else if (serCmd == 'P' || serCmd == 'p') {
          Serial.read(); // Consume
          Serial.println("\n--- Registered Slots Configuration ---");
          for (int i = 0; i < TOTAL_TUBES; i++) {
              int pct = (int)round((spices[i].level / max_fill) * 100.0f);
              if (pct < 0) pct = 0;
              if (pct > 100) pct = 100;
              Serial.printf("Slot %2d | Name: %-15s | Level: %3d%% (%5.1fg) | Raw Calibration: R:%3d, G:%3d, B:%3d\n",
                            i + 1, 
                            spices[i].name.c_str(), 
                            pct,
                            spices[i].level,
                            spices[i].r_val, 
                            spices[i].g_val, 
                            spices[i].b_val);
          }
          Serial.println("----------------------------------------\n");
      }
  }

  // --- Asynchronous Ticks ---
  tickDispenser();
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
          if (completedCycles < 0) completedCycles = 0;
          if (completedCycles > targetDispenseCycles) completedCycles = targetDispenseCycles;
          
          // Deduct only what was actually dispensed
          applyDispensedSpiceLevel(completedCycles);
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
    case STATE_BOOT: {
      static bool homingStarted = false;
      if (!homingStarted) {
          if (STATE_TIMEOUT(2000)) {
              isRecipeMode = false; 
              currentRecipeIndex = -1;
              currentTubeIndex = 0;
              pendingTargetTubeIndex = 0;
              
              if (!isMachineConfigured) {
                  Serial.println("[BOOT] Machine not configured. Entering Initial Setup...");
                  enableStepperMotor();
                  setupSubState = SETUP_HOMING;
                  startHoming(); // Start physical homing sequence
                  userInput = "";
                  isInitialCheck = false;
                  lcd.showOperationView("HOMING", "Finding Home...", 0, "Calibrating", ""); // Draw once here to prevent continuous redraw slow-stepping
                  changeState(STATE_INITIAL_SETUP);
              } else {
                  lcd.showOperationView("HOMING", "Finding Home...", 0, "Calibration", "");
                  startHoming();
                  homingStarted = true;
              }
          }
      } else {
          if (tickHoming()) {
              currentTubeIndex = 0;
              pendingTargetTubeIndex = 0;
              lcd.showOperationView("SYSTEM READY", spices[0].name, 100, "Aligned & Ready", "");
              delay(1000);
              
              isInitialCheck = false;
              changeState(STATE_MAIN_MENU);
          }
      }
      break;
    }

    case STATE_INITIAL_SETUP: {
      const float SEARCH_SPEED = -250.0;
      
      switch (setupSubState) {
          case SETUP_HOMING: {
              if (tickHoming()) {
                  currentTubeIndex = 0; // Ensure we start at Slot 1
                  setupSubState = SETUP_READ_COLOR; // Go directly to read color for Slot 1 since we are already physically home and aligned!
              }
              break;
          }

          case SETUP_INIT_TUBE: {
              Serial.printf("[SETUP] Starting Slot %d/20 positioning...\n", currentTubeIndex + 1);
              lcd.showOperationView("SETUP: SLOT " + String(currentTubeIndex + 1), 
                                    "Aligning...", 
                                    (int)(((float)currentTubeIndex / TOTAL_TUBES) * 100), 
                                    "Moving carousel", "");
              enableStepperMotor();
              if (isLimitSwitchPressed()) {
                  setupSubState = SETUP_ROTATE_EXITING;
              } else {
                  setupDebounceTimer = millis(); // Initialize search timer
                  setupSubState = SETUP_ROTATE_ENTERING;
              }
              break;
          }

          case SETUP_ROTATE_EXITING: {
              stepper.setSpeed(SEARCH_SPEED);
              stepper.runSpeed();
              if (!isLimitSwitchPressed()) {
                  Serial.println("[SETUP] Cleared current slot. Finding next edge...");
                  setupDebounceTimer = millis(); // Initialize search timer
                  setupSubState = SETUP_ROTATE_ENTERING;
              }
              break;
          }

          case SETUP_ROTATE_ENTERING: {
              stepper.setSpeed(SEARCH_SPEED);
              stepper.runSpeed();
              
              if (!isLimitSwitchPressed()) {
                  setupDebounceTimer = millis(); // Reset timer if switch goes LOW (released)
              }
              
              if (millis() - setupDebounceTimer >= 10) { // Must be pressed continuously for 10ms
                  Serial.println("[SETUP] Limit switch triggered (debounced)! Rotating slowly to release edge...");
                  setupDebounceTimer = millis(); // Initialize release timer
                  setupSubState = SETUP_ROTATE_ALIGNING;
              }
              break;
          }

          case SETUP_ROTATE_ALIGNING: {
              const float SLOW_ALIGN_SPEED = -100.0; // Slower speed for precision centering
              stepper.setSpeed(SLOW_ALIGN_SPEED);
              stepper.runSpeed();
              
              if (isLimitSwitchPressed()) {
                  setupDebounceTimer = millis(); // Reset timer if switch goes HIGH (pressed)
              }
              
              if (millis() - setupDebounceTimer >= 20) { // Must be released continuously for 20ms
                  Serial.println("[SETUP] Limit switch released (debounced). Alignment complete.");
                  stepper.stop();
                  setupSubState = SETUP_READ_COLOR;
              }
              break;
          }

          case SETUP_READ_COLOR: {
              Serial.printf("[SETUP] Bypassing color sensor reading for Slot %d/20...\n", currentTubeIndex + 1);
              spices[currentTubeIndex].r_val = 0;
              spices[currentTubeIndex].g_val = 0;
              spices[currentTubeIndex].b_val = 0;

              Serial.printf("[SETUP] Enter name for Slot %d (Press Enter for default 'Spice_%d'):\n", 
                  currentTubeIndex + 1, currentTubeIndex + 1);

              lcd.showOperationView("INPUT NAME", 
                                    "Slot " + String(currentTubeIndex + 1), 
                                    (int)(((float)currentTubeIndex / TOTAL_TUBES) * 100), 
                                    "Use Serial Mon", "");
              
              // Notify BLE that machine is ready for the slot name input
              JsonDocument readyMsg;
              readyMsg["type"] = "setup_ready_for_slot";
              readyMsg["slot"] = currentTubeIndex + 1;
              readyMsg["r_val"] = spices[currentTubeIndex].r_val;
              readyMsg["g_val"] = spices[currentTubeIndex].g_val;
              readyMsg["b_val"] = spices[currentTubeIndex].b_val;
              bleManager.notifyStatus(readyMsg);

              userInput = "";
              setupSubState = SETUP_AWAITING_NAME;
              break;
          }

          case SETUP_AWAITING_NAME: {
              if (bleSetupNamePending) {
                  Serial.printf("[MAIN LOOP SETUP DEBUG] Detected bleSetupNamePending = true. Name = '%s'\n", bleSetupNameReceived.c_str());
              }
              bool nameReceived = false;
              String inputName = "";
              
              // 1. Check Serial Monitor character-by-character
              static bool lastWasCR = false;
              while (Serial.available() > 0) {
                  char c = Serial.read();
                  if (c == '\n' && lastWasCR) {
                      lastWasCR = false;
                      continue; // Skip the LF if it immediately follows a CR
                  }
                  lastWasCR = (c == '\r');

                  if (c == '\n' || c == '\r') {
                      userInput.trim();
                      inputName = userInput;
                      userInput = "";
                      nameReceived = true;
                      break;
                  } else if (c == 8 || c == 127) { // Backspace
                      if (userInput.length() > 0) {
                          userInput.remove(userInput.length() - 1);
                          Serial.print("\b \b"); // Visually delete character in terminal
                      }
                  } else {
                      userInput += c;
                      Serial.print(c); // Echo back to serial monitor
                  }
              }
              
              // 2. Check BLE Input if no serial input was finalized
              if (!nameReceived && bleSetupNamePending) {
                  inputName = bleSetupNameReceived;
                  bleSetupNamePending = false;
                  nameReceived = true;
                  Serial.printf("[BLE SETUP INPUT] Received name: %s\n", inputName.c_str());
              }

              if (nameReceived) {
                  inputName.trim();
                  
                  // Sanitize name: keep only alphanumeric characters and single spaces
                  String sanitizedName = "";
                  bool lastWasSpace = false;
                  for (unsigned int k = 0; k < inputName.length(); k++) {
                      char ch = inputName[k];
                      if (isalnum(ch)) {
                          sanitizedName += ch;
                          lastWasSpace = false;
                      } else if (isspace(ch) || ch == '_' || ch == '-') {
                          if (!lastWasSpace && sanitizedName.length() > 0) {
                              sanitizedName += ' ';
                              lastWasSpace = true;
                          }
                      }
                  }
                  sanitizedName.trim();

                  String finalName = sanitizedName;
                  if (finalName.length() == 0) {
                      finalName = "Spice_" + String(currentTubeIndex + 1);
                  }

                  // Verify uniqueness among slots 0 to currentTubeIndex - 1
                  bool isDuplicate = false;
                  for (int s = 0; s < currentTubeIndex; s++) {
                      String existingUpper = spices[s].name;
                      existingUpper.toUpperCase();
                      String checkUpper = finalName;
                      checkUpper.toUpperCase();
                      if (existingUpper == checkUpper) {
                          isDuplicate = true;
                          break;
                      }
                  }

                  if (isDuplicate) {
                      Serial.printf("\n[ERROR] Name '%s' is already assigned to a slot! All spice tube names must be unique.\n", finalName.c_str());
                      
                      // Notify BLE that name registration failed
                      JsonDocument nak;
                      nak["type"] = "ack";
                      nak["command"] = "setup_slot_name";
                      nak["status"] = "fail";
                      nak["reason"] = "duplicate_name";
                      nak["detail"] = finalName;
                      bleManager.notifyStatus(nak);
                      
                      Serial.printf("[SETUP] Please enter a UNIQUE name for Slot %d (Press Enter for default 'Spice_%d'):\n", 
                                    currentTubeIndex + 1, currentTubeIndex + 1);
                      break;
                  }

                  spices[currentTubeIndex].name = finalName;
                  spices[currentTubeIndex].level = max_fill;
                  spices[currentTubeIndex].expiry = 0; // Default to 0

                  Serial.printf("\n[SETUP] Slot %d successfully mapped to '%s'\n", currentTubeIndex + 1, finalName.c_str());

                  // Keep MotionController's slot index in perfect sync during setup!
                  setCurrentSlotIndex(currentTubeIndex);

                  // Notify BLE of successful mapping
                  JsonDocument ack;
                  ack["type"] = "ack";
                  ack["command"] = "setup_slot_name";
                  ack["status"] = "success";
                  ack["slot"] = currentTubeIndex + 1;
                  ack["name"] = finalName;
                  bleManager.notifyStatus(ack);

                  currentTubeIndex++;
                  if (currentTubeIndex < TOTAL_TUBES) {
                      setupSubState = SETUP_INIT_TUBE;
                  } else {
                      Serial.println("[SETUP] All slots configured. Saving to LittleFS...");
                      saveGlobalSpices();
                      disableStepperMotor();
                      prepareReturnHome();
                  }
                  break;
              }
              break;
          }
      }
      break;
    }

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
        
        Serial.printf("[SENSOR] Expected Slot: %d (%s) | Redundant Color Detected: %s\n", 
                      pendingTargetTubeIndex + 1, expectedSpice.c_str(), detectedSpice.c_str());

        // Perform matchmaking based on physical index tracking
        if (getCurrentSlotIndex() == pendingTargetTubeIndex) {
           if (isInitialCheck) {
               // Surgical updates for boot
               lcd.updateSpiceName(spices[pendingTargetTubeIndex].name);
               lcd.updateTask("VERIFIED");
               
               int nextProgress = (int)(((float)(currentTubeIndex + 1) / TOTAL_TUBES) * 100);
               lcd.drawProgressBar(nextProgress, 160);
               
               delay(100);

               if (STATE_TIMEOUT(simulationEnabled ? 50 : 800)) {
                   currentTubeIndex++;
                   if (currentTubeIndex < TOTAL_TUBES) {
                       pendingTargetTubeIndex = currentTubeIndex;
                       startRotationToSlot(currentTubeIndex);
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
                lcd.updateContent("MISMATCH!", "Incorrect Index");
                lcd.updateStatus("Expected Slot " + String(pendingTargetTubeIndex + 1), ILI9341_RED);
           } else {
                lcd.showOperationView("MISMATCH!", "Alignment Error", 0, "ERROR: ALIGNMENT ERR", "Exit");
           }
           bleManager.sendAlert("wrong_spice", pendingTargetTubeIndex + 1, true);
           if (STATE_TIMEOUT(3000)) {
               if (isInitialCheck) changeState(STATE_EMPTY_RETRY); 
               else prepareReturnHome();
           }
        }
      } else if (!isInitialCheck) {
          lcd.updateTask("IDENTIFYING...");
          lcd.updateDetail("Verifying Index");
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
           
           // Pre-validate all ingredients in selected recipe
           bool allIngredientsAvailable = true;
           String missingOrLowSpice = "";
           bool isLow = false;
           
           for (int i = 0; i < recipes[currentRecipeIndex].ingredientCount; i++) {
               RecipeItem rItem = recipes[currentRecipeIndex].ingredients[i];
               int targetSlot = findSlotByName(rItem.spiceName);
               if (targetSlot == -1) {
                   allIngredientsAvailable = false;
                   missingOrLowSpice = rItem.spiceName;
                   isLow = false;
                   break;
               } else {
                   float requestedGrams = rItem.quantityGrams * servingsCount;
                   float availableGrams = spices[targetSlot].level;
                   if (requestedGrams > availableGrams) {
                       allIngredientsAvailable = false;
                       missingOrLowSpice = spices[targetSlot].name;
                       isLow = true;
                       break;
                   }
               }
           }
           
           if (!allIngredientsAvailable) {
               if (isLow) {
                   Serial.printf("[ERROR] Local Recipe rejected: Insufficient spice for '%s'.\n", missingOrLowSpice.c_str());
                   lcd.showOperationView("EMPTY!", missingOrLowSpice, 0, "Insuff. Spice", "Exit");
                   bleManager.sendAlert("low_spice", findSlotByName(missingOrLowSpice) + 1, true);
               } else {
                   Serial.printf("[ERROR] Local Recipe rejected: Missing spice '%s' on machine.\n", missingOrLowSpice.c_str());
                   lcd.showOperationView("ERROR!", "Missing Ingredient", 0, missingOrLowSpice, "Exit");
                   bleManager.sendAlert("missing_ingredient", 0, true);
               }
               changeState(STATE_EMPTY_RETRY);
               break;
           }

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
              
              // Calculate requested grams and verify loaded spice capacity
              float requestedGrams = targetDispenseCycles * GRAMS_PER_CYCLE;
              float availableGrams = spices[pendingTargetTubeIndex].level;
              
              if (requestedGrams > availableGrams) {
                  Serial.printf("[ERROR] Manual dispense rejected: Needs %.1fg but only %.1fg available in Slot %d '%s'.\n", 
                                requestedGrams, availableGrams, pendingTargetTubeIndex + 1, spices[pendingTargetTubeIndex].name.c_str());
                  lcd.showOperationView("EMPTY!", spices[pendingTargetTubeIndex].name, 0, "Insuff. Spice", "Exit");
                  bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1, true);
                  changeState(STATE_EMPTY_RETRY);
                  break;
              }
              
              if (pendingTargetTubeIndex != getCurrentSlotIndex()) {
                Serial.printf("[MANUAL] Rotating to target %d...\n", pendingTargetTubeIndex + 1);
                lcd.showOperationView("ROTATING", "Slot " + String(pendingTargetTubeIndex + 1), 0, "Motor Running", "Abort");
                startRotationToSlot(pendingTargetTubeIndex);
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
          if (isRecipeMode) {
              lcd.updateTask("Searching Spices");
              lcd.updateDetail("Scanning slots...");
          } else {
              lcd.updateTask("Moving to Slot " + String(pendingTargetTubeIndex + 1));
              lcd.updateDetail("Slots left: " + String(getSlotsRemainingToMove())); 
          }
      }
      
      if (tickRotation()) {
          currentTubeIndex = getCurrentSlotIndex();
          if (isRecipeMode) {
              startRecipeSearch();
          } else {
              if (STATE_TIMEOUT(100)) {
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
          if (laserSensor.isFilled()) { 
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
            bleManager.sendAlert("low_spice", pendingTargetTubeIndex + 1, true);
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
          
          // Deduct level using completed cycles
          applyDispensedSpiceLevel(targetDispenseCycles);

          if (isRecipeMode) {
            ingredientDispensed[currentIngredientIndex] = true; // Mark as dispensed!
            
            // Check if any ingredients remain undispensed
            int count = (currentRecipeIndex == -1) ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount;
            bool anyLeft = false;
            for (int i = 0; i < count; i++) {
                if (!ingredientDispensed[i]) {
                    anyLeft = true;
                    break;
                }
            }
            
            if (anyLeft) {
               startRecipeSearch();
            } else {
               // Send the final 100% complete status packet
               JsonDocument doc;
               doc["type"] = "status";
               doc["state"] = "idle";
               doc["active_recipe"] = (currentRecipeIndex == -1 ? "Remote" : recipes[currentRecipeIndex].name);
               doc["progress"] = 100;
               bleManager.notifyStatus(doc);
               
               lcd.showOperationView("COMPLETE", "Recipe Finished", 100, "Enjoy!", "Back");
               delay(1500);
               isRecipeMode = false;
               prepareReturnHome();
            }
          } else {
            // Send final 100% complete status packet for manual mode
            JsonDocument doc;
            doc["type"] = "status";
            doc["state"] = "idle";
            doc["active_recipe"] = "Manual";
            doc["progress"] = 100;
            bleManager.notifyStatus(doc);

            lcd.showOperationView("COMPLETE", "Manual Done", 100, "Enjoy!", "Back");
            delay(1500);
            changeState(STATE_MAIN_MENU);
            disableStepperMotor();
          } 
          
      }
      break;
    }

    case STATE_RETURNING_HOME: {
      if (tickHoming()) {
          currentTubeIndex = 0;
          disableStepperMotor();
          isRecipeMode = false;
          changeState(STATE_MAIN_MENU);
      }
      break;
    }
    
    default: break;
  }
}

void startRecipeSearch() {
   int currentSlot = getCurrentSlotIndex();
   Serial.printf("[RECIPE] Scanning Slot %d in search loop (Step %d/20)...\n", currentSlot + 1, searchStepCount + 1);
   
   String currentSpiceName = spices[currentSlot].name;
   currentSpiceName.toUpperCase();
   
   int totalIngredients = (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount);
   bool matchFound = false;
   
   for (int i = 0; i < totalIngredients; i++) {
       if (ingredientDispensed[i]) continue;
       
       RecipeItem rItem = (currentRecipeIndex == -1 ? remoteRecipe.ingredients[i] : recipes[currentRecipeIndex].ingredients[i]);
       String targetSpiceName = rItem.spiceName;
       targetSpiceName.toUpperCase();
       
       // Confirm match if current slot name matches target ingredient name (handling substrings or exact matches)
       if (currentSpiceName.length() > 0 && 
           (currentSpiceName == targetSpiceName || 
            currentSpiceName.indexOf(targetSpiceName) != -1 || 
            targetSpiceName.indexOf(currentSpiceName) != -1)) {
           
           pendingTargetTubeIndex = currentSlot;
           currentIngredientIndex = i;
           currentRecipeGrams = rItem.quantityGrams * servingsCount;
           targetDispenseCycles = (int)(currentRecipeGrams / GRAMS_PER_CYCLE);
           
           Serial.printf("[RECIPE] MATCH CONFIRMED (Index Tracking)! Slot %d holding '%s' matches recipe ingredient '%s'!\n", 
                         currentSlot + 1, spices[currentSlot].name.c_str(), rItem.spiceName.c_str());
           matchFound = true;
           changeState(STATE_CHECKING_FILL);
           return;
       }
   }
   
   // If no match found, check if we searched a full circle (20 slots)
   searchStepCount++;
   if (searchStepCount >= TOTAL_TUBES) {
       Serial.println("[RECIPE] [ERROR] Full circle searched but could not find all recipe ingredients!");
       lcd.showOperationView("ERROR!", "Ingredient Missing", 0, "Recipe Terminated", "Exit");
       bleManager.sendAlert("missing_ingredient", 0, true);
       isRecipeMode = false;
       changeState(STATE_MAIN_MENU);
       return;
   }
   
   // Rotate to next slot
   int nextSlot = (currentSlot + 1) % TOTAL_TUBES;
   startRotationToSlot(nextSlot);
   changeState(STATE_ROTATING_TO_TARGET);
}

void startRecipeIngredient() {
   // Initialize dispensed flags and counter on starting a new recipe run
   if (currentIngredientIndex == 0) {
       for (int i = 0; i < 10; i++) {
           ingredientDispensed[i] = false;
       }
       searchStepCount = 0;
       Serial.println("[RECIPE] Starting Opportunistic Out-of-Order Dispensing Flow...");
   }
   
   startRecipeSearch();
}

void prepareReturnHome() {
    currentTubeIndex = getCurrentSlotIndex();
    if (currentTubeIndex != 0) { 
        Serial.printf("[MOTOR] Returning home from Tube %d...\n", currentTubeIndex + 1);
        lcd.showOperationView("FINISHING", "Returning Home", 0, "Homing to Slot 1", "");
        startHoming();
        changeState(STATE_RETURNING_HOME);
    } else { 
        isRecipeMode = false;
        changeState(STATE_MAIN_MENU);
        disableStepperMotor();
    }
}

int findSlotByName(String targetName) {
    targetName.toUpperCase();
    for (int i = 0; i < TOTAL_TUBES; i++) {
        String name = spices[i].name;
        name.toUpperCase();
        if (name.indexOf(targetName) != -1) {
            return i;
        }
    }
    return -1;
}

