#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

void initHardware();
void updateLcd(String line1, String line2);
int readColor(char color);
String identifySpice();
void dispenseSpice(int totalCycles);
void verifySystemIntegrity();

#endif