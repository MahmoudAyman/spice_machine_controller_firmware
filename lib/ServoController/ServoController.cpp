#include "ServoController.h"
#include "Globals.h"
#include "Configuration.h"

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
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    servoPos = SERVO_START_OFFSET_ANGLE;
    lastDispenserActionTime = millis();
    Serial.printf("Starting non-blocking dispense: %d cycles from %d to %d\n", 
                  remainingCycles, SERVO_START_OFFSET_ANGLE, SERVO_TARGET_DISPENSE_ANGLE);
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            // Sweep from 180 down to 20
            if (now - lastDispenserActionTime >= 10) { 
                servoPos -= 10;
                if (servoPos < SERVO_TARGET_DISPENSE_ANGLE) {
                    servoPos = SERVO_TARGET_DISPENSE_ANGLE;
                }
                dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos <= SERVO_TARGET_DISPENSE_ANGLE) {
                    currentDispenserState = DISPENSER_SWEEP_BACKWARD;
                }
            }
            break;

        case DISPENSER_SWEEP_BACKWARD:
            // Sweep from 20 back to 180
            if (now - lastDispenserActionTime >= 10) {
                servoPos += 10;
                if (servoPos > SERVO_START_OFFSET_ANGLE) {
                    servoPos = SERVO_START_OFFSET_ANGLE;
                }
                dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos >= SERVO_START_OFFSET_ANGLE) {
                    currentDispenserState = DISPENSER_VIBRATING;
                    if (VIBRATOR_PIN != -1) {
                        digitalWrite(VIBRATOR_PIN, HIGH);
                    }
                }
            }
            break;

        case DISPENSER_VIBRATING:
            if (now - lastDispenserActionTime >= 150) { 
                if (VIBRATOR_PIN != -1) {
                    digitalWrite(VIBRATOR_PIN, LOW);
                }
                remainingCycles--;
                if (remainingCycles % 5 == 0 || remainingCycles < 5) {
                    Serial.printf("[DEBUG] Cycle complete. Remaining: %d\n", remainingCycles);
                }
                if (remainingCycles > 0) {
                    currentDispenserState = DISPENSER_SWEEP_FORWARD;
                    servoPos = SERVO_START_OFFSET_ANGLE;
                } else {
                    currentDispenserState = DISPENSER_IDLE;
                    Serial.println("Dispensing complete.");
                }
                lastDispenserActionTime = now;
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
