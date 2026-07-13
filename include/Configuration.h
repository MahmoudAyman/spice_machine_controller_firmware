#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <Arduino.h>

// --- System Configuration ---
#define SIMULATION_MODE 1  // Set to 0 for actual hardware

// --- Pin Definitions (Custom PCB V1.0) ---

// TFT SPI LCD (ST7735 or similar)
#define TFT_CS        5
#define TFT_SCLK      18
#define TFT_MOSI      23
#define TFT_DC        27
#define TFT_RST       33
#define TFT_LED       25

// Stepper Motor (A4988/DRV8825)
#define STEP_ENABLE_PIN -1   // CRITICAL: GPIO 19 is tied to ESP32 EN pin on PCB. MUST NOT BE USED.
#define STEP_PIN        21
#define STEP_DIR_PIN    22

// Limit Switch (Sensing structure alignment)
#define LIMIT_SWITCH_PIN 36  // SENSOR_VP pin, pulled low externally, HIGH when pressed

// Homing Limit Switch (Sensing absolute home position / Slot 1)
#define HOMING_SWITCH_PIN 39 // SENSOR_VN pin, pulled low externally, HIGH when pressed

// Discrete Buttons (Verified Hardware Mapping)
#define BTN_UP        26 // BTN_4
#define BTN_DOWN      14 // BTN_3
#define BTN_LEFT      13 // BTN_2
#define BTN_RIGHT     12 // BTN_1
#define BTN_OK        32 // BTN_5

// Legacy compatibility for Hardware.cpp initialization
#define BTN_1         12
#define BTN_2         13
#define BTN_3         14
#define BTN_4         26
#define BTN_5         32

// Color Sensor (TCS3200)
#define CS_S0         17
#define CS_S1         16
#define CS_S2         2
#define CS_S3         4
#define CS_OUT        34

// Other Peripherals
#define LASER_RX_PIN  35 
#define LASER_FILLED_STATE LOW
#define SERVO_PIN     15
#define VIBRATOR_PIN  -1  // Currently unused/disabled

// --- Constants ---
#define STEPS_PER_REVOLUTION 1600
#define TOTAL_TUBES          20
const int STEPS_PER_TUBE = STEPS_PER_REVOLUTION / TOTAL_TUBES; 

// Timing
const long WAIT_DURATION_EMPTY = 5000; 
const long WAIT_DURATION_FILLED = 5000; 
const long MATCH_THRESHOLD = 150; 

// Dispensing Calibration
const float MAX_SPICE_GRAMS = 150.0; // Physical capacity of each tube in grams
const float GRAMS_PER_CYCLE = 0.2;
const int CYCLES_PER_THEELEPEL = 10; 

// Color Sensor Calibration (Legacy)
const int WHITE_R = 25;
const int WHITE_G = 30;
const int WHITE_B = 22;
const int BLACK_R = 250;
const int BLACK_G = 300;
const int BLACK_B = 280;

#endif
