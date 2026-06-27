#include <Arduino.h>
#include "LCDManager.h"
#include "Configuration.h"

LCDManager lcd;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- LCD Hardware Diagnostic ---");
    Serial.println("Target: ILI9341 2.4\" TFT (240x320)");
    Serial.println("Mode: Software SPI (Safe for GPIO 19)");
    
    // Initialize LCD
    lcd.begin();
    
    Serial.println("[STEP 1] Running Full Diagnostic Pattern...");
    lcd.runDiagnostic();
    delay(3000);
}

void loop() {
    static int progress = 0;
    static bool bleSim = false;
    
    Serial.printf("[STEP 2] Testing Partial Refreshes (Progress: %d%%, BLE: %s)\n", 
                  progress, bleSim ? "ON" : "OFF");

    // Update only specific parts to test speed and stability
    lcd.updateHeader("System Test", bleSim ? 2 : 1);
    
    if (progress == 0) {
        lcd.updateContent("Refreshing...", "Safe SPI Mode");
    }

    // Using ILI9341 color constants
    lcd.updateStatus("Cycle: " + String(millis() / 1000) + "s", 
                     bleSim ? ILI9341_CYAN : ILI9341_YELLOW);
    
    lcd.drawProgressBar(progress);

    progress += 5;
    if (progress > 100) {
        progress = 0;
        bleSim = !bleSim; // Toggle BLE icon simulation
        Serial.println("[EVENT] Resetting progress bar and toggling BLE icon.");
    }

    delay(200);
}
