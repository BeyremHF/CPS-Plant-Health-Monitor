#include "Display.h"
#include "Config.h"
#include "AnimationConnect.h"
#include "AnimationHappy.h"
#include "AnimationModerate.h"
#include "AnimationSensing.h"
#include "AnimationStress.h"

// Frame counts come straight from the headers, so regenerating an animation
// with a different length cannot desync this.
static const int HAPPY_FRAMES =
    sizeof(Happy_frames) / sizeof(Happy_frames[0]);

static const int MODERATE_FRAMES =
    sizeof(Moderate_frames) / sizeof(Moderate_frames[0]);

static const int STRESS_FRAMES =
    sizeof(Stress_frames) / sizeof(Stress_frames[0]);

// Startup and WiFi share one bar; sensing has its own.
static const int CONNECT_FRAMES =
    sizeof(Connect_frames) / sizeof(Connect_frames[0]);

static const int SENSING_FRAMES =
    sizeof(Sensing_frames) / sizeof(Sensing_frames[0]);

// Per-animation playback speed, matching the generated code.
static const unsigned long HAPPY_FRAME_MS    = 200;
static const unsigned long MODERATE_FRAME_MS = 500;
static const unsigned long STRESS_FRAME_MS   = 250;
static const unsigned long CONNECT_FRAME_MS  = 200;
static const unsigned long SENSING_FRAME_MS  = 200;

Display::Display()
    : u8g2(
        U8G2_R0,
        U8X8_PIN_NONE
      ),
      plantState(PlantState::HEALTHY),
      activeScreen(DisplayScreen::FACE),
      overrideUntil(0),
      lastEffectiveLux(0),
      pumpDuration(0)
{
    lastData = SensorData();
}

bool Display::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    u8g2.begin();
    Serial.println("OLED initialized.");

    // Startup shows the same bar as WiFi. Latched, so it stays up through
    // sensor init until the sketch clears it.
    activeScreen = DisplayScreen::CONNECTING;
    overrideUntil = 0;
    render();

    return true;
}

void Display::drawCentered(
    const char* text,
    int y
) {
    int width = u8g2.getStrWidth(text);

    int x = (128 - width) / 2;

    u8g2.drawStr(x, y, text);
}

// State
void Display::setPlantState(PlantState state) {
    plantState = state;
}

PlantState Display::getPlantState() const {
    return plantState;
}

// Override handling
bool Display::overrideActive() {

    if (activeScreen == DisplayScreen::FACE) {
        return false;
    }

    // overrideUntil == 0 means the screen stays until it is cleared by hand.
    if (overrideUntil == 0) {
        return true;
    }

    if (millis() >= overrideUntil) {
        clearOverride();
        return false;
    }

    return true;
}

void Display::clearOverride() {
    activeScreen = DisplayScreen::FACE;
    overrideUntil = 0;
}

// Screens
void Display::drawAnimation(
    const uint8_t (*frames)[1024],
    int frameCount,
    unsigned long intervalMs
) {
    u8g2.setBitmapMode(1);

    // Frame index comes from millis(), so the animation plays itself with no
    // state to keep in sync -- the more often this is drawn, the smoother it
    // looks.
    int frame =
        (millis() / intervalMs) % frameCount;

    // The frames are XBM (least significant bit first), so drawXBMP is the
    // matching call.
    u8g2.drawXBMP(
        0,
        0,
        128,
        64,
        frames[frame]
    );
}

void Display::drawFace() {

    if (plantState == PlantState::HEALTHY) {
        drawAnimation(
            Happy_frames,
            HAPPY_FRAMES,
            HAPPY_FRAME_MS
        );
        return;
    }

    if (plantState == PlantState::MODERATE_STRESSED) {
        drawAnimation(
            Moderate_frames,
            MODERATE_FRAMES,
            MODERATE_FRAME_MS
        );
        return;
    }

    drawAnimation(
        Stress_frames,
        STRESS_FRAMES,
        STRESS_FRAME_MS
    );
}

void Display::drawPumpingScreen(int duration) {

    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);

    u8g2.setFont(u8g2_font_6x10_tr);
    drawCentered("WATERING", 15);

    u8g2.setFont(u8g2_font_10x20_tr);
    drawCentered("PUMP ON", 42);

    u8g2.setFont(u8g2_font_5x7_tr);

    char buffer[32];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d seconds",
        duration
    );

    drawCentered(buffer, 58);
}

// Five rows on a 64 px screen. The 5x7 font is 7 px tall, so 9 px spacing
// leaves a pixel of air between rows and still finishes at y=56, clear of the
// bottom edge at 63. The labels are all padded to ten characters so the values
// line up in one column.
void Display::drawSensorsScreen(const SensorData& data, float effectiveLux) {

    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);

    u8g2.setFont(u8g2_font_6x10_tr);
    drawCentered("SENSORS", 10);

    char buffer[32];

    u8g2.setFont(u8g2_font_5x7_tr);

    snprintf(
        buffer,
        sizeof(buffer),
        "Temp:     %.1f C",
        data.temperature
    );
    u8g2.drawStr(5, 20, buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Humidity: %.0f %%",
        data.humidity
    );
    u8g2.drawStr(5, 29, buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Soil:     %.0f %%",
        data.soilMoisture
    );
    u8g2.drawStr(5, 38, buffer);

    // The room on its own -- the lamp is switched off while this is measured.
    snprintf(
        buffer,
        sizeof(buffer),
        "Ambient:  %.0f lx",
        data.light
    );
    u8g2.drawStr(5, 47, buffer);

    // What the plant is actually receiving, lamp included.
    snprintf(
        buffer,
        sizeof(buffer),
        "At plant: %.0f lx",
        effectiveLux
    );
    u8g2.drawStr(5, 56, buffer);
}

// Takeover screens
void Display::showConnecting() {
    activeScreen = DisplayScreen::CONNECTING;
    overrideUntil = 0;
    render();
}

void Display::showScanning() {
    activeScreen = DisplayScreen::SCANNING;
    overrideUntil = 0;
    render();
}

void Display::showPumping(int duration) {
    activeScreen = DisplayScreen::PUMPING;
    overrideUntil = 0;
    pumpDuration = duration;
    render();
}

void Display::showSensors(
    const SensorData& data,
    float effectiveLux,
    unsigned long holdMs
) {
    activeScreen = DisplayScreen::SENSORS;
    overrideUntil = millis() + holdMs;
    lastData = data;
    lastEffectiveLux = effectiveLux;
    render();
}

// Drawing
void Display::render() {

    u8g2.clearBuffer();

    switch (activeScreen) {

        // Startup uses this one too -- anything that is not sensing.
        case DisplayScreen::CONNECTING:
            drawAnimation(
                Connect_frames,
                CONNECT_FRAMES,
                CONNECT_FRAME_MS
            );
            break;

        case DisplayScreen::SCANNING:
            drawAnimation(
                Sensing_frames,
                SENSING_FRAMES,
                SENSING_FRAME_MS
            );
            break;

        case DisplayScreen::PUMPING:
            drawPumpingScreen(pumpDuration);
            break;

        case DisplayScreen::SENSORS:
            drawSensorsScreen(lastData, lastEffectiveLux);
            break;

        case DisplayScreen::FACE:
        default:
            drawFace();
            break;
    }

    u8g2.sendBuffer();
}

void Display::update() {
    // Lets an expired SENSORS screen fall back to the face.
    overrideActive();
    render();
}

void Display::updateFor(unsigned long durationMs) {

    unsigned long start = millis();

    while (millis() - start < durationMs) {
        update();
        delay(20);
    }
}
