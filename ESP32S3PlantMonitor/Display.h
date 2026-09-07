#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "Sensors.h"


enum class PlantState {
    HEALTHY,
    MODERATE_STRESSED,
    HIGH_STRESSED
};

// Screens that temporarily take over from the face.
enum class DisplayScreen {
    FACE,
    CONNECTING,
    SCANNING,
    SENSORS,
    PUMPING,
    NONE,
    BOOT,
    TANK_EMPTY
};

class Display {
public:
    Display();

    bool begin();

    // Default screen
    void setPlantState(PlantState state);
    PlantState getPlantState() const;

    // Redraw
    void update();

    // Same as update(), but keeps redrawing for durationMs
    void updateFor(unsigned long durationMs);

    // Takeover screens. 
    // CONNECTING and PUMPING stay up until you call
    // clearOverride(); SENSORS clears itself after holdMs.
    void showConnecting();
    void showScanning();
    void showPumping(int duration);
    void showBoot(const char* status, bool success, bool appendStatus = true);
    void showSensors(
        const SensorData& data,
        float effectiveLux,
        unsigned long ambientAt,
        unsigned long holdMs = 4000
    );
    void showTankEmptyWarning(unsigned long holdMs = 4000);
    void clearOverride();
    void setSystemStatus(bool lampOn, unsigned long lastSyncMs);
private:
    Adafruit_ILI9341 tft;

    PlantState plantState;

    DisplayScreen activeScreen;
    unsigned long overrideUntil;   // 0 means "until clearOverride()"

    SensorData lastData;
    float lastEffectiveLux;

    // millis() at the ambient reading in lastData
    unsigned long lastAmbientAt;
    int pumpDuration;
    int lastAnimationFrame;
    DisplayScreen lastRenderedScreen;
    const char* bootStatus;
    bool bootSuccess;
    bool hudLampOn = false;
    unsigned long hudLastSync = 0;

    String logLines[6];
    void addLogLine(const String& line);
    void drawTerminalScreen(const char* title);
    void drawCentered(const char* text, int y);

    void drawFace();
    void drawAnimation(
        const uint8_t (*frames)[1024],
        int frameCount,
        unsigned long intervalMs
    );
    void drawPumpingScreen(int duration);
    void drawSensorsScreen(const SensorData& data, float effectiveLux);
    void drawTankEmptyScreen();
    void drawHUD(); 
    void render();
    bool overrideActive();
};

#endif
