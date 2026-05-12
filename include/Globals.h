#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <Keypad.h>
#include "Configuration.h"
#include "Database.h"
#include "../lib/ColorDetector/ColorDetector.h"

// --- State Machine Enum ---
enum SystemState {
  STATE_MAIN_MENU,
  STATE_RECIPE_SELECT,
  STATE_RECIPE_SERVINGS_INPUT,
  STATE_AWAITING_INPUT,
  STATE_AWAITING_QUANTITY_INPUT,
  STATE_ROTATING_TO_TARGET,
  STATE_IDENTIFYING,
  STATE_CHECKING_FILL,
  STATE_EMPTY_RETRY,
  STATE_DISPENSING,
  STATE_SHOWING_FILL_STATUS,
  STATE_RETURNING_HOME,
  STATE_ERROR_RETURN
};

// Expose objects to all files
extern Adafruit_SSD1306 display;
extern AccelStepper stepper;
extern Servo dispenserServo;
extern Keypad customKeypad;
extern ColorDetector colorDetector;

// Expose Logic Variables
extern int currentTubeIndex;
extern SystemState currentState; // Requires enum definition in .ino or main header, 
                                // but for simple Arduino structure, we keep State Enum in main.

#endif