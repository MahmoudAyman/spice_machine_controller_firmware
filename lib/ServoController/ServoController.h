#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Arduino.h>

// Servo constants
const int SERVO_START_OFFSET_ANGLE = 180;
const int SERVO_TARGET_DISPENSE_ANGLE = 20;
const float SERVO_GRAMS_PER_CYCLE = 0.2;

enum DispenserState {
    DISPENSER_IDLE,
    DISPENSER_SWEEP_FORWARD,
    DISPENSER_SWEEP_BACKWARD,
    DISPENSER_VIBRATING
};

void initServo();
void startDispense(int totalCycles);
void tickDispenser();
bool isDispensing();
int getRemainingDispenseCycles();
void emergencyStopServo();

#endif // SERVO_CONTROLLER_H
