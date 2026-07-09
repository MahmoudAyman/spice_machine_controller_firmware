#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include "Configuration.h"
#include "Database.h"
#include "../lib/ColorDetector/ColorDetector.h"
#include "../lib/BLEManager/BLEManager.h"
#include "../lib/LCDManager/LCDManager.h"
#include "../lib/LaserLevelSensor/LaserLevelSensor.h"

// --- State Machine Enum ---
enum SystemState {
  STATE_BOOT,
  STATE_INITIAL_SETUP,
  STATE_SYSTEM_CHECK,
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

// Expose objects to all files (Back to Instances)
extern LCDManager lcd;
extern AccelStepper stepper;
extern Servo dispenserServo;
extern ColorDetector colorDetector;
extern BLEManager bleManager;
extern LaserLevelSensor laserSensor;

// Expose Logic Variables
extern bool simulationEnabled;
extern int currentTubeIndex;
extern SystemState currentState; 
extern bool abortTriggered;
extern bool remoteRequestTriggered;
extern Recipe remoteRecipe;
extern int pendingTargetTubeIndex;
extern int currentSelection; 

// --- App-Driven BLE Setup Globals ---
extern String bleSetupNameReceived;
extern bool bleSetupNamePending;

// Function Prototypes
void sendBleStatus();
char getButtonKey();
bool isLimitSwitchPressed();

#endif
