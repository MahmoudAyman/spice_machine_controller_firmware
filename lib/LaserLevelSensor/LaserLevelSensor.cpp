#include "LaserLevelSensor.h"
#include "Configuration.h"
#include "Globals.h"

LaserLevelSensor::LaserLevelSensor(int rxPin, int enPin, int threshold) 
    : _rxPin(rxPin), _enPin(enPin), _threshold(threshold), 
      _state(IDLE), _ambientVal(0), _isFilled(false), _lastTimer(0) {}

void LaserLevelSensor::begin() {
    if (simulationEnabled) {
        Serial.println("LASER Simulation active. Skipping hardware pin configuration.");
        return;
    }
    Serial.printf("LASER Initializing Analog LDR on GPIO %d, EN on GPIO %d, Threshold: %d...\n", _rxPin, _enPin, _threshold);
    
    pinMode(_enPin, OUTPUT);
    digitalWrite(_enPin, LOW); 
    analogReadResolution(12);  
}

void LaserLevelSensor::triggerRead() {
    if (_state != IDLE || simulationEnabled) return;
    
    Serial.println("LASER triggerRead() started. Setting EN to LOW (Baseline).");
    digitalWrite(_enPin, LOW); // Ensure laser is off for baseline
    _lastTimer = millis();
    _state = WAIT_AMBIENT;
}

void LaserLevelSensor::update() {
    if (simulationEnabled || _state == IDLE) return;

    unsigned long currentMillis = millis();

    if (_state == WAIT_AMBIENT && (currentMillis - _lastTimer >= 500)) {
        _ambientVal = analogRead(_rxPin);
        
        Serial.printf("LASER Ambient Read -> Value: %d (Elapsed: %lu ms). Setting EN to HIGH...\n", 
                      _ambientVal, currentMillis - _lastTimer);
        
        digitalWrite(_enPin, HIGH); // Turn laser on
        _lastTimer = millis();
        _state = WAIT_LASER;
    } 
    else if (_state == WAIT_LASER && (currentMillis - _lastTimer >= 500)) {
        
        // Take a quick average of 5 readings to smooth out ADC noise
        int sum = 0;
        for(int i = 0; i < 5; i++) {
            sum += analogRead(_rxPin);
            delay(2); 
        }
        int laserVal = sum / 5;
        
        digitalWrite(_enPin, LOW); // Turn laser off immediately
        
        int delta = abs(laserVal - _ambientVal);
        _isFilled = (delta < _threshold);
        
        Serial.printf("LASER Active Read -> Value: %d (Elapsed: %lu ms). Delta: %d (Threshold: %d).\n", 
                      laserVal, currentMillis - _lastTimer, delta, _threshold);
        Serial.printf("LASER Result -> %s\n", _isFilled ? "FILLED (Light Blocked)" : "EMPTY (Light Detected)");
        
        _state = IDLE; // Reading complete
    }
}

bool LaserLevelSensor::isBusy() {
    return _state != IDLE;
}

bool LaserLevelSensor::isFilled() {
    if (simulationEnabled) return true; 
    Serial.print("Fill Sensor Final State:  ");
    Serial.println(_isFilled);
    return _isFilled;
}