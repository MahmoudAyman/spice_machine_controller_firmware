#include <Arduino.h>
#include <AccelStepper.h>
#include "Configuration.h"

// Initialize stepper
AccelStepper testStepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Stepper Motor Hardware Test (Diagnostic Mode) ---");
    Serial.println("STEP Pin: " + String(STEP_PIN));
    Serial.println("DIR Pin:  " + String(STEP_DIR_PIN));

    // Explicitly set pin modes
    pinMode(STEP_PIN, OUTPUT);
    pinMode(STEP_DIR_PIN, OUTPUT);
    
    // Note: STEP_ENABLE_PIN is hardwired to GND. 
    // CHECK: Are SLEEP and RESET pins on the driver tied to 3.3V/5V? 
    // If they are floating, the driver will not turn on.

    Serial.println("[DIAGNOSTIC] Pulsing STEP pin 5 times (Slow)...");
    for(int i=0; i<5; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delay(100);
        digitalWrite(STEP_PIN, LOW);
        delay(100);
        Serial.print(".");
    }
    Serial.println("\n[DIAGNOSTIC] Did the motor move or twitch?");

    // Configure stepper
    testStepper.setMaxSpeed(400);      
    testStepper.setAcceleration(200);
    
    Serial.println("\n[TEST 1] Moving 1 Full Revolution Clockwise...");
    testStepper.move(STEPS_PER_REVOLUTION);
}

void loop() {
    static int testStep = 1;
    
    testStepper.run();

    if (testStepper.distanceToGo() == 0) {
        delay(2000); // Pause between tests
        
        testStep++;
        if (testStep == 2) {
            Serial.println("[TEST 2] Moving 1 Full Revolution Counter-Clockwise...");
            testStepper.move(-STEPS_PER_REVOLUTION);
        } else if (testStep == 3) {
            Serial.println("[TEST 3] Moving Tube by Tube (5 tubes)...");
            testStepper.move(STEPS_PER_TUBE);
        } else if (testStep > 3 && testStep <= 7) {
            Serial.printf("  - Moved to Tube %d\n", testStep - 2);
            testStepper.move(STEPS_PER_TUBE);
        } else if (testStep == 8) {
            Serial.println("[TEST 4] Returning to Start Position...");
            testStepper.moveTo(0);
        } else if (testStep > 8) {
            Serial.println("--- All Tests Completed ---");
            while(true) {
                delay(1000);
            }
        }
    }
}
