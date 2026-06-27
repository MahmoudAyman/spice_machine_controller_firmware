#include <Arduino.h>
#include <ESP32Servo.h>
#include "Configuration.h"

Servo testServo;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Servo Motor Hardware Test ---");
    Serial.println("SERVO Pin: " + String(SERVO_PIN));

    // Attach the servo
    // Standard ESP32Servo attach (pin, min_pulse_ms, max_pulse_ms)
    // Common values are 500us to 2500us for 0-180 degrees
    testServo.attach(SERVO_PIN, 500, 2500); 
    
    Serial.println("[TEST 1] Moving to 0 degrees...");
    testServo.write(0);
    delay(2000);
}

void loop() {
    Serial.println("[TEST 2] Sweeping 0 -> 90 degrees...");
    for (int pos = 0; pos <= 90; pos += 10) {
        testServo.write(pos);
        Serial.printf("Position: %d\n", pos);
        delay(500);
    }
    
    delay(1000);

    Serial.println("[TEST 3] Sweeping 90 -> 0 degrees...");
    for (int pos = 90; pos >= 0; pos -= 10) {
        testServo.write(pos);
        Serial.printf("Position: %d\n", pos);
        delay(500);
    }

    delay(2000);
}
