#ifndef LASER_LEVEL_SENSOR_H
#define LASER_LEVEL_SENSOR_H

#include <Arduino.h>

class LaserLevelSensor {
public:
    LaserLevelSensor(int rxPin);
    void begin();
    bool isFilled();

private:
    int _rxPin;
};

#endif // LASER_LEVEL_SENSOR_H
