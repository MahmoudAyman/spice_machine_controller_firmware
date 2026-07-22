#include "Hardware.h"
#include "Globals.h"
#include "Configuration.h"
#include "Database.h"
#include "../ServoController/ServoController.h"


// --- Hardware Initialization ---
void initHardware() {
  // Stepper
  Serial.println("Initializing Stepper...");
  if (STEP_ENABLE_PIN != -1) {
      pinMode(STEP_ENABLE_PIN, OUTPUT); 
      digitalWrite(STEP_ENABLE_PIN, HIGH); 
  }
  
  stepper.setMaxSpeed(400); 
  stepper.setAcceleration(200);   
  // Note: STEP_ENABLE_PIN is hardwired to GND and set to -1
  stepper.setPinsInverted(true, false, false); 

  // Sensors
  Serial.println("Initializing Sensors...");
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(HOMING_SWITCH_PIN, INPUT_PULLUP);
  laserSensor.begin();

  // Buttons (Discrete Pins)
  Serial.println("Initializing Buttons...");
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);



  // Vibrator
  if (!simulationEnabled && VIBRATOR_PIN != -1) {
      pinMode(VIBRATOR_PIN, OUTPUT);
      digitalWrite(VIBRATOR_PIN, LOW); 
  }
}

// Button Mapping
char getButtonKey() {
    if (digitalRead(BTN_UP) == LOW)    return 'U'; // GPIO 26
    if (digitalRead(BTN_DOWN) == LOW)  return 'D'; // GPIO 14
    if (digitalRead(BTN_LEFT) == LOW)  return 'L'; // GPIO 13
    if (digitalRead(BTN_RIGHT) == LOW) return 'R'; // GPIO 12
    if (digitalRead(BTN_OK) == LOW)    return 'N'; // GPIO 32
    
    return 0;
}

bool isLimitSwitchPressed() {
    return digitalRead(LIMIT_SWITCH_PIN) == HIGH;
}

bool isHomingSwitchPressed() {
    return digitalRead(HOMING_SWITCH_PIN) == HIGH;
}

String identifySpice() {
  // Stubbed out: return the expected slot name directly
  return spices[pendingTargetTubeIndex].name;
}

void startIdentifySpice() {
  // Stubbed out
}

bool isIdentifying() {
  // Stubbed out: complete instantly
  return false;
}

String getIdentifiedSpice() {
  // Stubbed out
  return spices[pendingTargetTubeIndex].name;
}

void emergencyStopHardware() {
    emergencyStopServo();
    stepper.stop();
    if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
}
