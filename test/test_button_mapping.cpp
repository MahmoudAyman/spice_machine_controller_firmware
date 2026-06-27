#include <Arduino.h>
#include "Configuration.h"

// Define Button Pins based on Configuration.h
const int buttons[] = {BTN_1, BTN_2, BTN_3, BTN_4, BTN_5};
const char* buttonNames[] = {"BTN_1 (G34)", "BTN_2 (G35)", "BTN_3 (G32)", "BTN_4 (G39)", "BTN_5 (G36)"};
const int numButtons = 5;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- Keypad Mapping Diagnostic ---");
    Serial.println("Goal: Identify physical button to GPIO mapping.");
    Serial.println("Logic: Active-Low (Pressed = 0, Released = 1)");
    Serial.println("----------------------------------------");

    for (int i = 0; i < numButtons; i++) {
        if (buttons[i] != -1) {
            pinMode(buttons[i], INPUT_PULLUP);
            Serial.printf("[INIT] Configured %s as INPUT_PULLUP\n", buttonNames[i]);
        }
    }
}

void loop() {
    static int lastStates[numButtons] = {1, 1, 1, 1, 1};
    bool anyPressed = false;

    for (int i = 0; i < numButtons; i++) {
        if (buttons[i] == -1) continue;

        int currentState = digitalRead(buttons[i]);
        
        // Edge detection: when state changes
        if (currentState != lastStates[i]) {
            if (currentState == LOW) {
                Serial.printf("[EVENT] %s PRESSED (GPIO %d)\n", buttonNames[i], buttons[i]);
            } else {
                Serial.printf("[EVENT] %s RELEASED (GPIO %d)\n", buttonNames[i], buttons[i]);
            }
            lastStates[i] = currentState;
        }

        if (currentState == LOW) anyPressed = true;
    }

    // Optional: Visual indicator of current held buttons every second
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 1000) {
        lastUpdate = millis();
        if (anyPressed) {
            Serial.print("Currently Holding: ");
            for (int i = 0; i < numButtons; i++) {
                if (digitalRead(buttons[i]) == LOW) Serial.printf("[%s] ", buttonNames[i]);
            }
            Serial.println();
        }
    }

    delay(10); // Small debounce/stability delay
}
