#ifndef COLOR_DETECTOR_H
#define COLOR_DETECTOR_H

#include <Arduino.h>
#include "Database.h"

enum ColorDetectionState {
    COLOR_IDLE,
    COLOR_SAMPLING_RED,
    COLOR_SAMPLING_GREEN,
    COLOR_SAMPLING_BLUE,
    COLOR_COMPLETE
};

class ColorDetector {
public:
    ColorDetector(int s0, int s1, int s2, int s3, int out);
    void begin();
    void setCalibration(int whiteR, int whiteG, int whiteB, int blackR, int blackG, int blackB);
    
    // --- Async Interface ---
    void startIdentification(const Spice* spices, int numSpices, int matchThreshold);
    void tick();
    bool isBusy() { return _state != COLOR_IDLE && _state != COLOR_COMPLETE; }
    bool isComplete() { return _state == COLOR_COMPLETE; }
    String getResult() { return _result; }
    void reset() { _state = COLOR_IDLE; _result = "Unknown"; }

    // --- Blocking Interface (Legacy/Setup) ---
    int readRawColor(char color);
    int readMappedColor(char color);
    String identify(const Spice* spices, int numSpices, int matchThreshold);

private:
    int _s0, _s1, _s2, _s3, _out;
    int _wR = 0, _wG = 0, _wB = 0;
    int _bR = 1000, _bG = 1000, _bB = 1000;

    // Async State
    ColorDetectionState _state = COLOR_IDLE;
    const Spice* _targetSpices;
    int _targetNumSpices;
    int _targetThreshold;
    int _sampleCount = 0;
    long _accR, _accG, _accB;
    unsigned long _lastSampleTime = 0;
    String _result = "Unknown";
};

#endif
