# Spice Mixer – Intelligent Dispensing System

**Production Firmware Version:** **2.4**

---

## 1. Project Overview

The **Spice Mixer** is an automated intelligent dispensing machine capable of storing and accurately dispensing **20 different spices**.

### Key Features

* **Precision Stepper Motor** for rotating the spice rack
* **Color Sensor** to verify the correct spice tube
* **Servo Motor** for precise quantity dispensing
* Supports both:

  * **Custom Manual Dispensing**
  * **Pre-set Recipe Dispensing**

The system is designed to ensure **accuracy, repeatability, and safety** during operation.


## 2. Hardware Configuration

### Microcontroller

* **ESP32 DevKit V1**


### Pinout Configuration Table

| Component                            | Pin Name | ESP32 GPIO         | Notes                  |
| ------------------------------------ | -------- | ------------------ | ---------------------- |
| **Stepper Driver (A4988 / DRV8825)** | STEP     | 19                 |                        |
|                                      | DIR      | 23                 |                        |
|                                      | ENABLE   | 18                 | Active LOW             |
| **OLED Display**                     | SDA      | 21                 | I2C Data               |
|                                      | SCL      | 22                 | I2C Clock              |
| **Color Sensor (TCS3200)**           | S0       | 17                 | Frequency Scaling      |
|                                      | S1       | 16                 |                        |
|                                      | S2       | 2                  | Filter Selection       |
|                                      | S3       | 5                  |                        |
|                                      | OUT      | 34                 | Signal Input           |
| **Laser Sensor**                     | OUT      | 35                 | Analog / Digital Input |
| **Servo Motor**                      | Signal   | 15                 | PWM Output             |
| **Keypad (Rows)**                    | R1–R5    | 32, 33, 25, 26, 27 |                        |
| **Keypad (Columns)**                 | C1–C4    | 14, 12, 13, 4      |                        |


## 3. Software Setup Guide

### Step 1: Install Required Libraries
A folder named libraries is present in the repository you can use those libraries OR install the following libraries using the **Arduino Library Manager**:

* **Adafruit GFX Library**
* **Adafruit SSD1306**
* **AccelStepper**
* **ESP32Servo**
* **Keypad**


### Step 2: Color Sensor Calibration (**CRITICAL**)

Lighting conditions vary between environments.
The color sensor **must be calibrated** for your specific spice tubes before running the main system.

#### Calibration Procedure

1. Open the folder **`colordetect`**
2. Upload **`colordetect.ino`** to the ESP32
3. Open **Serial Monitor** at **115200 baud**
4. Place **Tube 1 (Paprika)** in front of the color sensor
5. Copy the displayed RGB values

   * Example: `{ 180, 420, 380 }`
6. Open **`Database.h`** in the main firmware folder
7. Replace the Paprika RGB values with the new readings
8. Repeat the process for **all 20 spice tubes**

⚠️ **Important:**
Skipping calibration may cause **incorrect tube detection** and system failure.

### Step 3: Upload Main Firmware

1. Open the folder **`SpiceMixer_Firmware`**
2. Open **`SpiceMixer_Firmware.ino`**
3. Ensure all required files are open:

   * `Configuration.h`
   * `Database.h`
   * `Hardware.h / Hardware.cpp`
   * `Globals.h`
4. Select:

   * **Board:** DOIT ESP32 DEVKIT V1
5. Upload the firmware

## 4. Code Structure Explained

The project follows a **modular architecture** for reliability, scalability, and easy maintenance.

### File Breakdown

* **SpiceMixer_Firmware.ino**
  Main control logic. Implements the system **state machine**
  (Main Menu → Recipe Mode → Dispensing Loop)

* **Configuration.h**
  System configuration file

  * Pin definitions
  * Motor speeds
  * Calibration constants (e.g., `0.2g per servo cycle`)

* **Database.h**
  Data storage file

  * Spice RGB color database
  * Recipe definitions
  * Used to add or modify recipes

* **Hardware.cpp / Hardware.h**
  Hardware abstraction layer

  * Motor control
  * Sensor readings
  * Servo operation

* **Globals.h**
  Shared variables and references across modules


## 5. User Manual & Workflow

### A. System Startup (Integrity Check)

On power-up, the machine performs a **full integrity check**:

1. Rotates to **Tube 1**
2. Scans the color and verifies it is **Paprika**
3. Continues verification for all **20 tubes**

#### Wrong Tube Detection

* The machine **stops immediately**
* Displays **"WRONG TUBE"**
* Shows which spice should be placed
* User must swap the tube and press **Enter** to continue

After successful verification, the system enters the **Main Menu**.

### B. Main Menu

* Press **1** → **Recipe Mode**
* Press **2** → **Manual Mode**


### C. Recipe Mode

#### User Input

1. Enter **Recipe Number** (1–12)
2. Enter **Number of Servings** (e.g., 3)

#### Automatic Operation

* Calculates required quantity:
  **Base Grams × Servings**
* Moves to each ingredient
* Uses **Laser Sensor** to check tube fill level
* Dispenses spice using servo motor

  * **1 Servo Cycle = 0.2g**
* Repeats until recipe is completed

#### Empty Tube Handling

* Machine **pauses automatically**
* Displays refill prompt
* After refilling, press **Enter**
* Dispensing resumes from the exact stopping point

---

### D. Manual Mode

#### User Input

1. Enter **Tube Number** (1–20)
2. Enter quantity in **Theelepels (Teaspoons)**

#### Conversion Logic

* **1 Theelepel = 2g**
* **1 Theelepel = 10 Servo Cycles**

The machine dispenses the selected spice and returns to the **Home Position**.

