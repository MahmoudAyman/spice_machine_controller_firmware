#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <Keypad.h>

void initHardware();
void updateLcd(String line1, String line2);
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

void verifySystemIntegrity();

#endif