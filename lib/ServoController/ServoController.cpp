#include "ServoController.h"
#include "Globals.h"
#include "Configuration.h"

// Define proper travel durations (160 deg travel needs ~350-400ms under load)
const unsigned long FORWARD_SWEEP_TIME_MS  = 350; 
const unsigned long BACKWARD_SWEEP_TIME_MS = 350; 
const unsigned long VIBRATION_TIME_MS      = 150; 

static DispenserState currentDispenserState = DISPENSER_IDLE;
static int remainingCycles = 0;
static unsigned long lastDispenserActionTime = 0;
static int servoPos = SERVO_START_OFFSET_ANGLE;

void initServo() {
    Serial.printf("Initializing Servo on Pin %d to start offset angle (%d)...\n", SERVO_PIN, SERVO_START_OFFSET_ANGLE);
    dispenserServo.attach(SERVO_PIN, 500, 2500); 
    dispenserServo.write(SERVO_START_OFFSET_ANGLE);
    servoPos = SERVO_START_OFFSET_ANGLE;
}

void startDispense(int totalCycles) {
    if (totalCycles <= 0) return;
    remainingCycles = totalCycles;
    
    // Set initial position and state for incremental sweep
    servoPos = SERVO_START_OFFSET_ANGLE;
    dispenserServo.write(servoPos);
    
    lastDispenserActionTime = millis();
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    
    Serial.printf("Starting non-blocking step dispense: %d cycles from %d to %d\n", 
                  remainingCycles, SERVO_START_OFFSET_ANGLE, SERVO_TARGET_DISPENSE_ANGLE);
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            // Move in steps of 5 degrees every 15 ms from 180 down to 20
            if (now - lastDispenserActionTime >= 15) {
                lastDispenserActionTime = now;
                if (servoPos > SERVO_TARGET_DISPENSE_ANGLE) {
                    servoPos -= 5;
                    if (servoPos < SERVO_TARGET_DISPENSE_ANGLE) {
                        servoPos = SERVO_TARGET_DISPENSE_ANGLE;
                    }
                    dispenserServo.write(servoPos);
                }
                
                if (servoPos == SERVO_TARGET_DISPENSE_ANGLE) {
                    currentDispenserState = DISPENSER_PAUSE_PEAK;
                }
            }
            break;

        case DISPENSER_PAUSE_PEAK:
            // Pause at the peak target angle for 500 ms (matching test code)
            if (now - lastDispenserActionTime >= 500) {
                lastDispenserActionTime = now;
                currentDispenserState = DISPENSER_SWEEP_BACKWARD;
            }
            break;

        case DISPENSER_SWEEP_BACKWARD:
            // Move in steps of 5 degrees every 15 ms from 20 back to 180
            if (now - lastDispenserActionTime >= 15) {
                lastDispenserActionTime = now;
                if (servoPos < SERVO_START_OFFSET_ANGLE) {
                    servoPos += 5;
                    if (servoPos > SERVO_START_OFFSET_ANGLE) {
                        servoPos = SERVO_START_OFFSET_ANGLE;
                    }
                    dispenserServo.write(servoPos);
                }
                
                if (servoPos == SERVO_START_OFFSET_ANGLE) {
                    currentDispenserState = DISPENSER_VIBRATING;
                    if (VIBRATOR_PIN != -1) {
                        digitalWrite(VIBRATOR_PIN, HIGH);
                    }
                }
            }
            break;

        case DISPENSER_VIBRATING:
            // Settle vibration for 150 ms
            if (now - lastDispenserActionTime >= VIBRATION_TIME_MS) { 
                if (VIBRATOR_PIN != -1) {
                    digitalWrite(VIBRATOR_PIN, LOW);
                }
                
                remainingCycles--;
                if (remainingCycles % 5 == 0 || remainingCycles < 5) {
                    Serial.printf("[DEBUG] Cycle complete. Remaining: %d\n", remainingCycles);
                }
                
                if (remainingCycles > 0) {
                    // Start next cycle
                    servoPos = SERVO_START_OFFSET_ANGLE;
                    lastDispenserActionTime = now;
                    currentDispenserState = DISPENSER_SWEEP_FORWARD;
                } else {
                    currentDispenserState = DISPENSER_IDLE;
                    Serial.println("Dispensing complete.");
                }
            }
            break;
    }
}

bool isDispensing() {
    return currentDispenserState != DISPENSER_IDLE;
}

int getRemainingDispenseCycles() {
    return remainingCycles;
}

void emergencyStopServo() {
    remainingCycles = 0;
    currentDispenserState = DISPENSER_IDLE;
    if (VIBRATOR_PIN != -1) {
        digitalWrite(VIBRATOR_PIN, LOW);
    }
    dispenserServo.write(SERVO_START_OFFSET_ANGLE);
    servoPos = SERVO_START_OFFSET_ANGLE;
}