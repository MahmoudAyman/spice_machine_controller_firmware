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
    
    // Command the move forward immediately
    dispenserServo.write(SERVO_TARGET_DISPENSE_ANGLE);
    lastDispenserActionTime = millis();
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    
    Serial.printf("Starting non-blocking dispense: %d cycles from %d to %d\n", 
                  remainingCycles, SERVO_START_OFFSET_ANGLE, SERVO_TARGET_DISPENSE_ANGLE);
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            // Wait for servo to physically complete the travel from 180 to 20 degrees
            if (now - lastDispenserActionTime >= FORWARD_SWEEP_TIME_MS) {
                // Command return sweep to start angle
                dispenserServo.write(SERVO_START_OFFSET_ANGLE);
                
                if (VIBRATOR_PIN != -1) {
                    digitalWrite(VIBRATOR_PIN, HIGH);
                }
                
                lastDispenserActionTime = now;
                currentDispenserState = DISPENSER_SWEEP_BACKWARD;
            }
            break;

        case DISPENSER_SWEEP_BACKWARD:
            // Wait for servo to physically travel from 20 back to 180 degrees
            if (now - lastDispenserActionTime >= BACKWARD_SWEEP_TIME_MS) {
                lastDispenserActionTime = now;
                currentDispenserState = DISPENSER_VIBRATING;
            }
            break;

        case DISPENSER_VIBRATING:
            // Allow vibrator extra settle time if needed
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
                    dispenserServo.write(SERVO_TARGET_DISPENSE_ANGLE);
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