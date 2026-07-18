#include "Hardware.h"
#include "Globals.h"
#include "Configuration.h"
#include "Database.h"

// --- Private Dispenser State ---
static DispenserState currentDispenserState = DISPENSER_IDLE;
static int remainingCycles = 0;
static unsigned long lastDispenserActionTime = 0;
static int servoPos = 0;

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
  // colorDetector.begin(); // Commented out
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

  // Servo
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Servo...");
  } else {
      Serial.println("Initializing Servo...");
      dispenserServo.attach(SERVO_PIN, 500, 2500); 
      dispenserServo.write(0);
  }

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

void startDispense(int totalCycles) {
    if (totalCycles <= 0) return;
    remainingCycles = totalCycles;
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    servoPos = 0;
    lastDispenserActionTime = millis();
    Serial.printf("%s: %d cycles\n", simulationEnabled ? "[SIM] Starting dispense" : "Starting non-blocking dispense", remainingCycles);
}

int getRemainingDispenseCycles() {
    return remainingCycles;
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            if (now - lastDispenserActionTime >= (simulationEnabled ? 2 : 10)) { 
                servoPos += 10;
                if (!simulationEnabled) dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos >= 360) {
                    currentDispenserState = DISPENSER_SWEEP_BACKWARD;
                }
            }
            break;

        case DISPENSER_SWEEP_BACKWARD:
            if (now - lastDispenserActionTime >= (simulationEnabled ? 2 : 10)) {
                servoPos -= 10;
                if (!simulationEnabled) dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos <= 0) {
                    currentDispenserState = DISPENSER_VIBRATING;
                    if (!simulationEnabled && VIBRATOR_PIN != -1) digitalWrite(VIBRATOR_PIN, HIGH);
                }
            }
            break;

        case DISPENSER_VIBRATING:
            if (now - lastDispenserActionTime >= (simulationEnabled ? 10 : 150)) { 
                if (!simulationEnabled && VIBRATOR_PIN != -1) digitalWrite(VIBRATOR_PIN, LOW);
                remainingCycles--;
                if (remainingCycles % 5 == 0 || remainingCycles < 5) {
                    Serial.printf("[DEBUG] Cycle complete. Remaining: %d\n", remainingCycles);
                }
                if (remainingCycles > 0) {
                    currentDispenserState = DISPENSER_SWEEP_FORWARD;
                    servoPos = 0;
                } else {
                    currentDispenserState = DISPENSER_IDLE;
                    Serial.println("Dispensing complete.");
                }
                lastDispenserActionTime = now;
            }
            break;
            
        case DISPENSER_COOLDOWN:
            break;
    }
}

bool isDispensing() {
    return currentDispenserState != DISPENSER_IDLE;
}

void emergencyStopHardware() {
    remainingCycles = 0;
    currentDispenserState = DISPENSER_IDLE;
    if (!simulationEnabled && VIBRATOR_PIN != -1) digitalWrite(VIBRATOR_PIN, LOW);
    dispenserServo.write(0);
    stepper.stop();
    if (STEP_ENABLE_PIN != -1) stepper.disableOutputs();
}
