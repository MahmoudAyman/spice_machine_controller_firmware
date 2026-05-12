#include "ColorDetector.h"

ColorDetector::ColorDetector(int s0, int s1, int s2, int s3, int out) 
    : _s0(s0), _s1(s1), _s2(s2), _s3(s3), _out(out) {}

void ColorDetector::begin() {
    pinMode(_s0, OUTPUT);
    pinMode(_s1, OUTPUT);
    pinMode(_s2, OUTPUT);
    pinMode(_s3, OUTPUT);
    pinMode(_out, INPUT);
    
    // Set scaling to 20%
    digitalWrite(_s0, HIGH);
    digitalWrite(_s1, LOW);
}

void ColorDetector::setCalibration(int whiteR, int whiteG, int whiteB, int blackR, int blackG, int blackB) {
    _wR = whiteR; _wG = whiteG; _wB = whiteB;
    _bR = blackR; _bG = blackG; _bB = blackB;
}

int ColorDetector::readRawColor(char color) {
    switch (color) {
        case 'r': digitalWrite(_s2, LOW); digitalWrite(_s3, LOW); break;
        case 'g': digitalWrite(_s2, HIGH); digitalWrite(_s3, HIGH); break;
        case 'b': digitalWrite(_s2, LOW); digitalWrite(_s3, HIGH); break;
    }
    return pulseIn(_out, LOW, 1000000);
}

int ColorDetector::readMappedColor(char color) {
    int raw = readRawColor(color);
    int white, black;
    switch (color) {
        case 'r': white = _wR; black = _bR; break;
        case 'g': white = _wG; black = _bG; break;
        case 'b': white = _wB; black = _bB; break;
        default: return 0;
    }
    int mapped = map(raw, white, black, 255, 0);
    return constrain(mapped, 0, 255);
}

String ColorDetector::identify(const Spice* spices, int numSpices, int matchThreshold) {
    long totalR = 0, totalG = 0, totalB = 0;
    for (int i = 0; i < 5; i++) {
        totalR += readRawColor('r'); delay(20);
        totalG += readRawColor('g'); delay(20);
        totalB += readRawColor('b'); delay(20);
    }
    int r = totalR / 5;
    int g = totalG / 5;
    int b = totalB / 5;

    long smallestDifference = -1;
    String closestSpiceName = "Unknown";

    for (int i = 0; i < numSpices; i++) {
        long difference = abs(spices[i].r_val - r) + 
                          abs(spices[i].g_val - g) + 
                          abs(spices[i].b_val - b);
        if (smallestDifference == -1 || difference < smallestDifference) {
            smallestDifference = difference;
            closestSpiceName = spices[i].name;
        }
    }
    
    if (smallestDifference > matchThreshold || smallestDifference == -1) return "Unknown";
    else return closestSpiceName;
}
