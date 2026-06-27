#include <Arduino.h>
#include "Configuration.h"
#include "ColorDetector.h"

// Instantiate the color detector on the configured pins
ColorDetector testDetector(CS_S0, CS_S1, CS_S2, CS_S3, CS_OUT);

// Local calibration variables
int calWhiteR = WHITE_R;
int calWhiteG = WHITE_G;
int calWhiteB = WHITE_B;
int calBlackR = BLACK_R;
int calBlackG = BLACK_G;
int calBlackB = BLACK_B;

void printCalibration() {
    Serial.println("\n========================================");
    Serial.println("  CURRENT TCS3200 CALIBRATION VALUES");
    Serial.println("========================================");
    Serial.printf("White Reference: R=%d, G=%d, B=%d\n", calWhiteR, calWhiteG, calWhiteB);
    Serial.printf("Black Reference: R=%d, G=%d, B=%d\n", calBlackR, calBlackG, calBlackB);
    Serial.println("----------------------------------------");
    Serial.println("Copy and paste these into Configuration.h if satisfied:");
    Serial.printf("const int WHITE_R = %d;\n", calWhiteR);
    Serial.printf("const int WHITE_G = %d;\n", calWhiteG);
    Serial.printf("const int WHITE_B = %d;\n", calWhiteB);
    Serial.printf("const int BLACK_R = %d;\n", calBlackR);
    Serial.printf("const int BLACK_G = %d;\n", calBlackG);
    Serial.printf("const int BLACK_B = %d;\n", calBlackB);
    Serial.println("========================================\n");
}

void printHelp() {
    Serial.println("\n--- TCS3200 Color Sensor Interactive Test ---");
    Serial.println("Commands:");
    Serial.println("  [h] - Show this help menu");
    Serial.println("  [r] - Read continuous raw sensor values (Red, Green, Blue pulse widths)");
    Serial.println("  [m] - Read continuous mapped sensor values (0 - 255 RGB)");
    Serial.println("  [w] - Calibrate WHITE (Place a WHITE tube / card over sensor, then press enter)");
    Serial.println("  [b] - Calibrate BLACK (Place a BLACK tube / card over sensor, then press enter)");
    Serial.println("  [c] - Print currently stored calibration constants");
    Serial.println("  [t] - Run real-time detection (Classifies as White, Black, or Unknown)");
    Serial.println("  [s] - Stop continuous readings");
    Serial.println("---------------------------------------------");
}

enum TestMode {
    MODE_IDLE,
    MODE_RAW_CONTINUOUS,
    MODE_MAPPED_CONTINUOUS,
    MODE_DETECTION_CONTINUOUS
};

TestMode currentMode = MODE_IDLE;
unsigned long lastReadTime = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\nInitializing GY-31 TCS3200 Color Detector...");
    testDetector.begin();
    testDetector.setCalibration(calWhiteR, calWhiteG, calWhiteB, calBlackR, calBlackG, calBlackB);
    
    Serial.println("Color detector initialized successfully.");
    printHelp();
}

void loop() {
    // Process serial commands
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        // Consume carriage returns / line feeds
        while (Serial.available() > 0 && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
            Serial.read();
        }
        
        switch (cmd) {
            case 'h':
                printHelp();
                currentMode = MODE_IDLE;
                break;
            case 'r':
                Serial.println("\nStarting raw pulse width readings (lower means higher intensity/frequency)...");
                currentMode = MODE_RAW_CONTINUOUS;
                break;
            case 'm':
                Serial.println("\nStarting mapped RGB readings (0 to 255 scale)...");
                currentMode = MODE_MAPPED_CONTINUOUS;
                break;
            case 'c':
                printCalibration();
                break;
            case 's':
                Serial.println("\nStopping readings.");
                currentMode = MODE_IDLE;
                break;
            case 'w': {
                Serial.println("\n--- Calibrating WHITE Reference ---");
                Serial.println("Averaging 10 samples... Keep white object steady.");
                long sumR = 0, sumG = 0, sumB = 0;
                for (int i = 0; i < 10; i++) {
                    sumR += testDetector.readRawColor('r'); delay(30);
                    sumG += testDetector.readRawColor('g'); delay(30);
                    sumB += testDetector.readRawColor('b'); delay(30);
                }
                calWhiteR = sumR / 10;
                calWhiteG = sumG / 10;
                calWhiteB = sumB / 10;
                testDetector.setCalibration(calWhiteR, calWhiteG, calWhiteB, calBlackR, calBlackG, calBlackB);
                Serial.println("WHITE Calibration complete!");
                printCalibration();
                break;
            }
            case 'b': {
                Serial.println("\n--- Calibrating BLACK Reference ---");
                Serial.println("Averaging 10 samples... Keep black object steady.");
                long sumR = 0, sumG = 0, sumB = 0;
                for (int i = 0; i < 10; i++) {
                    sumR += testDetector.readRawColor('r'); delay(30);
                    sumG += testDetector.readRawColor('g'); delay(30);
                    sumB += testDetector.readRawColor('b'); delay(30);
                }
                calBlackR = sumR / 10;
                calBlackG = sumG / 10;
                calBlackB = sumB / 10;
                testDetector.setCalibration(calWhiteR, calWhiteG, calWhiteB, calBlackR, calBlackG, calBlackB);
                Serial.println("BLACK Calibration complete!");
                printCalibration();
                break;
            }
            case 't':
                Serial.println("\nStarting real-time classification (Black vs White)...");
                currentMode = MODE_DETECTION_CONTINUOUS;
                break;
            default:
                break;
        }
    }

    // Run continuous operations
    unsigned long now = millis();
    if (now - lastReadTime >= 500) {
        lastReadTime = now;
        
        if (currentMode == MODE_RAW_CONTINUOUS) {
            int r = testDetector.readRawColor('r');
            int g = testDetector.readRawColor('g');
            int b = testDetector.readRawColor('b');
            Serial.printf("RAW -> R: %d | G: %d | B: %d\n", r, g, b);
        } 
        else if (currentMode == MODE_MAPPED_CONTINUOUS) {
            int r = testDetector.readMappedColor('r');
            int g = testDetector.readMappedColor('g');
            int b = testDetector.readMappedColor('b');
            Serial.printf("MAPPED (0-255) -> R: %d | G: %d | B: %d\n", r, g, b);
        } 
        else if (currentMode == MODE_DETECTION_CONTINUOUS) {
            int r = testDetector.readRawColor('r');
            int g = testDetector.readRawColor('g');
            int b = testDetector.readRawColor('b');
            
            // Basic matching calculation
            long diffWhite = abs(calWhiteR - r) + abs(calWhiteG - g) + abs(calWhiteB - b);
            long diffBlack = abs(calBlackR - r) + abs(calBlackG - g) + abs(calBlackB - b);
            
            String label = "Unknown";
            long threshold = 180; // Allow a moderate variance
            
            if (diffWhite < diffBlack && diffWhite < threshold) {
                label = "WHITE Tube";
            } else if (diffBlack < diffWhite && diffBlack < threshold) {
                label = "BLACK Tube";
            }
            
            Serial.printf("RAW -> R: %d G: %d B: %d | DiffWhite: %ld | DiffBlack: %ld | Detected: %s\n", 
                r, g, b, diffWhite, diffBlack, label.c_str());
        }
    }
}
