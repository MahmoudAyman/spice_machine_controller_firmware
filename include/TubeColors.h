#ifndef TUBE_COLORS_H
#define TUBE_COLORS_H

#include <Arduino.h>

// --- Tube Color Reference Structure ---
struct TubeColorRef {
    String name;
    int r;
    int g;
    int b;
};

// Number of registered test tube colors
const int NUM_TUBE_COLORS = 4;

// Color reference calibration array (Raw TCS3200 pulse width values)
const TubeColorRef TUBE_COLORS[NUM_TUBE_COLORS] = {
    {"YELLOW", 87, 125, 155},
    {"BLACK",   233, 172, 121},
    {"WHITE",  84, 87, 80},
    {"BLUE",  269, 270, 239}
};

// Helper function to find the closest matching registered tube color
inline String getMatchingTubeColor(int r, int g, int b, long threshold = 40) {
    long smallestDifference = -1;
    String closestMatch = "Unknown";

    for (int i = 0; i < NUM_TUBE_COLORS; i++) {
        long difference = abs(TUBE_COLORS[i].r - r) + 
                          abs(TUBE_COLORS[i].g - g) + 
                          abs(TUBE_COLORS[i].b - b);
        
        if (smallestDifference == -1 || difference < smallestDifference) {
            smallestDifference = difference;
            closestMatch = TUBE_COLORS[i].name;
        }
    }

    if (smallestDifference > threshold || smallestDifference == -1) {
        return "Unknown";
    }
    return closestMatch;
}

#endif
