#include "LCDManager.h"

LCDManager::LCDManager() : 
    _tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST),
    _isInitialized(false),
    _lastBleState(false) {}

void LCDManager::begin() {
    Serial.println("[LCD] Initializing ILI9341 (Software SPI)...");
    
    pinMode(TFT_LED, OUTPUT);
    setBacklight(true);

    _tft.begin();
    _tft.setRotation(1); // Landscape (320x240)
    _tft.fillScreen(ILI9341_BLACK);
    
    _isInitialized = true;
    updateHeader("Spice Mixer", false);
}

void LCDManager::clear() {
    if (!_isInitialized) return;
    _tft.fillScreen(ILI9341_BLACK);
}

void LCDManager::setBacklight(bool on) {
    digitalWrite(TFT_LED, on ? HIGH : LOW);
}

void LCDManager::updateHeader(String title, bool bleConnected) {
    if (!_isInitialized) return;

    if (title != _lastHeaderTitle || bleConnected != _lastBleState) {
        // Clear header area (320x30)
        _tft.fillRect(0, 0, 320, 30, ILI9341_BLUE);
        
        _tft.setTextColor(ILI9341_WHITE);
        _tft.setTextSize(2);
        _tft.setCursor(10, 8);
        _tft.print(title);

        _drawBLEIcon(bleConnected);

        _lastHeaderTitle = title;
        _lastBleState = bleConnected;
    }
}

void LCDManager::updateStatus(String status, uint16_t color) {
    if (!_isInitialized) return;
    if (status == _lastStatus) return;

    // Clear status area (bottom bar)
    _tft.fillRect(0, 210, 320, 30, ILI9341_BLACK);
    _tft.setCursor(10, 215);
    _tft.setTextColor(color);
    _tft.setTextSize(1);
    _tft.print(status);

    _lastStatus = status;
}

void LCDManager::updateContent(String line1, String line2) {
    if (!_isInitialized) return;

    // Clear content area
    _tft.fillRect(0, 35, 320, 170, ILI9341_BLACK);
    
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
    int height = 20;
    int x = 20;

    _tft.drawRect(x, y, width, height, ILI9341_WHITE);
    _tft.fillRect(x + 2, y + 2, (width - 4) * progress / 100, height - 4, ILI9341_GREEN);
}

void LCDManager::_drawBLEIcon(bool connected) {
    uint16_t color = connected ? ILI9341_CYAN : 0x7BEF; // Dark Grey
    // Simple Diamond shape for BLE (scaled for 320x240)
    _tft.drawTriangle(290, 15, 300, 5, 310, 15, color);
    _tft.drawTriangle(290, 15, 300, 25, 310, 15, color);
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
    
    updateHeader("ILI9341 DIAGNOSTIC", true);
    updateContent("2.4\" TFT Test", "320x240 Res");
    updateStatus("Hardware Verified", ILI9341_GREEN);
    drawProgressBar(50);
}
