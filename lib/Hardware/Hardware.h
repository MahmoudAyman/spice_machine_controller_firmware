#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>

void initHardware();
String identifySpice(); // Blocking version
void startIdentifySpice(); // Async version
bool isIdentifying();
String getIdentifiedSpice();

// --- Non-blocking Dispenser ---
enum DispenserState {
    DISPENSER_IDLE,
    DISPENSER_SWEEP_FORWARD,
    DISPENSER_SWEEP_BACKWARD,
    DISPENSER_VIBRATING,
    DISPENSER_COOLDOWN
};

void startDispense(int totalCycles);
void tickDispenser();
bool isDispensing();

// --- Emergency stop (used by abort path) ---
void emergencyStopHardware();
int getRemainingDispenseCycles(); // Returns cycles left in current dispense

#endif
