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
  pinMode(CS_S0, OUTPUT); pinMode(CS_S1, OUTPUT);
  pinMode(CS_S2, OUTPUT); pinMode(CS_S3, OUTPUT);
  pinMode(CS_OUT, INPUT);
  digitalWrite(CS_S0, HIGH); digitalWrite(CS_S1, LOW);
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

// --- Color Sensor Logic ---
int readColor(char color) {
  switch (color) {
    case 'r': digitalWrite(CS_S2, LOW); digitalWrite(CS_S3, LOW); break;
    case 'g': digitalWrite(CS_S2, HIGH); digitalWrite(CS_S3, HIGH); break;
    case 'b': digitalWrite(CS_S2, LOW); digitalWrite(CS_S3, HIGH); break;
  }
  return pulseIn(CS_OUT, LOW, 1000000);
}

String identifySpice() {
  long totalR = 0, totalG = 0, totalB = 0;
  for (int i=0; i<5; i++) {
    totalR += readColor('r'); delay(20);
    totalG += readColor('g'); delay(20);
    totalB += readColor('b'); delay(20);
  }
  int r = totalR / 5; int g = totalG / 5; int b = totalB / 5;
  Serial.printf("Read Avg RGB: %d, %d, %d\n", r, g, b);

  long smallestDifference = -1;
  String closestSpiceName = "Unknown";

  for (int i = 0; i < NUM_SPICES; i++) {
    long difference = abs(spices[i].r_val - r) + 
                      abs(spices[i].g_val - g) + 
                      abs(spices[i].b_val - b);
    if (smallestDifference == -1 || difference < smallestDifference) {
      smallestDifference = difference;
      closestSpiceName = spices[i].name;
    }
  }
  
  if (smallestDifference > MATCH_THRESHOLD) return "Unknown";
  else return closestSpiceName;
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
    // Activates briefly after every servo cycle to dislodge stuck spice
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