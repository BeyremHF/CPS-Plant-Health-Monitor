#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "Sensors.h"

// How the plant is doing. This is the "default" the screen falls back to
// whenever nothing more urgent needs showing.
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
    PUMPING
};

class Display {
public:
    Display();

    bool begin();

    // Default screen
    void setPlantState(PlantState state);
    PlantState getPlantState() const;

    // Redraw whatever should currently be on screen. Safe to call as often
    // as you like -- the face animates off millis(), so the more often this
    // runs the smoother it looks.
    void update();

    // Same as update(), but keeps redrawing for durationMs. Use it in place
    // of delay() so waiting time animates instead of freezing one frame.
    void updateFor(unsigned long durationMs);

    // Takeover screens. CONNECTING and PUMPING stay up until you call
    // clearOverride(); SENSORS clears itself after holdMs.
    void showConnecting();
    void showScanning();
    void showPumping(int duration);

    void showSensors(
        const SensorData& data,
        float effectiveLux,
        unsigned long holdMs = 4000
    );
    void clearOverride();

private:
    U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

    PlantState plantState;

    DisplayScreen activeScreen;
    unsigned long overrideUntil;   // 0 means "until clearOverride()"

    SensorData lastData;
    float lastEffectiveLux;
    int pumpDuration;

    void drawCentered(const char* text, int y);

    void drawFace();
    // Plays any lopaka-generated 128x64 XBM frame set, stepping every
    // intervalMs. Works for both faces because it takes the frame table.
    void drawAnimation(
        const uint8_t (*frames)[1024],
        int frameCount,
        unsigned long intervalMs
    );
    void drawPumpingScreen(int duration);
    void drawSensorsScreen(const SensorData& data, float effectiveLux);

    void render();
    bool overrideActive();
};

#endif
