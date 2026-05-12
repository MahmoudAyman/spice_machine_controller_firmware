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
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("LCD Failed")); while(1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);

  // Stepper
  pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, HIGH);
  stepper.setMaxSpeed(200); stepper.setAcceleration(50);   
  stepper.setEnablePin(ENABLE_PIN); stepper.setPinsInverted(true, false, true);

  // Sensors
  colorDetector.begin();
  pinMode(LASER_RX_PIN, INPUT);

  // Servo
  dispenserServo.attach(SERVO_PIN, 500, 2500); 
  dispenserServo.write(0);

  // Vibrator
  pinMode(VIBRATOR_PIN, OUTPUT);
  digitalWrite(VIBRATOR_PIN, LOW); // Ensure off at startup
}

// --- Display Helper ---
void updateLcd(String line1, String line2) {
  display.clearDisplay();
  display.setCursor(0, 10);
  display.print(line1);
  display.setCursor(0, 40);
  display.print(line2);
  display.display();
}

String identifySpice() {
  return colorDetector.identify(spices, NUM_SPICES, MATCH_THRESHOLD);
}

// --- Non-blocking Dispensing Logic ---

void startDispense(int totalCycles) {
    if (totalCycles <= 0) return;
    remainingCycles = totalCycles;
    currentDispenserState = DISPENSER_SWEEP_FORWARD;
    servoPos = 0;
    lastDispenserActionTime = millis();
    Serial.printf("Starting non-blocking dispense: %d cycles\n", remainingCycles);
}

void tickDispenser() {
    unsigned long now = millis();

    switch (currentDispenserState) {
        case DISPENSER_IDLE:
            break;

        case DISPENSER_SWEEP_FORWARD:
            if (now - lastDispenserActionTime >= 10) { // Increment every 10ms
                servoPos += 10;
                dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos >= 360) {
                    currentDispenserState = DISPENSER_SWEEP_BACKWARD;
                }
            }
            break;

        case DISPENSER_SWEEP_BACKWARD:
            if (now - lastDispenserActionTime >= 10) {
                servoPos -= 10;
                dispenserServo.write(servoPos);
                lastDispenserActionTime = now;
                if (servoPos <= 0) {
                    currentDispenserState = DISPENSER_VIBRATING;
                    digitalWrite(VIBRATOR_PIN, HIGH);
                }
            }
            break;

        case DISPENSER_VIBRATING:
            if (now - lastDispenserActionTime >= 150) { // Vibrate for 150ms
                digitalWrite(VIBRATOR_PIN, LOW);
                remainingCycles--;
                Serial.printf("Cycle complete. Remaining: %d\n", remainingCycles);
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
            // Optional: add delay between cycles if needed
            break;
    }
}

bool isDispensing() {
    return currentDispenserState != DISPENSER_IDLE;
}

// --- Verification Logic ---
// Note: This still uses blocking loops as it is a startup/maintenance routine.
// We may want to refactor this later if we need it to be responsive to BLE.
void verifySystemIntegrity() {
  updateLcd("System Check", "Verifying Tubes");
  delay(1000);
  stepper.enableOutputs();

  for (int i = 0; i < TOTAL_TUBES; i++) {
    if (i > 0) {
      updateLcd("Moving to", "Tube " + String(i + 1));
      stepper.move(STEPS_PER_TUBE); 
      while (stepper.distanceToGo() != 0) { stepper.run(); }
      currentTubeIndex = i; 
      delay(500); 
    } else {
      updateLcd("Checking", "Tube 1"); delay(500);
    }

    while (true) {
      updateLcd("Scanning...", "Tube " + String(i + 1));
      String detected = identifySpice();
      String expected = spices[i].name;

      if (detected == expected) {
        updateLcd("Tube " + String(i+1) + " OK", detected);
        delay(1000);
        break; 
      } else {
        updateLcd("WRONG TUBE!", "Found: " + detected);
        delay(2000);
        updateLcd("Need: " + expected, "Press 'Enter'");
        
        bool keyPressed = false;
        while (!keyPressed) {
          char key = customKeypad.getKey();
          if (key == 'N') keyPressed = true;
        }
        updateLcd("Re-checking...", ""); delay(1000);
      }
    }
  }

  updateLcd("Check Complete", "Returning Home"); delay(1000);
  int tubesToMoveHome = TOTAL_TUBES - currentTubeIndex; 
  if (tubesToMoveHome > 0 && currentTubeIndex != 0) {
    stepper.move(tubesToMoveHome * STEPS_PER_TUBE);
    while (stepper.distanceToGo() != 0) { stepper.run(); }
  }
  
  currentTubeIndex = 0;
  stepper.disableOutputs();
}
