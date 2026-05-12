#ifndef COLOR_DETECTOR_H
#define COLOR_DETECTOR_H

#include <Arduino.h>
#include "Database.h"

class ColorDetector {
public:
    ColorDetector(int s0, int s1, int s2, int s3, int out);
    void begin();
    void setCalibration(int whiteR, int whiteG, int whiteB, int blackR, int blackG, int blackB);
    int readRawColor(char color);
    int readMappedColor(char color);
    String identify(const Spice* spices, int numSpices, int matchThreshold);

private:
    int _s0, _s1, _s2, _s3, _out;
    int _wR = 0, _wG = 0, _wB = 0;
    int _bR = 1000, _bG = 1000, _bB = 1000;
};

#endif
