#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "Configuration.h"

class LCDManager {
public:
    LCDManager();

    void begin();
    void clear();
    
    // UI Components
    void updateHeader(String title, bool bleConnected = false);
    void updateStatus(String status, uint16_t color = ILI9341_WHITE);
    void updateContent(String line1, String line2 = "");
    void drawProgressBar(int progress, int y = 200);
    
    // Hardware Diagnostics
    void runDiagnostic();
    void setBacklight(bool on);

    // Legacy Support (to ease refactoring)
    void updateLegacy(String line1, String line2);

private:
    Adafruit_ILI9341 _tft;
    bool _isInitialized;
    
    // Internal drawing helpers to minimize flicker
    void _drawBLEIcon(bool connected);
    String _lastHeaderTitle;
    String _lastStatus;
    bool _lastBleState;
};

#endif
