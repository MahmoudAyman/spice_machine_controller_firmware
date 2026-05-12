#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <Arduino.h>

// --- System Configuration ---
#define SIMULATION_MODE 1  // Set to 1 to test without physical hardware connected

// --- Pin Definitions ---
// Display
#define I2C_SDA       21
#define I2C_SCL       22
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Keypad
const byte ROWS = 5;
const byte COLS = 4;
const byte rowPins[ROWS] = {32, 33, 25, 26, 27};
const byte colPins[COLS] = {14, 12, 13, 4};

// Stepper
#define STEP_PIN      19
#define DIR_PIN       23
#define ENABLE_PIN    18

// Sensors
#define CS_S0         17
#define CS_S1         16
#define CS_S2         2
#define CS_S3         5
#define CS_OUT        34
#define LASER_RX_PIN  35 

// Servo & Vibrator
#define SERVO_PIN     15
#define VIBRATOR_PIN  1   // NOTE: GPIO 1 is Serial TX. Using it for hardware will block Serial. 

// --- Constants ---
#define STEPS_PER_REVOLUTION 820
#define TOTAL_TUBES          20
const int STEPS_PER_TUBE = STEPS_PER_REVOLUTION / TOTAL_TUBES; 

// Timing
const long WAIT_DURATION_EMPTY = 5000; 
const long WAIT_DURATION_FILLED = 5000; 
const long MATCH_THRESHOLD = 150; 

// Dispensing Calibration
const float GRAMS_PER_CYCLE = 0.2;
const int CYCLES_PER_THEELEPEL = 10; // 1 Tlp = 2g = 10 cycles

// Color Sensor Calibration (Legacy)
const int WHITE_R = 25;
const int WHITE_G = 30;
const int WHITE_B = 22;
const int BLACK_R = 250;
const int BLACK_G = 300;
const int BLACK_B = 280;

#endif