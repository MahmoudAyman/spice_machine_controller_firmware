#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>

void initHardware();
bool isLimitSwitchPressed();
bool isHomingSwitchPressed();
String identifySpice(); // Blocking version
void startIdentifySpice(); // Async version
bool isIdentifying();
String getIdentifiedSpice();

// --- Emergency stop (used by abort path) ---
void emergencyStopHardware();

#endif
