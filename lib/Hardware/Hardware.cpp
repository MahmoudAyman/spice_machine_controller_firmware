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
  // LCD (SPI TFT ST7735)
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing LCD...");
  } else {
      Serial.println("Initializing TFT LCD...");
      pinMode(TFT_LED, OUTPUT);
      digitalWrite(TFT_LED, HIGH); 
      
      display.initR(INITR_BLACKTAB); 
      display.setRotation(1);        
      display.fillScreen(ST77XX_BLACK);
      display.setTextColor(ST77XX_WHITE);
      display.setTextSize(2);
  }

  // Stepper
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Stepper...");
  } else {
      Serial.println("Initializing Stepper...");
      if (STEP_ENABLE_PIN != -1) {
          pinMode(STEP_ENABLE_PIN, OUTPUT); 
          digitalWrite(STEP_ENABLE_PIN, HIGH); 
      }
      
      stepper.setMaxSpeed(200); 
      stepper.setAcceleration(50);   
      if (STEP_ENABLE_PIN != -1) stepper.setEnablePin(STEP_ENABLE_PIN); 
      stepper.setPinsInverted(true, false, true);
  }

  // Sensors
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Sensors...");
  } else {
      Serial.println("Initializing Sensors...");
      colorDetector.begin();
      pinMode(LASER_RX_PIN, INPUT);
  }

  // Buttons (Discrete Pins)
  if (!simulationEnabled) {
      Serial.println("Initializing Buttons...");
      pinMode(BTN_1, INPUT_PULLUP);
      pinMode(BTN_2, INPUT_PULLUP);
      pinMode(BTN_3, INPUT_PULLUP);
      pinMode(BTN_4, INPUT_PULLUP);
      pinMode(BTN_5, INPUT_PULLUP);
  }

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

// --- Display Helper ---
void updateLcd(String line1, String line2) {
  if (simulationEnabled) {
      Serial.printf("[LCD] %s | %s\n", line1.c_str(), line2.c_str());
  } else {
      display.fillScreen(ST77XX_BLACK);
      display.setCursor(0, 20);
      display.println(line1);
      display.setCursor(0, 60);
      display.println(line2);
  }
}

// Button Mapping
char getButtonKey() {
    if (simulationEnabled) return 0;
    
    if (digitalRead(BTN_1) == LOW) return 'U'; 
    if (digitalRead(BTN_2) == LOW) return 'D'; 
    if (digitalRead(BTN_3) == LOW) return 'N'; 
    if (digitalRead(BTN_4) == LOW) return 'E'; 
    if (digitalRead(BTN_5) == LOW) return 'A'; 
    
    return 0;
}

String identifySpice() {
  if (simulationEnabled) {
      return spices[pendingTargetTubeIndex].name;
  }
  return colorDetector.identify(spices, NUM_SPICES, MATCH_THRESHOLD);
}

void startIdentifySpice() {
    if (simulationEnabled) {
        // No action needed for simulation
    } else {
        colorDetector.startIdentification(spices, NUM_SPICES, MATCH_THRESHOLD);
    }
}

bool isIdentifying() {
    if (simulationEnabled) return false; 
    return colorDetector.isBusy();
}

String getIdentifiedSpice() {
    if (simulationEnabled) {
        return spices[pendingTargetTubeIndex].name;
    }
    return colorDetector.getResult();
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
