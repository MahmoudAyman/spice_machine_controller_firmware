/*
  Spice Mixer - Production V3.4
*/

#include "Configuration.h"
#include "Database.h"
#include "Globals.h"
#include "Hardware.h"
#include "MotionController.h"

// --- Global Object Definitions ---
LCDManager lcd;
Servo dispenserServo;
ColorDetector colorDetector = ColorDetector(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);
BLEManager bleManager;
LaserLevelSensor laserSensor(LASER_RX_PIN);

// --- Logic Variables ---
bool simulationEnabled = (SIMULATION_MODE == 1);
SystemState currentState = STATE_BOOT;
String userInput = "";
bool ingredientDispensed[10] = {false};
int searchStepCount = 0;

enum SetupState {
    SETUP_INIT_TUBE,
    SETUP_ROTATE_EXITING,
    SETUP_ROTATE_ENTERING,
    SETUP_ROTATE_ALIGNING,
    SETUP_READ_COLOR,
    SETUP_AWAITING_NAME
};
SetupState setupSubState = SETUP_INIT_TUBE;
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

  // Initialize default volatile diagnostic recipe
  activeRecipeCount = 1;
  recipes[0].id = "def_1";
  recipes[0].name = "Motion Test";
  recipes[0].ingredientCount = 2;
  recipes[0].ingredients[0].spiceName = "BLACK";
  recipes[0].ingredients[0].quantityGrams = 1.0;
  recipes[0].ingredients[1].spiceName = "BLUE";
  recipes[0].ingredients[1].quantityGrams = 1.0;

  Serial.println("[INIT] Initializing Hardware Drivers...");
  initHardware(); 
  
  Serial.println("[INIT] Initializing Motion Controller...");
  initMotionController();

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
              Serial.printf("Slot %2d | Name: %-15s | Level: %3d%% | Raw Calibration: R:%3d, G:%3d, B:%3d\n",
                            i + 1, 
                            spices[i].name.c_str(), 
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
    case STATE_BOOT: {
      if (STATE_TIMEOUT(2000)) {
          isRecipeMode = false; 
          currentRecipeIndex = -1;
          currentTubeIndex = 0;
          pendingTargetTubeIndex = 0;
          
          if (!isMachineConfigured) {
              Serial.println("[BOOT] Machine not configured. Entering Initial Setup...");
              enableStepperMotor();
              setupSubState = SETUP_INIT_TUBE;
              userInput = "";
              isInitialCheck = false;
              lcd.showOperationView("INITIAL SETUP", "Welcome!", 0, "Initializing...", "");
              changeState(STATE_INITIAL_SETUP);
          } else {
              startBootRecoveryAlignment();
          }
      } else if (isMachineConfigured && currentState == STATE_BOOT) {
          int matchedSlot = 0;
          if (tickBootRecovery(matchedSlot)) {
              currentTubeIndex = matchedSlot;
              pendingTargetTubeIndex = matchedSlot;
              lcd.showOperationView("SYSTEM READY", spices[matchedSlot].name, 100, "Aligned & Ready", "");
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
                  setupDebounceTimer = millis(); // Reset timer if switch goes low
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
                  setupDebounceTimer = millis(); // Reset timer if switch goes high
              }
              
              if (millis() - setupDebounceTimer >= 20) { // Must be released continuously for 20ms
                  Serial.println("[SETUP] Limit switch released (debounced). Alignment complete.");
                  stepper.stop();
                  setupSubState = SETUP_READ_COLOR;
              }
              break;
          }

          case SETUP_READ_COLOR: {
              Serial.printf("[SETUP] Reading sensor raw pulse widths for Slot %d/20...\n", currentTubeIndex + 1);
              lcd.showOperationView("SETUP: SLOT " + String(currentTubeIndex + 1), 
                                    "Calibrating...", 
                                    (int)(((float)currentTubeIndex / TOTAL_TUBES) * 100), 
                                    "Reading Sensor", "");
              long sumR = 0, sumG = 0, sumB = 0;
              for (int i = 0; i < 3; i++) {
                  sumR += colorDetector.readRawColor('r'); delay(15);
                  sumG += colorDetector.readRawColor('g'); delay(15);
                  sumB += colorDetector.readRawColor('b'); delay(15);
              }
              spices[currentTubeIndex].r_val = sumR / 3;
              spices[currentTubeIndex].g_val = sumG / 3;
              spices[currentTubeIndex].b_val = sumB / 3;

              Serial.printf("[SETUP] Slot %d raw pulse widths -> R: %d | G: %d | B: %d\n", 
                  currentTubeIndex + 1, spices[currentTubeIndex].r_val, spices[currentTubeIndex].g_val, spices[currentTubeIndex].b_val);

              Serial.printf("[SETUP] Enter name for Slot %d (Press Enter for default 'Spice_%d'):\n", 
                  currentTubeIndex + 1, currentTubeIndex + 1);

              lcd.showOperationView("INPUT NAME", 
                                    "Slot " + String(currentTubeIndex + 1), 
                                    (int)(((float)currentTubeIndex / TOTAL_TUBES) * 100), 
                                    "Use Serial Mon", "");
              
              userInput = "";
              setupSubState = SETUP_AWAITING_NAME;
              break;
          }

          case SETUP_AWAITING_NAME: {
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
                      
                      // Sanitize name: keep only alphanumeric characters and single spaces
                      String sanitizedName = "";
                      bool lastWasSpace = false;
                      for (unsigned int k = 0; k < userInput.length(); k++) {
                          char ch = userInput[k];
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
                          Serial.printf("[SETUP] Please enter a UNIQUE name for Slot %d (Press Enter for default 'Spice_%d'):\n", 
                                        currentTubeIndex + 1, currentTubeIndex + 1);
                          userInput = "";
                          break;
                      }

                      spices[currentTubeIndex].name = finalName;
                      spices[currentTubeIndex].level = 100;

                      Serial.printf("\n[SETUP] Slot %d successfully mapped to '%s'\n", currentTubeIndex + 1, finalName.c_str());

                      currentTubeIndex++;
                      if (currentTubeIndex < TOTAL_TUBES) {
                          setupSubState = SETUP_INIT_TUBE;
                      } else {
                          Serial.println("[SETUP] All slots configured. Saving to LittleFS...");
                          saveGlobalSpices();
                          disableStepperMotor();
                          prepareReturnHome();
                      }
                      userInput = "";
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
        
        Serial.printf("[SENSOR] Expected: %s | Detected: %s\n", expectedSpice.c_str(), detectedSpice.c_str());

        String dUpper = detectedSpice;
        dUpper.toUpperCase();
        String eUpper = expectedSpice;
        eUpper.toUpperCase();

        if (dUpper == eUpper || eUpper.indexOf(dUpper) != -1 || dUpper.indexOf(eUpper) != -1) {
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
               lcd.showOperationView("COMPLETE", "Recipe Finished", 100, "Enjoy!", "Back");
               delay(1500);
               isRecipeMode = false;
               changeState(STATE_MAIN_MENU);
               disableStepperMotor();
            }
          } else {
            lcd.showOperationView("COMPLETE", "Manual Done", 100, "Enjoy!", "Back");
            delay(1500);
            changeState(STATE_MAIN_MENU);
            disableStepperMotor();
          } 
          
      }
      break;
    }

    case STATE_RETURNING_HOME: {
      lcd.showOperationView("HOMING", "Moving to Slot 1", 50, "Clearing Area", "");
      if (tickRotation()) {
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
   
   long rawR = 0, rawG = 0, rawB = 0;
   bool matchFound = false;
   
   // Read current slot color raw values
   for (int i = 0; i < 3; i++) {
       rawR += colorDetector.readRawColor('r'); delay(15);
       rawG += colorDetector.readRawColor('g'); delay(15);
       rawB += colorDetector.readRawColor('b'); delay(15);
   }
   rawR /= 3; rawG /= 3; rawB /= 3;
   
   int totalIngredients = (currentRecipeIndex == -1 ? remoteRecipe.ingredientCount : recipes[currentRecipeIndex].ingredientCount);
   for (int i = 0; i < totalIngredients; i++) {
       if (ingredientDispensed[i]) continue;
       
       RecipeItem rItem = (currentRecipeIndex == -1 ? remoteRecipe.ingredients[i] : recipes[currentRecipeIndex].ingredients[i]);
       
       // Search all database slots to see if our raw reading matches the calibrated slot holding this ingredient
       int targetSlot = findSlotByName(rItem.spiceName);
       if (targetSlot != -1) {
           long diff = abs(spices[targetSlot].r_val - rawR) + 
                       abs(spices[targetSlot].g_val - rawG) + 
                       abs(spices[targetSlot].b_val - rawB);
           
           Serial.printf("[RECIPE] Comparing Slot %d Raw (R:%d, G:%d, B:%d) to Database Slot %d '%s' Calibration (R:%d, G:%d, B:%d) | diff: %ld\n",
                         currentSlot + 1, rawR, rawG, rawB, targetSlot + 1, rItem.spiceName.c_str(), spices[targetSlot].r_val, spices[targetSlot].g_val, spices[targetSlot].b_val, diff);

           if (diff <= 15) {
               pendingTargetTubeIndex = currentSlot;
               currentIngredientIndex = i;
               currentRecipeGrams = rItem.quantityGrams * servingsCount;
               targetDispenseCycles = (int)(currentRecipeGrams / GRAMS_PER_CYCLE);
               
               Serial.printf("[RECIPE] MATCH CONFIRMED! Slot %d matches calibrated values for '%s' (diff: %ld)!\n", currentSlot + 1, rItem.spiceName.c_str(), diff);
               matchFound = true;
               changeState(STATE_CHECKING_FILL);
               return;
           } else {
               Serial.printf("[RECIPE] Current Slot %d does not match '%s' calibration (diff: %ld)\n", currentSlot + 1, rItem.spiceName.c_str(), diff);
           }
       }
   }
   
   // If no match found, check if we searched a full circle (20 slots)
   searchStepCount++;
   if (searchStepCount >= TOTAL_TUBES) {
       Serial.println("[RECIPE] [ERROR] Full circle searched but could not find all recipe ingredients!");
       lcd.showOperationView("ERROR!", "Ingredient Missing", 0, "Recipe Terminated", "Exit");
       bleManager.sendAlert("missing_ingredient", 0);
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


