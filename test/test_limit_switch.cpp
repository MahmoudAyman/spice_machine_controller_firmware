#include <Arduino.h>
#include "Configuration.h"
#include "ColorDetector.h"
#include "TubeColors.h"
#include <AccelStepper.h>

// Instantiate stepper
AccelStepper testStepper(AccelStepper::DRIVER, STEP_PIN, STEP_DIR_PIN);

// Instantiate color detector
ColorDetector colorSensor(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);

// Alignment States for Advanced Testing
enum AlignState {
    ALIGN_IDLE,
    ALIGN_EXITING,          // Leaving the current tube protrusion (waiting for switch to go LOW)
    ALIGN_ENTERING,         // Searching for the next tube protrusion (waiting for switch to go HIGH)
    ALIGN_POST_TRIGGER,     // Micro-stepping an additional -110 steps to center/align the tube
    ALIGN_READING_COLOR     // Motor stopped, reading and classifying the tube color
};

AlignState currentAlignState = ALIGN_IDLE;
bool searchForBlack = false;       // If true, automatically advances until BLACK is detected
const float SEARCH_SPEED = -150.0; // Negative speed for normal rotation direction
const int POST_TRIGGER_OFFSET = -110; // Extra steps to align after trigger

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n==============================================");
    Serial.println("   LIMIT SWITCH & COLOR SENSOR INTEGRATION");
    Serial.println("==============================================");
    Serial.printf("Limit Switch GPIO Pin : %d (SENSOR_VP)\n", LIMIT_SWITCH_PIN);
    Serial.printf("Color Sensor Pins     : S0=%d, S1=%d, S2=%d, S3=%d, OUT=%d\n", CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);
    Serial.println("Direction Convention  : ");
    Serial.println("  - Normal Carousel Rotation: NEGATIVE (Command '2')");
    Serial.println("  - Reverse/Adjustment: POSITIVE (Command '1')");
    Serial.println("Tuning Offset         : -110 steps after switch trigger");
    Serial.println("==============================================\n");

    // Configure pins
    pinMode(LIMIT_SWITCH_PIN, INPUT);
    colorSensor.begin();

    // Initialize stepper parameters
    testStepper.setMaxSpeed(400);
    testStepper.setAcceleration(200);

    Serial.println("Serial Monitor Controls:");
    Serial.println("  - Enter '1' : Move stepper +10 steps (Positive / Reverse Nudge)");
    Serial.println("  - Enter '2' : Move stepper -10 steps (Negative / Normal Nudge)");
    Serial.println("  - Enter '3' : Advance to Next Tube, stop, apply tuning offset, read color");
    Serial.println("  - Enter '5' : Autonomous Scan: Rotate slot-by-slot until BLACK tube is found!");
    Serial.println("  - Enter '6' : Read current color values mapped to RGB (average of 10 samples)");
    Serial.println("  - Enter 's' : Emergency STOP active search immediately");
    Serial.println("----------------------------------------------\n");
}

void loop() {
    static int lastSwitchState = -1;
    int currentSwitchState = digitalRead(LIMIT_SWITCH_PIN);

    // Edge detection for limit switch
    if (currentSwitchState != lastSwitchState) {
        if (currentSwitchState == HIGH) {
            Serial.printf("[SWITCH] >>> TRIGGERED / PRESSED (HIGH) at millis: %lu <<<\n", millis());
        } else {
            Serial.printf("[SWITCH] >>> RELEASED (LOW) at millis: %lu <<<\n", millis());
        }
        lastSwitchState = currentSwitchState;
    }

    // --- State Machine for Auto-Advance / Search ---
    if (currentAlignState != ALIGN_IDLE) {
        if (currentAlignState == ALIGN_EXITING) {
            // Move motor at search speed (negative)
            testStepper.setSpeed(SEARCH_SPEED);
            testStepper.runSpeed();

            // Wait for switch to go LOW (we have left the current protrusion)
            if (currentSwitchState == LOW) {
                Serial.println("[ADVANCE] Cleared current slot. Searching for next slot...");
                currentAlignState = ALIGN_ENTERING;
            }
        }
        else if (currentAlignState == ALIGN_ENTERING) {
            // Move motor at search speed (negative)
            testStepper.setSpeed(SEARCH_SPEED);
            testStepper.runSpeed();

            // Wait for switch to go HIGH (we have hit the next protrusion)
            if (currentSwitchState == HIGH) {
                // Command relative move of POST_TRIGGER_OFFSET steps
                testStepper.move(POST_TRIGGER_OFFSET);
                Serial.printf("[ADVANCE] Limit switch triggered! Nudging %d steps for mechanical alignment...\n", POST_TRIGGER_OFFSET);
                currentAlignState = ALIGN_POST_TRIGGER;
            }
        }
        else if (currentAlignState == ALIGN_POST_TRIGGER) {
            // Run the relative adjustment move
            testStepper.run();
            if (testStepper.distanceToGo() == 0) {
                Serial.println("[ADVANCE] Alignment offset complete. Stopped for color scan...");
                currentAlignState = ALIGN_READING_COLOR;
            }
        }
        else if (currentAlignState == ALIGN_READING_COLOR) {
            // Take 3 measurements to average out any sensor noise
            long sumR = 0, sumG = 0, sumB = 0;
            for (int i = 0; i < 3; i++) {
                sumR += colorSensor.readRawColor('r'); delay(15);
                sumG += colorSensor.readRawColor('g'); delay(15);
                sumB += colorSensor.readRawColor('b'); delay(15);
            }
            int avgR = sumR / 3;
            int avgG = sumG / 3;
            int avgB = sumB / 3;

            // Match against registered colors with a distance threshold of 80
            String matchedLabel = getMatchingTubeColor(avgR, avgG, avgB, 80);

            Serial.println("----------------------------------------------------------------");
            Serial.printf("[SENSOR] Captured Raw values -> R: %d | G: %d | B: %d\n", avgR, avgG, avgB);
            Serial.printf("[SENSOR] Identified Color   -> >>> %s <<<\n", matchedLabel.c_str());
            Serial.println("----------------------------------------------------------------");

            if (searchForBlack) {
                if (matchedLabel == "BLACK") {
                    Serial.println("[AUTONOMOUS] >>> SUCCESS: BLACK tube identified! Scanning halted. <<<");
                    searchForBlack = false;
                    currentAlignState = ALIGN_IDLE;
                } else {
                    Serial.printf("[AUTONOMOUS] Detected %s. Moving to next slot...\n", matchedLabel.c_str());
                    currentAlignState = ALIGN_EXITING;
                }
            } else {
                // Done with single slot advance
                currentAlignState = ALIGN_IDLE;
            }
        }
    } else {
        // Run standard AccelStepper movement profile (nudges)
        testStepper.run();
    }

    // --- Serial Command Parser ---
    if (Serial.available() > 0) {
        char cmd = Serial.read();

        // Consume any remaining carriage return or line feed characters
        while (Serial.available() > 0 && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
            Serial.read();
        }

        switch (cmd) {
            case '1':
                if (currentAlignState != ALIGN_IDLE) {
                    Serial.println("[WARN] Carousel moving! Press 's' to stop first.");
                } else {
                    Serial.println("[SERIAL] Received '1': Moving +10 steps (Positive/Reverse)...");
                    testStepper.move(10);
                }
                break;
            case '2':
                if (currentAlignState != ALIGN_IDLE) {
                    Serial.println("[WARN] Carousel moving! Press 's' to stop first.");
                } else {
                    Serial.println("[SERIAL] Received '2': Moving -10 steps (Negative/Normal)...");
                    testStepper.move(-10);
                }
                break;
            case '3':
                Serial.println("[SERIAL] Received '3': Advancing to next tube and reading color...");
                searchForBlack = false;
                if (currentSwitchState == HIGH) {
                    currentAlignState = ALIGN_EXITING;
                } else {
                    currentAlignState = ALIGN_ENTERING;
                }
                break;
            case '5':
                Serial.println("[SERIAL] Received '5': Starting Autonomous Search for BLACK tube...");
                searchForBlack = true;
                if (currentSwitchState == HIGH) {
                    currentAlignState = ALIGN_EXITING;
                } else {
                    currentAlignState = ALIGN_ENTERING;
                }
                break;
            case '6': {
                Serial.println("[SERIAL] Received '6': Measuring mapped RGB values (average of 10 samples)...");
                long rawR = 0, rawG = 0, rawB = 0;
                long mapR = 0, mapG = 0, mapB = 0;
                for (int i = 0; i < 10; i++) {
                    rawR += colorSensor.readRawColor('r');
                    mapR += colorSensor.readMappedColor('r'); delay(15);
                    rawG += colorSensor.readRawColor('g');
                    mapG += colorSensor.readMappedColor('g'); delay(15);
                    rawB += colorSensor.readRawColor('b');
                    mapB += colorSensor.readMappedColor('b'); delay(15);
                }

                Serial.println("----------------------------------------------------------------");
                Serial.printf("[SENSOR] Avg RAW    -> R: %ld | G: %ld | B: %ld\n", rawR / 10, rawG / 10, rawB / 10);
                Serial.printf("[SENSOR] Avg MAPPED -> R: %ld | G: %ld | B: %ld\n", mapR / 10, mapG / 10, mapB / 10);

                Serial.println("----------------------------------------------------------------");
                break;
            }
            case 's':
                Serial.println("[SERIAL] Received 's': Emergency STOP triggered!");
                testStepper.moveTo(testStepper.currentPosition());
                currentAlignState = ALIGN_IDLE;
                searchForBlack = false;
                break;
            default:
                break;
        }
    }
}