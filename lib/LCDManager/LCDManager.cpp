#include "LCDManager.h"

LCDManager::LCDManager() : 
    _tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST),
    _isInitialized(false),
    _lastBleStatus(-1) {}

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
    _lastLeft = "__NONE__";
    _lastOk = "__NONE__";
    _lastRight = "__NONE__";
    _lastSelectedIndex = -1;
    _lastView = "";

    updateHeader("Spice Mixer", 1); // Default to Advertising
}

void LCDManager::clear() {
    if (!_isInitialized) return;
    _tft.fillScreen(ILI9341_BLACK);
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
    
    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(3);
    _tft.setCursor(20, 70);
    _tft.println(line1);
    
    if (line2 != "") {
        _tft.setCursor(20, 120);
        _tft.println(line2);
    }
}

void LCDManager::drawProgressBar(int progress, int y) {
    if (!_isInitialized) return;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    int width = 280;
    int height = 15;
    int x = 20;

    _tft.drawRect(x, y, width, height, ILI9341_WHITE);
    _tft.fillRect(x + 2, y + 2, (width - 4) * progress / 100, height - 4, ILI9341_GREEN);
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

    int maxItems = 5;
    int itemHeight = 30;
    int startY = 40;
    
    int scrollOffset = 0;
    if (selectedIndex >= maxItems) {
        scrollOffset = selectedIndex - (maxItems - 1);
    }

    static int lastScrollOffset = -1;
    
    if (viewChanged || scrollOffset != lastScrollOffset) {
        // Full redraw of the menu area
        _tft.fillRect(0, 30, 320, 170, ILI9341_BLACK);
        for (int i = 0; i < maxItems; i++) {
            int optIdx = i + scrollOffset;
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
    } else if (selectedIndex != _lastSelectedIndex) {
        // Fast Partial Redraw
        int oldIdxInView = _lastSelectedIndex - scrollOffset;
        int newIdxInView = selectedIndex - scrollOffset;

        // Clear old highlight
        if (oldIdxInView >= 0 && oldIdxInView < maxItems) {
            int y = startY + (oldIdxInView * itemHeight);
            _tft.fillRect(5, y - 5, 310, itemHeight, ILI9341_BLACK);
            _tft.setTextColor(ILI9341_LIGHTGREY);
            _tft.setTextSize(2);
            _tft.setCursor(20, y);
            _tft.print(options[_lastSelectedIndex]);
        }

        // Draw new highlight
        if (newIdxInView >= 0 && newIdxInView < maxItems) {
            int y = startY + (newIdxInView * itemHeight);
            _tft.fillRect(5, y - 5, 310, itemHeight, ILI9341_ORANGE);
            _tft.setTextColor(ILI9341_WHITE);
            _tft.setTextSize(2);
            _tft.setCursor(20, y);
            _tft.print(options[selectedIndex]);
        }
    }

    _lastView = currentViewID;
    _lastSelectedIndex = selectedIndex;
    lastScrollOffset = scrollOffset;
}

void LCDManager::showNumericSelection(String title, String value, String unit, String leftLab, String okLab, String rightLab) {
    updateHeader(title, _lastBleStatus);
    _drawActionBar(leftLab, okLab, rightLab);

    _tft.fillRect(0, 30, 320, 170, ILI9341_BLACK);

    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(6);
    
    int16_t x1, y1;
    uint16_t w, h;
    _tft.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(160 - (w / 2), 80);
    _tft.print(value);

    _tft.setTextSize(2);
    _tft.getTextBounds(unit, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(160 - (w / 2), 140);
    _tft.print(unit);

    // Draw arrows
    _tft.setTextColor(ILI9341_YELLOW);
    _tft.setTextSize(3);
    _tft.setCursor(30, 80);
    _tft.print("<");
    _tft.setCursor(270, 80);
    _tft.print(">");
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

    drawProgressBar(progress, 110);

    _tft.setTextColor(ILI9341_WHITE);
    _tft.setTextSize(2);
    _tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(160 - (w/2), 150);
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
