#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <Keypad.h>

void initHardware();
void updateLcd(String line1, String line2);
String identifySpice();
void dispenseSpice(int totalCycles);
void verifySystemIntegrity();

#endif