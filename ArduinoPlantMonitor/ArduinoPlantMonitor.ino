#include <Arduino.h>

#include "Config.h"
#include "Led.h"
#include "Sensors.h"
#include "Firebase.h"
#include "Display.h"

unsigned long lastSensorSend = 0;
Display display;

// What Firebase said about the pump on the most recent poll. Kept so the
// serial log can report the moment it flips instead of every poll.
bool lastPumpTrigger = false;

// Who last asked for water, so the log can say what is actually in charge.
enum class WaterSource { NONE, REMOTE, FALLBACK };

WaterSource lastWaterSource = WaterSource::NONE;
unsigned long lastWaterMs = 0;

// When the soil first dropped below WATERING_THRESHOLD and stayed there.
// 0 means "not currently dry". This is the clock the fallback runs on.
unsigned long dryStartMs = 0;

// When the board last saw someone else set the pump flag. Proof the backend
// is alive; 0 means it has not been heard from since boot.
unsigned long lastRemoteMs = 0;

// False until this session has actually measured the soil.
bool haveReading = false;

// Works out which face to show from the latest reading.
//
// Counts how many of the plant's needs are currently unmet. Exposed so the
// serial report can show which ones, rather than just the verdict.
//
// Water and temperature only
int countStressFactors(const SensorData& data) {

    int factors = 0;

    if (data.soilMoisture < MOISTURE_HEALTHY_MIN) {
        factors++;
    }

    if (data.temperature < TEMP_MIN || data.temperature > TEMP_MAX) {
        factors++;
    }

    return factors;
}


PlantState evaluatePlantState(const SensorData& data) {

    // Severe dryness outranks everything else. A plant this dry is in trouble
    // regardless of how good the light and temperature are.
    if (data.soilMoisture < MOISTURE_MODERATE_MIN) {
        return PlantState::HIGH_STRESSED;
    }

    int factors = countStressFactors(data);

    if (factors == 0) {
        return PlantState::HEALTHY;
    }

    if (factors == 1) {
        return PlantState::MODERATE_STRESSED;
    }

    return PlantState::HIGH_STRESSED;
}


// Machine-readable form, published to Firebase so the web dashboard can render the same
// This is kept separate from plantStateName() because that one is for humans to read the
// serial log and that may be reworded
const char* plantStateSlug(PlantState state) {

    if (state == PlantState::HEALTHY) {
        return "healthy";
    }

    if (state == PlantState::MODERATE_STRESSED) {
        return "moderate_stress";
    }

    return "high_stress";
}


const char* plantStateName(PlantState state) {

    if (state == PlantState::HEALTHY) {
        return "HEALTHY";
    }

    if (state == PlantState::MODERATE_STRESSED) {
        return "MODERATE STRESS";
    }

    return "HIGH STRESS";
}


// millis() since a recorded moment, in whole seconds
unsigned long secondsSince(unsigned long since) {
    return (millis() - since) / 1000UL;
}


const char* waterSourceName(WaterSource source) {

    if (source == WaterSource::REMOTE) {
        return "backend";
    }

    if (source == WaterSource::FALLBACK) {
        return "board fallback";
    }

    return "nothing yet";
}


// Runs the pump and blocks for the duration. Both the remote path and the
// fallback path go through here so the logging and the dry-clock reset will always be the same
void runPump(int seconds, WaterSource source) {

    Serial.printf("[pump] ON for %d s -- requested by %s\n",
                  seconds, waterSourceName(source));

    display.showPumping(seconds);

    digitalWrite(RELAY_PUMP_PIN, HIGH);
    delay(seconds * 1000UL);
    digitalWrite(RELAY_PUMP_PIN, LOW);

    display.clearOverride();
    Serial.println("[pump] OFF");

    lastWaterSource = source;
    lastWaterMs = millis();

    // Fresh water means the soil is no longer "continuously dry", so the
    // fallback clock restarts. Without this the board would pour again on
    // the very next pass while the reading catches up.
    dryStartMs = 0;
}


// Tracks how long the soil has been below the threshold without a break.
void updateDryClock(float moisture) {

    if (moisture >= WATERING_THRESHOLD) {
        dryStartMs = 0;
        return;
    }

    if (dryStartMs == 0) {
        dryStartMs = millis();
    }
}


// True once the soil has been dry longer than the backend had to react.
bool fallbackDue() {

    if (!FALLBACK_ENABLED) {
        return false;
    }

    if (dryStartMs == 0) {
        return false;
    }

    return (millis() - dryStartMs) >= FALLBACK_GRACE_MS;
}


// ---- light ----
//
// The sensor's only job here is to turn off (to veto) the lamp when the room is
// already bright enough that running it would be pointless.

enum class LightMode { AUTO, MANUAL_ON, MANUAL_OFF };

LightMode lightMode = LightMode::AUTO;

// When the current manual override was first seen, so MANUAL_ON can expire.
unsigned long lightManualMs = 0;

bool lightIsOn = false;

// millis() at the last relay change, or 0 if it has not moved since boot --
// which lets the very first switch happen immediately instead of waiting out
// a cooldown that never started.
unsigned long lightSwitchedMs = 0;

bool lightVetoed = false;

// False until NTP answers. Without a clock there is no photoperiod to be in,
// and the board holds the lamp off rather than guessing the hour.
bool lightTimeKnown = false;

// The last ambient reading. Ambient means the natural ambient lux without the light strip.
float lastAmbientLux = 0;

// The last effective reading: ambient plus the lamp, when the lamp was on.
// This is what the plant receives.
// This number is reported, but not used for controlling
float lastEffectiveLux = 0;

// When ambient was last actually measured
// 0 means never (so the first cycle always measures)
unsigned long lastAmbientMs = 0;


bool inPhotoperiod(int hour) {
    return hour >= LIGHT_HOUR_START && hour < LIGHT_HOUR_END;
}


bool lightCooldownActive() {

    if (lightSwitchedMs == 0) {
        return false;
    }

    return (millis() - lightSwitchedMs) < LIGHT_COOLDOWN_MS;
}


// True once ambient has actually been measured at least once. The dip schedule
// and the startup gate both hang off this, because they are the same question:
// has this board ever seen what the room looks like?
bool haveAmbient() {
    return lastAmbientMs != 0;
}


// True when ambient is due to be measured again rather than carried forward.
// See LIGHT_AMBIENT_MS in Config.h: if that interval is too often, it may wear off the relay.
bool ambientDue() {

    if (!haveAmbient()) {
        return true;
    }

    return (millis() - lastAmbientMs) >= LIGHT_AMBIENT_MS;
}


// Two light numbers
//
//   effective -- what the plant is receiving right now, lamp included
//   ambient   -- the room on its own, which needs the lamp switched off
//
// The control rule needs ambient
SensorData readSensorsWithLight(float& effectiveLux) {

    SensorData data = readSensors();

    // Lamp dark already: this one reading is both numbers, for free.
    if (!lightIsOn) {
        effectiveLux = data.light;
        lastAmbientLux = data.light;
        lastAmbientMs = millis();
        return data;
    }

    // Lamp on, so what was just read includes it.
    effectiveLux = data.light;

    if (!ambientDue()) {
        data.light = lastAmbientLux;
        return data;
    }

    digitalWrite(RELAY_LIGHT_PIN, LOW);
    delay(LIGHT_SETTLE_MS);

    // Discarded: this conversion may have started while the lamp was lit.
    readLight();

    data.light = readLight();

    digitalWrite(RELAY_LIGHT_PIN, HIGH);

    lastAmbientLux = data.light;
    lastAmbientMs = millis();

    return data;
}


void updateLightVeto(float ambientLux) {

    if (!lightVetoed && ambientLux > LIGHT_VETO_LUX) {
        lightVetoed = true;
        Serial.printf("[light] ambient %.1f lx > %.1f -- lamp not worth running\n",
                      ambientLux, LIGHT_VETO_LUX);
        return;
    }

    if (lightVetoed && ambientLux < LIGHT_VETO_CLEAR_LUX) {
        lightVetoed = false;
        Serial.printf("[light] ambient %.1f lx < %.1f -- veto lifted\n",
                      ambientLux, LIGHT_VETO_CLEAR_LUX);
    }
}


void pollLightMode() {

    String mode = checkLightMode();

    LightMode wanted = LightMode::AUTO;

    if (mode == "on") {
        wanted = LightMode::MANUAL_ON;
    }
    else if (mode == "off") {
        wanted = LightMode::MANUAL_OFF;
    }

    if (wanted == lightMode) {
        return;
    }

    Serial.printf("[light] mode is now %s\n", mode.c_str());

    lightMode = wanted;
    lightManualMs = millis();
    lightSwitchedMs = 0;
}


// A manual ON reverts to auto by itself, and the board clears the flag in
// Firebase the same way it clears the pump trigger after acting on it.
void expireLightManual() {

    if (lightMode != LightMode::MANUAL_ON) {
        return;
    }

    if ((millis() - lightManualMs) < LIGHT_MANUAL_MS) {
        return;
    }

    Serial.println("[light] manual ON expired -- back to auto");

    lightMode = LightMode::AUTO;
    sendLightMode("auto");
}


// The whole rule, in one place: time decides, the sensor only vetoes.
bool lightShouldBeOn() {

    if (!LIGHT_CONTROL_ENABLED) {
        return false;
    }

    if (lightMode == LightMode::MANUAL_OFF) {
        return false;
    }

    if (lightMode == LightMode::MANUAL_ON) {
        return true;
    }

    struct tm now;

    // Short timeout on purpose. getLocalTime() defaults to five seconds, which
    // would stall the entire loop on every pass while the clock is unset.
    if (!getLocalTime(&now, 10)) {
        lightTimeKnown = false;
        return false;
    }

    lightTimeKnown = true;

    // No decision before the first reading exists. Deciding on no data means
    // deciding "not vetoed", which switches the lamp on and arms the cooldown --
    // and the cooldown then blocks the correction for a full five minutes once the
    // first reading disagrees. Waiting up to one sensor cycle costs nothing and
    // saves a pointless relay cycle on every boot.
    if (!haveAmbient()) {
        return false;
    }

    if (!inPhotoperiod(now.tm_hour)) {
        return false;
    }

    return !lightVetoed;
}


void applyLight(bool wanted) {

    if (wanted == lightIsOn) {
        return;
    }

    if (lightCooldownActive()) {
        return;
    }

    digitalWrite(RELAY_LIGHT_PIN, wanted ? HIGH : LOW);

    lightIsOn = wanted;
    lightSwitchedMs = millis();

    Serial.printf("[light] lamp %s\n", wanted ? "ON" : "OFF");
}


// What the board reports back to Firebase. "no_time" is not a lamp fault: it
// means NTP never answered, so there is no photoperiod to be inside, and the
// board is refusing to run the lamp at an hour it cannot name.
const char* lightStateSlug() {

    if (lightIsOn) {
        return "on";
    }

    if (lightMode == LightMode::AUTO && !lightTimeKnown) {
        return "no_time";
    }

    return "off";
}


// Only written when it changes. This loop runs every couple of seconds and the
// dashboard has no use for a write that often.
void publishLightState() {

    static String lastPublished = "";

    String slug = lightStateSlug();

    if (slug == lastPublished) {
        return;
    }

    sendLightState(slug.c_str());
    lastPublished = slug;
}


String lightSummary() {

    if (lightMode == LightMode::MANUAL_ON) {
        return String("ON (manual, ")
             + ((LIGHT_MANUAL_MS - (millis() - lightManualMs)) / 1000UL)
             + " s left)";
    }

    if (lightMode == LightMode::MANUAL_OFF) {
        return "off (manual)";
    }

    struct tm now;

    if (!getLocalTime(&now, 10)) {
        return "off (auto, NO CLOCK)";
    }

    char clock[6];
    strftime(clock, sizeof(clock), "%H:%M", &now);

    if (!haveAmbient()) {
        return String("off (auto, ") + clock + ", no light reading yet)";
    }

    String why;

    if (!inPhotoperiod(now.tm_hour)) {
        why = String("outside ") + LIGHT_HOUR_START + "-" + LIGHT_HOUR_END;
    }
    else if (lightVetoed) {
        why = String("vetoed, room ") + String(lastAmbientLux, 0) + " lx";
    }
    else {
        why = String("window ") + LIGHT_HOUR_START + "-" + LIGHT_HOUR_END;
    }

    if (lightCooldownActive()) {
        why += String(", cooldown ")
             + ((LIGHT_COOLDOWN_MS - (millis() - lightSwitchedMs)) / 1000UL)
             + " s left";
    }

    return String(lightIsOn ? "ON" : "off")
         + " (auto, " + clock + ", " + why + ")";
}


// One line for the pump: what it is doing, and who last made it do something.
String pumpSummary(bool pumpTriggered, int pumpSeconds) {

    if (pumpTriggered) {
        return String("RUNNING ") + pumpSeconds + " s";
    }

    String state = "idle";

    if (dryStartMs != 0 && FALLBACK_ENABLED) {
        state += String(" (dry ") + secondsSince(dryStartMs)
               + "/" + (FALLBACK_GRACE_MS / 1000UL) + " s)";
    }

    if (lastWaterSource != WaterSource::NONE) {
        state += String(", last ") + waterSourceName(lastWaterSource)
               + " " + secondsSince(lastWaterMs) + " s ago";
    }

    return state;
}

void logSensorReport(const SensorData& data, bool pumpTriggered, int pumpSeconds) {

    Serial.println();
    Serial.println("------------- READING -------------");
    Serial.printf("Temperature : %.1f C\n",   data.temperature);
    Serial.printf("Humidity    : %.1f %%\n",  data.humidity);
    Serial.printf("Pressure    : %.1f hPa\n", data.pressure);
    Serial.printf("Light       : %.1f lx (ambient, %lu s ago)\n",
                  data.light, secondsSince(lastAmbientMs));
    Serial.printf("At plant    : %.1f lx\n",  lastEffectiveLux);
    Serial.printf("Soil        : %.1f %% (raw ADC %d)\n",
                  data.soilMoisture, data.soilRaw);

    Serial.println("-----------------------------------");
    Serial.printf("State       : %s\n",
                  plantStateName(evaluatePlantState(data)));
    Serial.printf("Lamp        : %s\n",
                  lightSummary().c_str());
    Serial.println("-----------------------------------");
}



void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("Plant Monitor");

    // LED
    initLED();

    // Relay
    pinMode(RELAY_PUMP_PIN, OUTPUT);
    digitalWrite(RELAY_PUMP_PIN, LOW);

    pinMode(RELAY_LIGHT_PIN, OUTPUT);
    digitalWrite(RELAY_LIGHT_PIN, LOW);

    //Display
    display.begin();

    // Sensors
    while (!initSensors()) {
        Serial.println("Sensor initialization failed.");
        Serial.println("Retrying in 3 seconds...");
        setLED(255, 80, 0);
        // Animates the startup bar while waiting, instead of freezing it.
        display.updateFor(3000);
    }

    // WiFi
    display.showConnecting();
    initWiFi();
    display.clearOverride();

    Serial.println("System ready!");
    display.update();
}


void loop() {
    int pump_duration = 0;

    // WiFi
    if (!isWiFiConnected()) {
        setLED(255, 0, 0);
        display.showConnecting();
        initWiFi();
        display.clearOverride();
    }


    // Pump. Read once per pass and remembered, so the sensor report can show
    // the flag even on passes where nothing fires.
    bool pumpTriggered = checkPump(pump_duration);

    // A trigger raised before this session's first reading predates the soil
    // data behind it. Drop it; the backend re-raises it if still dry.
    if (pumpTriggered && !haveReading) {
        firebaseRequest(
        "PUT",
        "/pump/trigger.json",
        "false");
        pumpTriggered = false;
    }

    if (pumpTriggered != lastPumpTrigger) {
        Serial.print("[pump] trigger is now ");
        Serial.println(pumpTriggered ? "TRUE" : "false");

        // A flag going true is the only evidence the board has that the
        // backend (or a person) is out there and reacting.
        if (pumpTriggered) {
            lastRemoteMs = millis();
        }

        lastPumpTrigger = pumpTriggered;
    }

    if (pumpTriggered) {
        // Cleared first, so a reset mid-pump cannot replay it on the next boot.
        firebaseRequest(
        "PUT",
        "/pump/trigger.json",
        "false");
        runPump(pump_duration, WaterSource::REMOTE);
    }
    else if (fallbackDue()) {
        // The backend had its window and did nothing. Water anyway.
        Serial.printf(
            "[fallback] dry for %lu s with no backend response -- watering\n",
            secondsSince(dryStartMs));
        runPump(WATERING_PUMP_SECONDS, WaterSource::FALLBACK);
    }

    // Sensors every SENSOR_INTERVAL seconds
    if (
        millis() - lastSensorSend
        >= SENSOR_INTERVAL
    ) {
        Serial.println("Reading sensors...");

        // readSensors() only takes about 200 ms, so without a short hold the
        // sensing bar would flash past unseen.
        display.showScanning();
        display.updateFor(SCAN_SCREEN_MS);

        // data.light is ambient
        SensorData data =
            readSensorsWithLight(lastEffectiveLux);

        updateLightVeto(data.light);

        // The face follows the newest reading
        PlantState state = evaluatePlantState(data);

        display.setPlantState(state);

        // Must run before the report
        updateDryClock(data.soilMoisture);
        haveReading = true;

        logSensorReport(data, lastPumpTrigger, pump_duration);

        // Show what was just measured, then fall back to the face on its own.
        display.showSensors(data, lastEffectiveLux, lastAmbientMs, SENSOR_SCREEN_MS);

        // Blue LED while sending
        setLED(0, 0, 255);
        sendSensorData(data, plantStateSlug(state), lightIsOn, lastEffectiveLux);
        setLED(0, 0, 0);
        lastSensorSend = millis();
    }

    // Light
    pollLightMode();
    expireLightManual();
    applyLight(lightShouldBeOn());
    publishLightState();

    // Keeps the screen animating instead of freezing on one frame.
    display.updateFor(LOOP_INTERVAL);
}
