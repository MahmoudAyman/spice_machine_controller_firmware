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
  Wire.begin();
  
  // LCD
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing LCD...");
  } else {
      if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("LCD Failed")); while(1);
      }
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(2);
  }

  // Stepper
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Stepper...");
  } else {
      pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, HIGH);
      stepper.setMaxSpeed(200); stepper.setAcceleration(50);   
      stepper.setEnablePin(ENABLE_PIN); stepper.setPinsInverted(true, false, true);
  }

  // Sensors
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Sensors...");
  } else {
      colorDetector.begin();
      pinMode(LASER_RX_PIN, INPUT);
  }

  // Servo
  if (simulationEnabled) {
      Serial.println("[SIM] Initializing Servo...");
  } else {
      dispenserServo.attach(SERVO_PIN, 500, 2500); 
      dispenserServo.write(0);
  }

  // Vibrator
  if (!simulationEnabled) {
      pinMode(VIBRATOR_PIN, OUTPUT);
      digitalWrite(VIBRATOR_PIN, LOW); // Ensure off at startup
  } else {
      Serial.println("[SIM] Skipping Vibrator Init (GPIO 1 conflict)");
  }
}

// --- Display Helper ---
void updateLcd(String line1, String line2) {
  if (simulationEnabled) {
      Serial.printf("[LCD] %s | %s\n", line1.c_str(), line2.c_str());
  } else {
      display.clearDisplay();
      display.setCursor(0, 10);
      display.print(line1);
      display.setCursor(0, 40);
      display.print(line2);
      display.display();
  }
}

String identifySpice() {
  if (simulationEnabled) {
      return spices[pendingTargetTubeIndex].name; // Always correct in simulation
  }
  return colorDetector.identify(spices, NUM_SPICES, MATCH_THRESHOLD);
}

void startIdentifySpice() {
    if (simulationEnabled) {
        Serial.printf("[SIM] Identifying tube %d...\n", pendingTargetTubeIndex + 1);
    } else {
        colorDetector.startIdentification(spices, NUM_SPICES, MATCH_THRESHOLD);
    }
}

bool isIdentifying() {
    if (simulationEnabled) {
        static unsigned long simStart = 0;
        if (simStart == 0) simStart = millis();
        if (millis() - simStart >= 1000) { // 1s simulation
            simStart = 0;
            return false;
        }
        return true;
    }
    return colorDetector.isBusy();
}

String getIdentifiedSpice() {
    if (simulationEnabled) {
        return spices[pendingTargetTubeIndex].name;
    }
    return colorDetector.getResult();
}

// --- Non-blocking Dispensing Logic ---

void startDispense(int totalCycles) {
    if (totalCycles <= 0) return;
    remainingCycles = totalCycles;
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    servoPos = 0;
    lastDispenserActionTime = millis();
    Serial.printf("%s: %d cycles\n", simulationEnabled ? "[SIM] Starting dispense" : "Starting non-blocking dispense", remainingCycles);
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            if (now - lastDispenserActionTime >= (simulationEnabled ? 2 : 10)) { // Faster in simulation
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
                    if (!simulationEnabled) digitalWrite(VIBRATOR_PIN, HIGH);
                }
            }
            break;

        case DISPENSER_VIBRATING:
            if (now - lastDispenserActionTime >= (simulationEnabled ? 10 : 150)) { 
                if (!simulationEnabled) digitalWrite(VIBRATOR_PIN, LOW);
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
    // Stop dispensing immediately
    remainingCycles = 0;
    currentDispenserState = DISPENSER_IDLE;
    digitalWrite(VIBRATOR_PIN, LOW);
    dispenserServo.write(0);

    // Stop stepper output immediately; motion will be re-initiated by the main state machine if needed
    stepper.stop();
    stepper.disableOutputs();
}
