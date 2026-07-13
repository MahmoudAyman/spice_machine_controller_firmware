#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "Configuration.h"
#include <SPI.h>

#define ILI9341_ORANGE      0xFD20

class LCDManager {
public:
    LCDManager();

    void begin();
    void clear();
    void clearViewCache();
    
    // Core Views (Refactored)
    void showMenu(String title, const char* options[], int count, int selectedIndex, String leftLab = "", String okLab = "Select", String rightLab = "");
    void showNumericSelection(String title, String value, String unit, String leftLab = "Back", String okLab = "Start", String rightLab = "");
    void showOperationView(String title, String spiceName, int progress, String status, String cancelLab = "Abort");
    
    // UI Components
    void updateHeader(String title, int bleStatus); // 0: Disconnected, 1: Advertising, 2: Connected
    void updateStatus(String status, uint16_t color = ILI9341_WHITE);
    void updateContent(String line1, String line2 = "");
    void updateSpiceName(String name); // Line 1 (Surgical)
    void updateTask(String task);   // Line 2 (Surgical)
    void updateDetail(String detail); // Line 3 (Surgical)
    void setActionBar(String left, String ok = "", String right = "");
    void drawProgressBar(int progress, int y = 140, bool forceRedraw = false); 
    
    // Hardware Diagnostics
    void runDiagnostic();
    void setBacklight(bool on);

private:
    Adafruit_ILI9341 _tft;
    bool _isInitialized;
    
    // Internal drawing helpers to minimize flicker
    void _drawBLEStatus(int status);
    void _drawActionBar(String left, String ok, String right);
    
    String _lastHeaderTitle;
    String _lastStatus;
    String _lastSpiceName; // Track Line 1
    String _lastTask;      // Track Line 2
    String _lastDetail;    // Track Line 3
    int _lastBleStatus; // 0, 1, 2
    int _lastProgress;  // Track for incremental drawing
    
    // State tracking for partial redraws
    int _lastSelectedIndex;
    String _lastView; // Track current view type to detect transitions
    
    // Action bar tracking
    String _lastLeft;
    String _lastOk;
    String _lastRight;
};

#endif
