#include "MotionController.h"
#include "Globals.h"
#include "Configuration.h"
#include "Database.h"
#include "Hardware.h"

// --- Global Stepper Instance ---
// Placed here so it is encapsulated, but still available to other modules via Globals.h
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);

// --- Private Motion Controller State ---
static RotationState currentRotState = ROT_IDLE;
static BootAlignState currentBootState = BOOT_ALIGN_IDLE;
static HomingState currentHomeState = HOME_IDLE;

static int mcCurrentSlotIndex = 0;
static int mcTargetSlotIndex = 0;
static int mcSlotsRemainingToMove = 0;
static unsigned long mcDebounceTimer = 0;

static const float SEARCH_SPEED = -250.0;
static const float SLOW_ALIGN_SPEED = -50.0;

void initMotionController() {
    Serial.println("[MC] Initializing Motion Controller...");
    stepper.setMaxSpeed(400);
    stepper.setAcceleration(200);
    stepper.setPinsInverted(true, false, false);
    currentRotState = ROT_IDLE;
    currentBootState = BOOT_ALIGN_IDLE;
    currentHomeState = HOME_IDLE;
    mcCurrentSlotIndex = 0;
    mcSlotsRemainingToMove = 0;
}

void startRotationToSlot(int targetSlotIndex) {
    if (targetSlotIndex < 0 || targetSlotIndex >= NUM_SPICES) {
        Serial.printf("[MC] [ERROR] Invalid target slot index %d!\n", targetSlotIndex);
        return;
    }
    
    mcTargetSlotIndex = targetSlotIndex;
    int slotsToMove = (targetSlotIndex >= mcCurrentSlotIndex) ? 
                      (targetSlotIndex - mcCurrentSlotIndex) : 
                      (TOTAL_TUBES - mcCurrentSlotIndex + targetSlotIndex);
                      
    if (slotsToMove == 0) {
        Serial.printf("[MC] Already at target Slot %d. No rotation needed.\n", targetSlotIndex + 1);
        mcSlotsRemainingToMove = 0;
        currentRotState = ROT_IDLE;
        return;
    }
    
    Serial.printf("[MC] Requesting rotation of %d slots from Slot %d to Slot %d...\n", 
                  slotsToMove, mcCurrentSlotIndex + 1, targetSlotIndex + 1);
                  
    mcSlotsRemainingToMove = slotsToMove;
    enableStepperMotor();
    
    currentRotState = isLimitSwitchPressed() ? ROT_EXITING : ROT_ENTERING;
    mcDebounceTimer = millis();
}

bool tickRotation() {
    switch (currentRotState) {
        case ROT_IDLE:
            return true;
            
        case ROT_EXITING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            if (!isLimitSwitchPressed()) {
                Serial.println("[MC] Cleared slot protrusion. Searching for next edge...");
                mcDebounceTimer = millis();
                currentRotState = ROT_ENTERING;
            }
            break;
        }
        
        case ROT_ENTERING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            
            if (!isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 10) { // Pressed for 10ms
                mcSlotsRemainingToMove--;
                mcCurrentSlotIndex = (mcCurrentSlotIndex + 1) % TOTAL_TUBES;
                Serial.printf("[MC] Passed Slot %d. Slots remaining: %d\n", mcCurrentSlotIndex + 1, mcSlotsRemainingToMove);
                
                if (mcSlotsRemainingToMove > 0) {
                    currentRotState = ROT_EXITING;
                } else {
                    Serial.println("[MC] Arrived at target slot. Centering slowly...");
                    mcDebounceTimer = millis();
                    currentRotState = ROT_ALIGNING;
                }
            }
            break;
        }
        
        case ROT_ALIGNING: {
            stepper.setSpeed(SLOW_ALIGN_SPEED);
            stepper.runSpeed();
            
            if (isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 20) { // Released for 20ms
                stepper.stop();
                Serial.println("[MC] Target centering alignment complete.");
                currentRotState = ROT_IDLE;
                return true; // Finished!
            }
            break;
        }
    }
    return false;
}

void startBootRecoveryAlignment() {
    Serial.println("[MC] Starting Boot recovery alignment...");
    currentBootState = BOOT_ALIGN_INIT;
}

bool tickBootRecovery(int &matchedSlotIndex) {
    switch (currentBootState) {
        case BOOT_ALIGN_IDLE:
            return true;
            
        case BOOT_ALIGN_INIT: {
            enableStepperMotor();
            if (isLimitSwitchPressed()) {
                currentBootState = BOOT_ALIGN_EXITING;
            } else {
                mcDebounceTimer = millis();
                currentBootState = BOOT_ALIGN_ENTERING;
            }
            break;
        }
        
        case BOOT_ALIGN_EXITING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            if (!isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
                currentBootState = BOOT_ALIGN_ENTERING;
            }
            break;
        }
        
        case BOOT_ALIGN_ENTERING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            if (!isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            if (millis() - mcDebounceTimer >= 10) { // Pressed for 10ms
                mcDebounceTimer = millis();
                currentBootState = BOOT_ALIGN_ALIGNING;
            }
            break;
        }
        
        case BOOT_ALIGN_ALIGNING: {
            stepper.setSpeed(SLOW_ALIGN_SPEED);
            stepper.runSpeed();
            if (isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            if (millis() - mcDebounceTimer >= 20) { // Released for 20ms
                stepper.stop();
                currentBootState = BOOT_ALIGN_READ_AND_MATCH;
            }
            break;
        }
        
        case BOOT_ALIGN_READ_AND_MATCH: {
            Serial.println("[MC] Centered. Bypassing color sensor discovery...");
            
            mcCurrentSlotIndex = 0;
            matchedSlotIndex = 0;
            
            disableStepperMotor();
            currentBootState = BOOT_ALIGN_IDLE;
            return true; // Complete!
        }
    }
    return false;
}

void startHoming() {
    Serial.println("[MC] Starting Absolute Homing routine...");
    enableStepperMotor();
    currentHomeState = HOME_INIT;
}

bool tickHoming() {
    switch (currentHomeState) {
        case HOME_IDLE:
            return true;
            
        case HOME_INIT: {
            if (isHomingSwitchPressed()) {
                Serial.println("[MC] Homing switch is already pressed. Moving off...");
                currentHomeState = HOME_EXITING;
            } else {
                Serial.println("[MC] Homing switch is released. Searching fast...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ENTERING;
            }
            break;
        }
        
        case HOME_EXITING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            if (!isHomingSwitchPressed()) {
                Serial.println("[MC] Cleared homing switch. Initiating search...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ENTERING;
            }
            break;
        }
        
        case HOME_ENTERING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            
            if (!isHomingSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 10) { // Pressed continuously for 10ms
                Serial.println("[MC] Homing switch triggered! Aligning slowly...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ALIGNING;
            }
            break;
        }
        
        case HOME_ALIGNING: {
            stepper.setSpeed(SLOW_ALIGN_SPEED);
            stepper.runSpeed();
            
            if (isHomingSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 20) { // Released continuously for 20ms
                Serial.println("[MC] Homing switch released. Moving to next slot alignment...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ALIGN_NEXT_SLOT_ENTERING;
            }
            break;
        }

        case HOME_ALIGN_NEXT_SLOT_ENTERING: {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            
            if (!isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 10) { // Pressed for 10ms
                Serial.println("[MC] Next slot alignment switch triggered. Centering slowly...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ALIGN_NEXT_SLOT_ALIGNING;
            }
            break;
        }

        case HOME_ALIGN_NEXT_SLOT_ALIGNING: {
            stepper.setSpeed(SLOW_ALIGN_SPEED);
            stepper.runSpeed();
            
            if (isLimitSwitchPressed()) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 20) { // Released continuously for 20ms
                stepper.stop();
                Serial.println("[MC] Absolute Homing next-slot alignment complete! Index reset to Slot 1 (0).");
                mcCurrentSlotIndex = 0; // Absolute home represents Slot 1
                currentHomeState = HOME_IDLE;
                disableStepperMotor();
                return true; // Complete!
            }
            break;
        }
    }
    return false;
}

RotationState getRotationState() { return currentRotState; }
int getSlotsRemainingToMove() { return mcSlotsRemainingToMove; }
int getCurrentSlotIndex() { return mcCurrentSlotIndex; }
void setCurrentSlotIndex(int slotIndex) { mcCurrentSlotIndex = slotIndex; }

void disableStepperMotor() {
    if (STEP_ENABLE_PIN != -1) {
        stepper.disableOutputs();
    }
}

void enableStepperMotor() {
    if (STEP_ENABLE_PIN != -1) {
        stepper.enableOutputs();
    }
}
