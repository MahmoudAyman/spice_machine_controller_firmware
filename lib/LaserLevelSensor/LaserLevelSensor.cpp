#include "LaserLevelSensor.h"
#include "Configuration.h"
#include "Globals.h"

LaserLevelSensor::LaserLevelSensor(int rxPin) : _rxPin(rxPin) {}

void LaserLevelSensor::begin() {
    if (simulationEnabled) {
        Serial.println("[LASER] Simulation active. Skipping hardware pin configuration.");
        return;
    }
    Serial.printf("[LASER] Initializing Laser Receiver on GPIO %d...\n", _rxPin);
    pinMode(_rxPin, INPUT);
}

bool LaserLevelSensor::isFilled() {
    if (simulationEnabled) {
        return true; // Always return "Filled" (Level OK) in simulation mode
    }
    int rawVal = digitalRead(_rxPin);
    return (rawVal == LASER_FILLED_STATE);
}
