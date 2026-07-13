#include <Arduino.h>
#include <AccelStepper.h>
#include "Configuration.h"

// Initialize AccelStepper on the same pins as production
AccelStepper testStepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);

// Homing states
enum HomingState {
    HOME_IDLE,
    HOME_INIT,
    HOME_EXITING,   // Move off current position (must ONLY move in the negative direction)
    HOME_ENTERING,  // Move fast (negative direction) until homing switch triggers
    HOME_ALIGNING   // Move slow (negative direction) until homing switch releases
};

HomingState currentHomeState = HOME_IDLE;
unsigned long mcDebounceTimer = 0;

// All speeds are strictly negative to ensure unidirectional negative rotation (same as main.cpp)
const float SEARCH_SPEED = -250.0;
const float SLOW_ALIGN_SPEED = -50.0;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n==================================================");
    Serial.println("   HOMING & LIMIT SWITCH DIAGNOSTIC TEST (UNIDIRECTIONAL)");
    Serial.println("==================================================");
    Serial.printf("Alignment Switch Pin (GPIO 36): %d\n", LIMIT_SWITCH_PIN);
    Serial.printf("Homing Switch Pin (GPIO 39)   : %d\n", HOMING_SWITCH_PIN);
    Serial.println("==================================================\n");

    // Configure pins
    pinMode(LIMIT_SWITCH_PIN, INPUT);
    pinMode(HOMING_SWITCH_PIN, INPUT);

    // Initialize stepper parameters with the exact pin inversion as main.cpp
    testStepper.setMaxSpeed(400);
    testStepper.setAcceleration(200);
    testStepper.setPinsInverted(true, false, false); // CRITICAL: Matches main.cpp inversion!

    Serial.println("Serial Monitor Controls:");
    Serial.println("  - Enter 'h' or 'H' : Run the Absolute Homing Sequence!");
    Serial.println("  - Enter 's'        : Emergency Stop Stepper Motor!");
    Serial.println("  - Current switch states are printed below every 1 second.");
    Serial.println("  * Note: The motor will ONLY rotate in the negative direction.");
    Serial.println("--------------------------------------------------\n");
}

void loop() {
    // 1. Periodically print switch states (non-blocking 1-second interval)
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint >= 1000) {
        lastPrint = millis();
        bool alignPressed = (digitalRead(LIMIT_SWITCH_PIN) == HIGH);
        bool homePressed = (digitalRead(HOMING_SWITCH_PIN) == HIGH);
        
        Serial.printf("[DIAGNOSTIC] Time: %7lu ms | Alignment SW (GPIO 36): %s | Homing SW (GPIO 39): %s | Homing State: %d\n",
                      millis(),
                      alignPressed ? "PRESSED (HIGH)" : "RELEASED (LOW)",
                      homePressed ? "PRESSED (HIGH)" : "RELEASED (LOW)",
                      currentHomeState);
    }

    // 2. Process Unidirectional Homing State Machine (Strict Negative Direction)
    switch (currentHomeState) {
        case HOME_IDLE:
            break;
            
        case HOME_INIT: {
            if (digitalRead(HOMING_SWITCH_PIN) == HIGH) {
                // To exit, we continue moving in the negative direction, similar to main.cpp's exit logic!
                Serial.println("[HOMING] Switch already pressed. Exiting by continuing negative rotation...");
                currentHomeState = HOME_EXITING;
            } else {
                Serial.println("[HOMING] Homing switch released. Searching fast (negative direction)...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ENTERING;
            }
            break;
        }
        
        case HOME_EXITING: {
            testStepper.setSpeed(SEARCH_SPEED); // Negative speed
            testStepper.runSpeed();
            if (digitalRead(HOMING_SWITCH_PIN) == LOW) {
                Serial.println("[HOMING] Switch cleared. Settle and search fast (negative direction)...");
                delay(100); // physical settling
                mcDebounceTimer = millis();
                currentHomeState = HOME_ENTERING;
            }
            break;
        }
        
        case HOME_ENTERING: {
            testStepper.setSpeed(SEARCH_SPEED); // Negative speed
            testStepper.runSpeed();
            
            if (digitalRead(HOMING_SWITCH_PIN) == LOW) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 10) { // Pressed for 10ms
                Serial.println("[HOMING] Homing switch triggered! Slowing down for precision alignment (negative direction)...");
                mcDebounceTimer = millis();
                currentHomeState = HOME_ALIGNING;
            }
            break;
        }
        
        case HOME_ALIGNING: {
            testStepper.setSpeed(SLOW_ALIGN_SPEED); // Negative speed
            testStepper.runSpeed();
            
            if (digitalRead(HOMING_SWITCH_PIN) == HIGH) {
                mcDebounceTimer = millis();
            }
            
            if (millis() - mcDebounceTimer >= 20) { // Released for 20ms
                testStepper.stop();
                Serial.println("[HOMING] SUCCESS! Homing switch aligned & released. Resetting home index.");
                currentHomeState = HOME_IDLE;
            }
            break;
        }
    }

    // 3. Serial Command Parser
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        
        // Clear CR/LF
        while (Serial.available() > 0 && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
            Serial.read();
        }

        if (cmd == 'h' || cmd == 'H') {
            if (currentHomeState != HOME_IDLE) {
                Serial.println("[WARN] Homing already in progress!");
            } else {
                Serial.println("[SERIAL] Starting Absolute Homing Sequence...");
                currentHomeState = HOME_INIT;
            }
        }
        else if (cmd == 's') {
            Serial.println("[SERIAL] Emergency Stop!");
            testStepper.stop();
            currentHomeState = HOME_IDLE;
        }
    }
}
