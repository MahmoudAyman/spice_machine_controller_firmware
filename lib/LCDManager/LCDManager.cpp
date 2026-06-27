#include "LCDManager.h"

LCDManager::LCDManager() : 
    _tft(TFT_CS, TFT_DC, TFT_RST),
    _isInitialized(false),
    _lastBleStatus(-1),
    _lastProgress(-1) {}

void LCDManager::begin() {
    Serial.println("[LCD] Initializing ILI9341 (Software SPI)...");
    
    pinMode(TFT_LED, OUTPUT);
    setBacklight(true);

    _tft.begin();
    _tft.setRotation(1); // Landscape (320x240)
    _tft.fillScreen(ILI9341_BLACK);
    
    _isInitialized = true;
    
    // Initial State Reset
    _lastHeaderTitle = "";
    _lastStatus = "";
    _lastSpiceName = "";
    _lastTask = "";
    _lastDetail = "";
    _lastLeft = "__NONE__";
    _lastOk = "__NONE__";
    _lastRight = "__NONE__";
    _lastSelectedIndex = -1;
    _lastView = "";
    _lastProgress = -1;

    updateHeader("Spice Mixer", 1); // Default to Advertising
}

void LCDManager::clear() {
    if (!_isInitialized) return;
    _tft.fillScreen(ILI9341_BLACK);
    _lastProgress = -1;
}

void LCDManager::setBacklight(bool on) {
    digitalWrite(TFT_LED, on ? HIGH : LOW);
}

void LCDManager::updateHeader(String title, int bleStatus) {
    if (!_isInitialized) return;

    if (title != _lastHeaderTitle || bleStatus != _lastBleStatus) {
        // Header bar (320x30)
        _tft.fillRect(0, 0, 320, 30, ILI9341_BLUE);
        
        _tft.setTextColor(ILI9341_WHITE);
        _tft.setTextSize(2);
        _tft.setCursor(10, 7);
        _tft.print(title);

        _drawBLEStatus(bleStatus);

        _lastHeaderTitle = title;
        _lastBleStatus = bleStatus;
    }
}

void LCDManager::_drawBLEStatus(int status) {
    _tft.setTextSize(1);
    uint16_t color = ILI9341_WHITE;
    String txt = "Unknown";

    if (status == 0) { color = ILI9341_RED; txt = "Disconnected"; }
    else if (status == 1) { color = ILI9341_WHITE; txt = "Advertising"; }
    else if (status == 2) { color = ILI9341_GREEN; txt = "BLE Connected"; }

    int16_t x1, y1;
    uint16_t w, h;
    _tft.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    
    _tft.setTextColor(color);
    _tft.setCursor(310 - w, 11);
    _tft.print(txt);
}

void LCDManager::updateStatus(String status, uint16_t color) {
    if (!_isInitialized) return;
    if (status == _lastStatus) return;

    // Status line just above action bar
    _tft.fillRect(0, 180, 320, 20, ILI9341_BLACK);
    _tft.setCursor(10, 185);
    _tft.setTextColor(color);
    _tft.setTextSize(1);
    _tft.print(status);

    _lastStatus = status;
}

void LCDManager::updateContent(String line1, String line2) {
    if (!_isInitialized) return;

    // Clear content area (30 to 180)
    _tft.fillRect(0, 30, 320, 150, ILI9341_BLACK);
    _lastProgress = -1; 
    _lastSpiceName = line1;
    _lastTask = line2;
    _lastDetail = "";
    
    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(3);
    _tft.setCursor(20, 60); 
    _tft.println(line1);
    
    if (line2 != "") {
        _tft.setTextSize(2);
        _tft.setCursor(20, 100);
        _tft.println(line2);
    }
}

void LCDManager::updateSpiceName(String name) {
    if (!_isInitialized || name == _lastSpiceName) return;
    
    // Clear Line 1 area (roughly 50 to 95)
    _tft.fillRect(0, 50, 320, 45, ILI9341_BLACK);
    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(3);
    _tft.setCursor(20, 60);
    _tft.print(name);
    
    _lastSpiceName = name;
}

void LCDManager::updateTask(String task) {
    if (!_isInitialized || task == _lastTask) return;
    
    _tft.fillRect(0, 95, 320, 30, ILI9341_BLACK);
    _tft.setTextColor(ILI9341_CYAN);
    _tft.setTextSize(2);
    _tft.setCursor(20, 100);
    _tft.print(task);
    
    _lastTask = task;
}

void LCDManager::updateDetail(String detail) {
    if (!_isInitialized || detail == _lastDetail) return;
    
    _tft.fillRect(0, 125, 320, 30, ILI9341_BLACK);
    _tft.setTextColor(ILI9341_YELLOW);
    _tft.setTextSize(2);
    _tft.setCursor(20, 130);
    _tft.print(detail);
    
    _lastDetail = detail;
}

void LCDManager::drawProgressBar(int progress, int y, bool forceRedraw) {
    if (!_isInitialized) return;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    int width = 280;
    int height = 20;
    int x = 20;

    if (forceRedraw || _lastProgress == -1 || progress < _lastProgress) {
        _tft.drawRect(x, y, width, height, ILI9341_WHITE);
        _tft.fillRect(x + 2, y + 2, width - 4, height - 4, ILI9341_BLACK);
        if (progress > 0) {
            _tft.fillRect(x + 2, y + 2, (width - 4) * progress / 100, height - 4, ILI9341_GREEN);
        }
    } else if (progress > _lastProgress) {
        int startX = x + 2 + ((width - 4) * _lastProgress / 100);
        int endX = x + 2 + ((width - 4) * progress / 100);
        int drawW = endX - startX;
        if (drawW > 0) {
            _tft.fillRect(startX, y + 2, drawW, height - 4, ILI9341_GREEN);
        }
    }
    _lastProgress = progress;
}

void LCDManager::setActionBar(String left, String ok, String right) {
    _drawActionBar(left, ok, right);
}

void LCDManager::_drawActionBar(String left, String ok, String right) {
    if (left == _lastLeft && ok == _lastOk && right == _lastRight) return;

    // Action Bar area: 320x40 at bottom
    _tft.fillRect(0, 200, 320, 40, 0x2104); // Dark Grey/Charcoal
    _tft.drawFastHLine(0, 200, 320, ILI9341_WHITE);

    _tft.setTextSize(2);
    _tft.setTextColor(ILI9341_WHITE);

    // Left Button
    if (left != "") {
        _tft.setCursor(10, 212);
        _tft.print("< " + left);
    }

    // OK Button (Center)
    if (ok != "") {
        int16_t x1, y1;
        uint16_t w, h;
        _tft.getTextBounds(ok, 0, 0, &x1, &y1, &w, &h);
        _tft.setCursor(160 - (w / 2), 212);
        _tft.print(ok);
    }

    // Right Button
    if (right != "") {
        int16_t x1, y1;
        uint16_t w, h;
        _tft.getTextBounds(right + " >", 0, 0, &x1, &y1, &w, &h);
        _tft.setCursor(310 - w, 212);
        _tft.print(right + " >");
    }

    _lastLeft = left;
    _lastOk = ok;
    _lastRight = right;
}

void LCDManager::showMenu(String title, const char* options[], int count, int selectedIndex, String leftLab, String okLab, String rightLab) {
    String currentViewID = "MENU_" + title;
    bool viewChanged = (_lastView != currentViewID);
    
    updateHeader(title, _lastBleStatus);
    _drawActionBar(leftLab, okLab, rightLab);

    int itemsPerPage = 5;
    int itemHeight = 30;
    int startY = 40;
    
    int currentPage = selectedIndex / itemsPerPage;
    int scrollOffset = currentPage * itemsPerPage;

    static int lastPage = -1;
    if (viewChanged) lastPage = -1;
    
    if (viewChanged || currentPage != lastPage) {
        _tft.fillRect(0, 30, 320, 170, ILI9341_BLACK);
        for (int i = 0; i < itemsPerPage; i++) {
            int optIdx = scrollOffset + i;
            if (optIdx >= count) break;

            int y = startY + (i * itemHeight);
            if (optIdx == selectedIndex) {
                _tft.fillRect(5, y - 5, 310, itemHeight, ILI9341_ORANGE);
                _tft.setTextColor(ILI9341_WHITE);
            } else {
                _tft.setTextColor(ILI9341_LIGHTGREY);
            }
            _tft.setTextSize(2);
            _tft.setCursor(20, y);
            _tft.print(options[optIdx]);
        }
        
        if (count > itemsPerPage) {
            int totalPages = (count + itemsPerPage - 1) / itemsPerPage;
            String pg = "Pg " + String(currentPage + 1) + "/" + String(totalPages);
            _tft.setTextSize(1);
            _tft.setTextColor(ILI9341_LIGHTGREY);
            _tft.setCursor(270, 185);
            _tft.print(pg);
        }
    } else if (selectedIndex != _lastSelectedIndex) {
        int oldIdxInPage = _lastSelectedIndex % itemsPerPage;
        int newIdxInPage = selectedIndex % itemsPerPage;

        int oldY = startY + (oldIdxInPage * itemHeight);
        _tft.fillRect(5, oldY - 5, 310, itemHeight, ILI9341_BLACK);
        _tft.setTextColor(ILI9341_LIGHTGREY);
        _tft.setTextSize(2);
        _tft.setCursor(20, oldY);
        _tft.print(options[_lastSelectedIndex]);

        int newY = startY + (newIdxInPage * itemHeight);
        _tft.fillRect(5, newY - 5, 310, itemHeight, ILI9341_ORANGE);
        _tft.setTextColor(ILI9341_WHITE);
        _tft.setTextSize(2);
        _tft.setCursor(20, newY);
        _tft.print(options[selectedIndex]);
    }

    _lastView = currentViewID;
    _lastSelectedIndex = selectedIndex;
    lastPage = currentPage;
}
void LCDManager::showNumericSelection(String title, String value, String unit, String leftLab, String okLab, String rightLab) {
    String currentViewID = "NUM_" + title;
    bool viewChanged = (_lastView != currentViewID);

    updateHeader(title, _lastBleStatus);
    _drawActionBar(leftLab, okLab, rightLab);

    // Track last value to minimize redraw
    static String lastValue = "";
    if (viewChanged) lastValue = "";

    if (viewChanged) {
        // Initial setup for Numeric View
        _tft.fillRect(0, 30, 320, 170, ILI9341_BLACK);

        // Static Arrows (Up and Down)
        _tft.setTextColor(ILI9341_YELLOW);
        _tft.setTextSize(3);

        // Up Arrow
        _tft.setCursor(153, 40);
        _tft.print("^");

        // Down Arrow
        _tft.setCursor(153, 170);
        _tft.print("v");

        // Static Unit
        _tft.setTextColor(ILI9341_WHITE);
        _tft.setTextSize(2);
        int16_t x1, y1;
        uint16_t w, h;
        _tft.getTextBounds(unit, 0, 0, &x1, &y1, &w, &h);
        _tft.setCursor(160 - (w / 2), 145);
        _tft.print(unit);
    }

    if (viewChanged || value != lastValue) {
        // Surgical update of the Number area
        _tft.fillRect(50, 70, 220, 70, ILI9341_BLACK);
        _tft.setTextColor(ILI9341_WHITE);
        _tft.setTextSize(6);

        int16_t x1, y1;
        uint16_t w, h;
        _tft.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
        _tft.setCursor(160 - (w / 2), 80);
        _tft.print(value);

        lastValue = value;
    }

    _lastView = currentViewID;
}

void LCDManager::showOperationView(String title, String spiceName, int progress, String status, String cancelLab) {
    updateHeader(title, _lastBleStatus);
    _drawActionBar(cancelLab, "", "");

    _tft.fillRect(0, 30, 320, 170, ILI9341_BLACK);

    _tft.setTextColor(ILI9341_CYAN);
    _tft.setTextSize(3);
    int16_t x1, y1;
    uint16_t w, h;
    _tft.getTextBounds(spiceName, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(160 - (w/2), 60);
    _tft.print(spiceName);

    drawProgressBar(progress, 160, true); 

    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(2);
    _tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(160 - (w/2), 120);
    _tft.print(status);
}

void LCDManager::runDiagnostic() {
    Serial.println("[LCD] Running Diagnostic (240x320)...");
    _tft.fillScreen(ILI9341_RED);
    delay(500);
    _tft.fillScreen(ILI9341_GREEN);
    delay(500);
    _tft.fillScreen(ILI9341_BLUE);
    delay(500);
    _tft.fillScreen(ILI9341_BLACK);
    
    updateHeader("ILI9341 DIAGNOSTIC", 2);
    showOperationView("DIAGNOSTIC", "Display Test", 50, "Hardware Verified", "Exit");
}
