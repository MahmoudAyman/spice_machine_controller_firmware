#include <Arduino.h>
#include "Configuration.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Laser Level Sensor Calibration & Diagnostic ---");
    Serial.println("Goal: Verify and calibrate KY-008 Laser source and Laser RX module.");
    Serial.println("Note: The Laser source is wired to VCC (Always On).");
    Serial.println("The Laser receiver is wired to LASER_RX_PIN.");
    Serial.printf("Configuring LASER_RX_PIN (GPIO %d) as INPUT...\n", LASER_RX_PIN);
    Serial.println("--------------------------------------------------");

    pinMode(LASER_RX_PIN, INPUT);
}

void loop() {
    static int lastLaserState = -1;
    static unsigned long lastChangeTime = 0;
    int rawState = digitalRead(LASER_RX_PIN);

    // Filter/Debounce state changes slightly to prevent noise flickering
    if (rawState != lastLaserState) {
        if (millis() - lastChangeTime > 50) { // 50ms stable debounce
            lastLaserState = rawState;
            lastChangeTime = millis();

            if (rawState == HIGH) {
                Serial.println("[SENSOR EVENT] LASER_RX_PIN is HIGH - Beam is Blocked (or Light Absent)");
                Serial.println("               (Matches typical pull-up HIGH when laser is blocked)");
            } else {
                Serial.println("[SENSOR EVENT] LASER_RX_PIN is LOW - Beam is Unblocked (or Light Present)");
                Serial.println("               (Matches active-LOW trigger when laser light hits the receiver)");
            }
        }
    }

    // Print a periodic heartbeat every 2 seconds with the current state
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 2000) {
        lastHeartbeat = millis();
        Serial.printf("[HEARTBEAT] Current Laser Receiver State: %s (Raw Value: %d)\n", 
                      (rawState == HIGH) ? "HIGH (Blocked/Full)" : "LOW (Unblocked/Empty)", 
                      rawState);
    }

    delay(10);
}
