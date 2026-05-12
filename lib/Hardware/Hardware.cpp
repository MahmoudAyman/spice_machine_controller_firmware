#include "Hardware.h"
#include "Globals.h"
#include "Configuration.h"
#include "Database.h"

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

// --- Dispensing Logic ---
void dispenseSpice(int totalCycles) {
  Serial.printf("Dispensing %d cycles...\n", totalCycles);
  for (int i = 0; i < totalCycles; i++) {
    // Servo Sweep Forward
    for (int pos = 0; pos <= 360; pos += 10) { dispenserServo.write(pos); }
    // Servo Sweep Backward
    for (int pos = 360; pos >= 0; pos -= 10) { dispenserServo.write(pos); }
    
    // --- Vibrator Activation ---
    digitalWrite(VIBRATOR_PIN, HIGH);
    delay(150); // Vibrate for 150ms
    digitalWrite(VIBRATOR_PIN, LOW);

    Serial.printf("Cycle %d/%d complete.\n", i + 1, totalCycles);
  }
}

// --- Verification Logic ---
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
