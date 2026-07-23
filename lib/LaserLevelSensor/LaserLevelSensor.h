#ifndef LASER_LEVEL_SENSOR_H
#define LASER_LEVEL_SENSOR_H

#include <Arduino.h>

class LaserLevelSensor {
public:
    LaserLevelSensor(int rxPin, int enPin, int threshold = 180);
    void begin();
    
    // Non-blocking state machine methods
    void update();
    void triggerRead();
    bool isBusy();
    bool isFilled();

private:
    enum SensorState { IDLE, WAIT_AMBIENT, WAIT_LASER };
    SensorState _state;
    
    int _rxPin;
    int _enPin;
    int _threshold;
    int _ambientVal;
    bool _isFilled;
    unsigned long _lastTimer;
};

#endif // LASER_LEVEL_SENSOR_H