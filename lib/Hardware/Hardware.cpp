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

void startIdentifySpice() {
    colorDetector.startIdentification(spices, NUM_SPICES, MATCH_THRESHOLD);
}

bool isIdentifying() {
    return colorDetector.isBusy();
}

String getIdentifiedSpice() {
    return colorDetector.getResult();
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
            break;
    }
}

bool isDispensing() {
    return currentDispenserState != DISPENSER_IDLE;
}

// --- Verification Logic ---
// NO LONGER BLOCKING. Uses a state machine for startup check.
enum IntegrityState {
    INT_IDLE,
    INT_MOVING,
    INT_SCANNING,
    INT_WAIT_USER,
    INT_COMPLETE
};
static IntegrityState intState = INT_IDLE;
static int intTubeIdx = 0;
static bool intSuccess = true;

void verifySystemIntegrity() {
    // Legacy blocking version removed. Logic moved to main loop states if needed,
    // or kept as a special "System Check" state.
    // For now, we'll keep the function signature but make it non-blocking if called.
    // Realistically, verifySystemIntegrity should be a STATE in the main machine.
}
