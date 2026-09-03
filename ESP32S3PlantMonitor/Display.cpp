#include "Display.h"
#include "Config.h"
#include "AnimationConnect.h"
#include "AnimationHappy.h"
#include "AnimationModerate.h"
#include "AnimationSensing.h"
#include "AnimationStress.h"
#include <WiFi.h>
#include <time.h>
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
    : tft(
        TFT_CS,
        TFT_DC,
        TFT_RST
      ),
      plantState(PlantState::HEALTHY),
      activeScreen(DisplayScreen::BOOT),
      overrideUntil(0),
      lastEffectiveLux(0),
      lastAmbientAt(0),
      pumpDuration(0),
      lastAnimationFrame(-1),
      lastRenderedScreen(DisplayScreen::NONE),
      bootStatus("Starting system"),
      bootSuccess(false)
{
    lastData = SensorData();
}
bool Display::begin() {

    // Start hardware SPI
    SPI.begin(
        TFT_SCK,
        TFT_MISO,
        TFT_MOSI,
        TFT_CS
    );

    // Initialize display
    tft.begin();
    tft.setRotation(1);

    // Render connecting screen immediately
    render();

    Serial.println("ILI9341 initialized.");

    return true;
}

void Display::drawCentered(
    const char* text,
    int y
) {
    int16_t x1, y1;
    uint16_t w, h;

    tft.getTextBounds(
        text,
        0,
        y,
        &x1,
        &y1,
        &w,
        &h
    );

    int x = (tft.width() - w) / 2;

    tft.setCursor(x, y);
    tft.print(text);
}

// State
void Display::setPlantState(PlantState state) {

    if (plantState != state) {
        plantState = state;

        // Force the first frame of the new animation.
        lastAnimationFrame = -1;
    }
}

PlantState Display::getPlantState() const {
    return plantState;
}



void Display::clearOverride() {
    activeScreen = DisplayScreen::FACE;
    overrideUntil = 0;
    lastAnimationFrame = -1;
    render();
}

void Display::drawAnimation(
    const uint8_t (*frames)[1024],
    int frameCount,
    unsigned long intervalMs
) {
    if (frameCount <= 0 || intervalMs == 0) {
        return;
    }

    int frame = (millis() / intervalMs) % frameCount;

    if (frame == lastAnimationFrame) {
        return;
    }

    lastAnimationFrame = frame;

    const int animationWidth = 128;
    const int animationHeight = 64;

    const int xOffset =
        (tft.width() - animationWidth) / 2;

    const int yOffset =
        (tft.height() - animationHeight) / 2;

    // Clear only the animation area
    tft.fillRect(
        xOffset,
        yOffset,
        animationWidth,
        animationHeight,
        ILI9341_WHITE
    );

    // Draw animation frame
    tft.drawXBitmap(
        xOffset,
        yOffset,
        frames[frame],
        animationWidth,
        animationHeight,
        ILI9341_BLACK
    );
}
void Display::drawFace() {
    if (lastAnimationFrame == -1) {
            drawHUD();
        }
    switch (plantState) {

        case PlantState::HEALTHY:
            drawAnimation(
                Happy_frames,
                HAPPY_FRAMES,
                HAPPY_FRAME_MS
            );
            break;

        case PlantState::MODERATE_STRESSED:
            drawAnimation(
                Moderate_frames,
                MODERATE_FRAMES,
                MODERATE_FRAME_MS
            );
            break;

        case PlantState::HIGH_STRESSED:
            drawAnimation(
                Stress_frames,
                STRESS_FRAMES,
                STRESS_FRAME_MS
            );
            break;
    }
}
void Display::drawPumpingScreen(int duration) {
    int totalFrames = 30;
    // Advance the animation frame roughly every 40ms (~25 fps)
    int frame = (millis() / 40) % totalFrames; 

    // Skip drawing if we are still on the same frame
    if (frame == lastAnimationFrame) {
        return;
    }

    // Draw static text ONLY on the very first frame to prevent flickering
    if (lastAnimationFrame == -1) {
        tft.setTextColor(ILI9341_BLUE);
        tft.setTextSize(3);
        drawCentered("WATERING", 30);
        
        tft.setTextColor(ILI9341_BLACK);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "PUMPING: %d SEC", duration);
        tft.setTextSize(2);
        drawCentered(buffer, 200);
    }

    lastAnimationFrame = frame;

    // Clear ONLY the center area where the water falls to keep it buttery smooth
    tft.fillRect(0, 70, tft.width(), 120, ILI9341_WHITE);

    // Draw 3 staggered cascading water drops
    for (int i = 0; i < 3; i++) {
        // Offset the math so the drops don't fall in a perfectly straight horizontal line
        int offset = i * (totalFrames / 3);
        int currentFrame = (frame + offset) % totalFrames;
        
        // Spread the 3 drops evenly across the center
        int dropX = (tft.width() / 2) - 50 + (i * 50);
        // Move downwards 4 pixels per frame
        int dropY = 70 + (currentFrame * 4); 
        
        int r = 8;
        // Water drop shape: blue triangle stacked on a blue circle
        tft.fillTriangle(
            dropX - r, dropY, 
            dropX + r, dropY, 
            dropX, dropY - r * 2, 
            ILI9341_BLUE
        );
        tft.fillCircle(dropX, dropY, r, ILI9341_BLUE);
    }
}

// How long ago a reading was taken, short enough to be at the end of a row: "8s", "4m", "2h"
static void formatAge(unsigned long sinceMs, char* out, size_t size) {

    if (sinceMs == 0) {
        snprintf(out, size, "--");
        return;
    }

    unsigned long seconds = (millis() - sinceMs) / 1000UL;

    if (seconds < 60) {
        snprintf(out, size, "%lus", seconds);
        return;
    }

    if (seconds < 3600) {
        snprintf(out, size, "%lum", seconds / 60UL);
        return;
    }

    snprintf(out, size, "%luh", seconds / 3600UL);
}


void Display::drawSensorsScreen(
    const SensorData& data,
    float effectiveLux
) {
    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(3);
    drawCentered("SENSOR DATA", 15);

    tft.setTextSize(2);

    int y = 60;
    int spacing = 35;
    int leftCol = 10;
    int rightCol = 110;
    int barW = 190;
    char buffer[32];

    // 1. Temperature (Green if healthy, else Red)
    uint16_t tempColor = (data.temperature >= TEMP_MIN && data.temperature <= TEMP_MAX) ? ILI9341_DARKGREEN : ILI9341_RED;
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(leftCol, y); tft.print("Temp:");
    tft.setTextColor(tempColor);
    snprintf(buffer, sizeof(buffer), "%.1f C", data.temperature);
    tft.setCursor(rightCol, y); tft.print(buffer);
    
    int tempFill = constrain((data.temperature / 45.0) * barW, 0, barW);
    tft.drawRect(rightCol, y + 18, barW, 6, ILI9341_BLACK);
    tft.fillRect(rightCol, y + 18, tempFill, 6, tempColor);
    y += spacing;

    // 2. Humidity (Blue if humid, Orange if dry)
    uint16_t humColor =
    (data.humidity >= 40.0)
        ? ILI9341_DARKGREEN
        : ILI9341_RED;
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(leftCol, y); tft.print("Hum:");
    tft.setTextColor(humColor);
    snprintf(buffer, sizeof(buffer), "%.0f %%", data.humidity);
    tft.setCursor(rightCol, y); tft.print(buffer);
    
    int humFill = constrain((data.humidity / 100.0) * barW, 0, barW);
    tft.drawRect(rightCol, y + 18, barW, 6, ILI9341_BLACK);
    tft.fillRect(rightCol, y + 18, humFill, 6, humColor);
    y += spacing;

    // 3. Soil Moisture 
    uint16_t soilColor =
        (data.soilMoisture >= WATERING_THRESHOLD)
            ? ILI9341_DARKGREEN
            : ILI9341_RED;
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(leftCol, y); tft.print("Soil:");
    tft.setTextColor(soilColor);
    snprintf(buffer, sizeof(buffer), "%.0f %%", data.soilMoisture);
    tft.setCursor(rightCol, y); tft.print(buffer);
    
    int soilFill = constrain((data.soilMoisture / 100.0) * barW, 0, barW);
    tft.drawRect(rightCol, y + 18, barW, 6, ILI9341_BLACK);
    tft.fillRect(rightCol, y + 18, soilFill, 6, soilColor);
    y += spacing;

    // 4. Light 
    uint16_t lightColor =
    (effectiveLux >= LIGHT_MIN)
        ? ILI9341_DARKGREEN
        : ILI9341_RED;
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(leftCol, y); tft.print("Light:");
    tft.setTextColor(lightColor);
    snprintf(buffer, sizeof(buffer), "%.0f lx", effectiveLux);
    tft.setCursor(rightCol, y); tft.print(buffer);
    
    // Assuming 2500 lux as a rough maximum for the bar chart scaling
    int lightFill = constrain((effectiveLux / 2500.0) * barW, 0, barW);
    tft.drawRect(rightCol, y + 18, barW, 6, ILI9341_BLACK);
    tft.fillRect(rightCol, y + 18, lightFill, 6, lightColor);

    // 5. Water Tank Warning Overlay (Overrides bottom section if empty)
    if (data.waterTankEmpty) {
        tft.fillRect(0, 205, tft.width(), 35, ILI9341_RED);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        drawCentered("! TANK EMPTY !", 214);
    }
    else {
        y += spacing; 
    
        tft.setTextColor(ILI9341_DARKGREY);
        tft.setCursor(leftCol, y); 
        tft.print("Press:");
        tft.setTextColor(ILI9341_DARKCYAN); 
        snprintf(buffer, sizeof(buffer), "%.1f hPa", data.pressure);
        tft.setCursor(rightCol, y); 
        tft.print(buffer);
    }
}
// Takeover screens
void Display::showTankEmptyWarning(unsigned long holdMs) {
    activeScreen = DisplayScreen::TANK_EMPTY;
    overrideUntil = millis() + holdMs;
    render();
}
void Display::showBoot(const char* status, bool success, bool appendStatus) {
    activeScreen = DisplayScreen::BOOT;
    overrideUntil = 0;
    
    String line = String(status);
    
    // Only append status tags if requested
    if (appendStatus) {
        if (success && line.indexOf("IP:") == -1) {
            line += " [OK]";
        } else if (!success) {
            line += " [FAILED]";
        }
    }
    
    addLogLine(line);
    render();
}

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
    unsigned long ambientAt,
    unsigned long holdMs
) {
    activeScreen = DisplayScreen::SENSORS;
    overrideUntil = millis() + holdMs;
    lastData = data;
    lastEffectiveLux = effectiveLux;
    lastAmbientAt = ambientAt;
    render();
}

void Display::render() {
    bool screenChanged =
        activeScreen != lastRenderedScreen;
    if (screenChanged) {

        // Clear the screen ONLY when changing screens.
        tft.fillScreen(ILI9341_WHITE);

        // Force the animation to render its first frame.
        lastAnimationFrame = -1;

        lastRenderedScreen = activeScreen;
    }
    switch (activeScreen) {

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
            drawSensorsScreen(
                lastData,
                lastEffectiveLux
            );
            break;
        case DisplayScreen::BOOT:
                    // Replaced the static screen with the dynamic terminal
                    drawTerminalScreen("SYSTEM BOOT");
                    break;
        case DisplayScreen::TANK_EMPTY:
                drawTankEmptyScreen();
                break;
        case DisplayScreen::FACE:
        default:
            drawFace();
            break;
    }
}

void Display::update() {

    if (activeScreen != DisplayScreen::FACE &&
        overrideUntil != 0 &&
        millis() >= overrideUntil) {

        clearOverride();
        return;
    }

    render();
}

void Display::updateFor(unsigned long durationMs) {

    unsigned long start = millis();

    while (millis() - start < durationMs) {
        update();
        delay(10);
    }
}


void Display::addLogLine(const String& line) {
    // Shift all lines up by one
    for (int i = 0; i < 5; i++) {
        logLines[i] = logLines[i + 1];
    }
    // Add the new line at the bottom
    logLines[5] = "> " + line;
}

void Display::drawTerminalScreen(const char* title) {
    tft.fillScreen(ILI9341_WHITE); 
    
    // Header Bar
    tft.fillRect(0, 0, tft.width(), 30, ILI9341_DARKGREY);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    drawCentered(title, 7);
    
    // Print Log Lines
    tft.setTextSize(2);
    int y = 45;
    
    for (int i = 0; i < 6; i++) {
        if (logLines[i].length() > 0) {
            String line = logLines[i];
            int targetIdx = -1;
            uint16_t highlightColor = ILI9341_BLACK;
            String highlightWord = "";

            // Identify if the line contains any status keywords
            if (line.indexOf("FAILED") >= 0) {
                targetIdx = line.indexOf("FAILED");
                highlightColor = ILI9341_RED;
                highlightWord = "FAILED";
            } else if (line.indexOf("not found") >= 0) {
                targetIdx = line.indexOf("not found");
                highlightColor = ILI9341_RED;
                highlightWord = "not found";
            } else if (line.indexOf("OK") >= 0) {
                targetIdx = line.indexOf("OK");
                highlightColor = ILI9341_GREEN;
                highlightWord = "OK";
            } else if (line.indexOf("ready") >= 0) {
                targetIdx = line.indexOf("ready");
                highlightColor = ILI9341_GREEN;
                highlightWord = "ready";
            }

            tft.setCursor(10, y);
            
            if (targetIdx >= 0) {
                // 1. Print text before the keyword in BLACK
                tft.setTextColor(ILI9341_BLACK);
                tft.print(line.substring(0, targetIdx));
                
                // 2. Print the keyword in COLOR
                tft.setTextColor(highlightColor);
                tft.print(highlightWord);
                
                // 3. Print any remaining text after the keyword in BLACK
                tft.setTextColor(ILI9341_BLACK);
                tft.print(line.substring(targetIdx + highlightWord.length()));
            } else {
                // Print the entire line in black if no keyword is found
                tft.setTextColor(ILI9341_BLACK);
                tft.print(line);
            }
            
            y += 30;
        }
    }
}
void Display::setSystemStatus(bool lampOn, unsigned long lastSyncMs) {
    bool changed = (hudLampOn != lampOn);
    hudLampOn = lampOn;
    
    if (changed && activeScreen == DisplayScreen::FACE) {
        lastAnimationFrame = -1; 
    }
}
void Display::drawHUD() {
    // 1. Draw Top Bar (Dark Grey)
    tft.fillRect(0, 0, tft.width(), 30, ILI9341_DARKGREY);
    tft.setTextSize(2);
    
    // Top Left: WiFi Status
    tft.setCursor(5, 7);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(ILI9341_GREEN);
        tft.print("WiFi: OK");
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.print("WiFi: DC");
    }

    // Top Right: Temp | Air Hum | Soil Hum
    char tempStr[10], humStr[10], soilStr[10], totalBuf[40];
    snprintf(tempStr, sizeof(tempStr), "%.1fC", lastData.temperature);
    snprintf(humStr, sizeof(humStr), "%.0f%%", lastData.humidity);
    snprintf(soilStr, sizeof(soilStr), "%.0f%%", lastData.soilMoisture);
    
    // Create a combined string just to calculate the total width for right-alignment
    snprintf(totalBuf, sizeof(totalBuf), "%s | %s | %s", tempStr, humStr, soilStr);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(totalBuf, 0, 7, &x1, &y1, &w, &h);
    
    tft.setCursor(tft.width() - w - 5, 7);

    // 1. Print Temperature (Green if healthy, else Red)
    if (lastData.temperature >= TEMP_MIN && lastData.temperature <= TEMP_MAX) {
        tft.setTextColor(ILI9341_GREEN);
    } else {
        tft.setTextColor(ILI9341_RED);
    }
    tft.print(tempStr);
    
    tft.setTextColor(ILI9341_WHITE);
    tft.print(" | ");

    // 2. Print Air Humidity (Green if >= 40%, else Red)
    if (lastData.humidity >= 40.0) {
        tft.setTextColor(ILI9341_GREEN);
    } else {
        tft.setTextColor(ILI9341_RED);
    }
    tft.print(humStr);
    
    tft.setTextColor(ILI9341_WHITE);
    tft.print(" | ");

    // 3. Print Soil Moisture (Green if above threshold, else Red)
    if (lastData.soilMoisture >= WATERING_THRESHOLD) {
        tft.setTextColor(ILI9341_GREEN);
    } else {
        tft.setTextColor(ILI9341_RED);
    }
    tft.print(soilStr);

    // 2. Draw Bottom Bar (Light Grey)
    tft.fillRect(0, tft.height() - 35, tft.width(), 35, ILI9341_LIGHTGREY);
    
    // Bottom Left: Tank / Health
    tft.setCursor(5, tft.height() - 25);
    if (lastData.waterTankEmpty) {
        tft.setTextColor(ILI9341_RED);
        tft.print("! TANK EMPTY !");
    } else {
        tft.setTextColor(ILI9341_BLACK);
        tft.print("Health: ");
        switch(plantState) {
            case PlantState::HEALTHY:
                tft.setTextColor(ILI9341_DARKGREEN); tft.print("OK"); break;
            case PlantState::MODERATE_STRESSED:
                tft.setTextColor(ILI9341_ORANGE); tft.print("WARN"); break;
            case PlantState::HIGH_STRESSED:
                tft.setTextColor(ILI9341_RED); tft.print("CRIT"); break;
        }
    }

    // Bottom Right: Lamp Status
    char botBuf[16];
    snprintf(botBuf, sizeof(botBuf), "Lamp: %s", hudLampOn ? "ON" : "OFF");
    
    tft.getTextBounds(botBuf, 0, tft.height() - 25, &x1, &y1, &w, &h);
    tft.setCursor(tft.width() - w - 5, tft.height() - 25);
    tft.setTextColor(ILI9341_BLACK);
    tft.print(botBuf);
}

void Display::drawTankEmptyScreen() {
    // Only draw the static text on the first frame to prevent flickering
    if (lastAnimationFrame == -1) {
        tft.fillScreen(ILI9341_RED);
        tft.setTextColor(ILI9341_WHITE);
        
        tft.setTextSize(3);
        drawCentered("WARNING", 50);
        
        tft.setTextSize(2);
        drawCentered("PUMP BLOCKED", 120);
        drawCentered("WATER TANK EMPTY", 150);
    }
    
    lastAnimationFrame = 1; // Mark as drawn
}