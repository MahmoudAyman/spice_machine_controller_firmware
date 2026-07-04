#include <Arduino.h>
#include "Configuration.h"
#include "Database.h"
#include "ColorDetector.h"
#include "Hardware.h"
#include <AccelStepper.h>

// Instantiate objects with the exact names expected by Hardware.cpp / Database.h
bool simulationEnabled = (SIMULATION_MODE == 1);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);
ColorDetector colorDetector(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);
Servo dispenserServo;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n==============================================");
    Serial.println("  CLOSED-LOOP DATABASE-SEARCH RECIPE TEST");
    Serial.println("==============================================");
    
    Serial.println("[INIT] Initializing Storage...");
    if (!initStorage()) {
        Serial.println("[ERROR] Storage Initialization Failed!");
    }
    
    Serial.println("[INIT] Loading Global Spices...");
    loadGlobalSpices();

    Serial.println("[INIT] Initializing Hardware Drivers...");
    initHardware(); 
    
    Serial.println("[INIT] Configuring Color Detector...");
    colorDetector.setCalibration(WHITE_R, WHITE_G, WHITE_B, BLACK_R, BLACK_G, BLACK_B);
    
    Serial.println("\n==============================================");
    Serial.println("Ready! Commands:");
    Serial.println("  'p' : Print all database tube mappings & calibrated RGBs");
    Serial.println("  'r' : Run test recipe (1g of 'BLACK' + 1g of 'BLUE')");
    Serial.println("==============================================");
}

// Helper to search database slots for a spice by name
int findSlotByName(String targetName) {
    targetName.toUpperCase();
    for (int i = 0; i < NUM_SPICES; i++) {
        String name = spices[i].name;
        name.toUpperCase();
        if (name.indexOf(targetName) != -1) {
            return i;
        }
    }
    return -1;
}

// Closed-loop rotation & sensor matching based on database calibration values
bool searchAndAlignToSpiceSlot(String targetSpiceName) {
    targetSpiceName.toUpperCase();
    Serial.printf("\n[SEARCH] Starting closed-loop search for spice '%s'...\n", targetSpiceName.c_str());
    
    int targetSlot = findSlotByName(targetSpiceName);
    if (targetSlot == -1) {
        Serial.printf("[ERROR] Spice '%s' is not registered in the database! Please run configuration setup first.\n", targetSpiceName.c_str());
        return false;
    }
    
    // Read the reference calibration values stored in the database during setup
    int refR = spices[targetSlot].r_val;
    int refG = spices[targetSlot].g_val;
    int refB = spices[targetSlot].b_val;
    
    Serial.printf("[SEARCH] Database Slot %d matches '%s'. Calibrated Reference -> R:%d, G:%d, B:%d\n", 
                  targetSlot + 1, targetSpiceName.c_str(), refR, refG, refB);
                  
    const float SEARCH_SPEED = -150.0;
    const float SLOW_ALIGN_SPEED = -50.0;
    
    // Search through up to 20 slots (a full circle)
    for (int searchStep = 0; searchStep < 20; searchStep++) {
        Serial.printf("[SEARCH] Advancing to next physical slot (Slot %d/20 in search stream)...\n", searchStep + 1);
        
        // 1. Exiting current slot protrusion if limit switch is pressed
        if (digitalRead(LIMIT_SWITCH_PIN) == HIGH) {
            while (digitalRead(LIMIT_SWITCH_PIN) == HIGH) {
                stepper.setSpeed(SEARCH_SPEED);
                stepper.runSpeed();
                yield();
            }
            delay(50); // Small delay to settle
        }
        
        // 2. Search for the next rising edge
        unsigned long debounceStart = millis();
        while (true) {
            stepper.setSpeed(SEARCH_SPEED);
            stepper.runSpeed();
            yield();
            
            if (digitalRead(LIMIT_SWITCH_PIN) == LOW) {
                debounceStart = millis(); // Reset if switch is LOW
            }
            if (millis() - debounceStart >= 10) { // Continuous high for 10ms
                break;
            }
        }
        
        // 3. Align slowly until the switch releases
        Serial.println("[MOTION] Trigger found! Centering slowly...");
        debounceStart = millis();
        while (true) {
            stepper.setSpeed(SLOW_ALIGN_SPEED);
            stepper.runSpeed();
            yield();
            
            if (digitalRead(LIMIT_SWITCH_PIN) == HIGH) {
                debounceStart = millis(); // Reset if switch is HIGH
            }
            if (millis() - debounceStart >= 20) { // Continuous low for 20ms
                break;
            }
        }
        
        stepper.stop();
        Serial.println("[MOTION] Aligned. Reading tube color sensor...");
        delay(300); // Settle
        
        // 4. Read color sensor raw pulse widths
        long sumR = 0, sumG = 0, sumB = 0;
        for (int i = 0; i < 3; i++) {
            sumR += colorDetector.readRawColor('r'); delay(15);
            sumG += colorDetector.readRawColor('g'); delay(15);
            sumB += colorDetector.readRawColor('b'); delay(15);
        }
        int avgR = sumR / 3;
        int avgG = sumG / 3;
        int avgB = sumB / 3;
        
        Serial.printf("[SENSOR] Captured Raw: R:%d, G:%d, B:%d\n", avgR, avgG, avgB);
        
        // 5. Match with target reference
        long diff = abs(avgR - refR) + abs(avgG - refG) + abs(avgB - refB);
        Serial.printf("[SENSOR] Difference from '%s' calibrated values: %ld\n", targetSpiceName.c_str(), diff);
        
        if (diff <= 150) { // Matching threshold (allow up to 150 raw pulse-width variance)
            Serial.printf("[SEARCH] SUCCESS: Found matching '%s' slot! (diff: %ld)\n", targetSpiceName.c_str(), diff);
            return true;
        } else {
            Serial.printf("[SEARCH] Not matching '%s' (diff: %ld). Advancing search...\n", targetSpiceName.c_str(), diff);
        }
    }
    
    Serial.printf("[ERROR] Searched all 20 slots but could not find matching '%s' tube!\n", targetSpiceName.c_str());
    return false;
}

void executeDispenseCycle(float grams) {
    int targetCycles = (int)(grams / GRAMS_PER_CYCLE);
    if (targetCycles <= 0) targetCycles = 5; // Fallback default
    
    Serial.printf("[DISPENSER] Starting sweep dispensing for %.1fg (%d cycles)...\n", grams, targetCycles);
    startDispense(targetCycles);
    
    while (isDispensing()) {
        tickDispenser();
        yield();
    }
    Serial.println("[DISPENSER] Dispense cycle complete.");
}

void runRecipe() {
    Serial.println("\n>>> STARTING TEST RECIPE: 1g BLACK & 1g BLUE (CLOSED-LOOP DATABASE SEARCH) <<<");
    
    // Ingredient 1: BLACK
    Serial.println("\n--- Ingredient 1: BLACK ---");
    if (searchAndAlignToSpiceSlot("BLACK")) {
        executeDispenseCycle(1.0);
    } else {
        Serial.println("[RECIPE] ERROR: Could not find BLACK spice tube. Skipping BLACK.");
    }
    
    // Ingredient 2: BLUE
    Serial.println("\n--- Ingredient 2: BLUE ---");
    if (searchAndAlignToSpiceSlot("BLUE")) {
        executeDispenseCycle(1.0);
    } else {
        Serial.println("[RECIPE] ERROR: Could not find BLUE spice tube. Skipping BLUE.");
    }
    
    Serial.println("\n>>> RECIPE CLOSED-LOOP TEST COMPLETE! <<<\n");
}

void loop() {
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        
        // Consume any whitespace
        while(Serial.available() > 0 && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
            Serial.read();
        }
        
        if (cmd == 'p' || cmd == 'P') {
            Serial.println("\n--- Registered Slots Configuration ---");
            for (int i = 0; i < NUM_SPICES; i++) {
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
        else if (cmd == 'r' || cmd == 'R') {
            runRecipe();
        }
    }
}
