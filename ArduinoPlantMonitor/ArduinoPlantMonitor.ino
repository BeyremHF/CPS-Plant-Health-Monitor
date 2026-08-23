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

// When the soil first dropped below MOISTURE_HEALTHY_MIN and stayed there.
// 0 means "not currently dry". This is the clock the fallback runs on.
unsigned long dryStartMs = 0;

// When the board last saw someone else set the pump flag. Proof the backend
// is alive; 0 means it has not been heard from since boot.
unsigned long lastRemoteMs = 0;

// Works out which face to show from the latest reading. This lives on the
// board on purpose: the screen keeps telling the truth even with no WiFi and
// no backend running.
PlantState evaluatePlantState(const SensorData& data) {

    if (data.soilMoisture >= MOISTURE_HEALTHY_MIN) {
        return PlantState::HEALTHY;
    }

    if (data.soilMoisture >= MOISTURE_MODERATE_MIN) {
        return PlantState::MODERATE_STRESSED;
    }

    return PlantState::HIGH_STRESSED;
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


// millis() since a recorded moment, in whole seconds. Rollover-safe because
// the subtraction is done in unsigned arithmetic.
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
// fallback path go through here so the logging and the dry-clock reset can
// never drift apart.
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

    if (moisture >= MOISTURE_HEALTHY_MIN) {
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


// One readable block per sensor cycle: what was measured, how it compares to
// the thresholds in Config.h, and what the pump is actually doing.
//
// Note the watering line is a *prediction*, not a decision. This board never
// decides to water -- the backend compares moisture against its own threshold
// and sets the flag in Firebase. MOISTURE_HEALTHY_MIN here is the same 40.0
// the backend uses, so the two should agree; if they disagree on screen, the
// backend is either not running or working from a staler reading.
void logSensorReport(const SensorData& data, bool pumpTriggered, int pumpSeconds) {

    Serial.println();
    Serial.println("------------- READING -------------");
    Serial.printf("Temperature : %.1f C\n",   data.temperature);
    Serial.printf("Humidity    : %.1f %%\n",  data.humidity);
    Serial.printf("Pressure    : %.1f hPa\n", data.pressure);
    Serial.printf("Light       : %.1f lx\n",  data.light);
    Serial.printf("Soil        : %.1f %% (raw ADC %d)\n",
                  data.soilMoisture, data.soilRaw);

    Serial.println("------------- STATE ---------------");
    Serial.printf("Thresholds  : healthy >= %.1f %%, moderate >= %.1f %%\n",
                  MOISTURE_HEALTHY_MIN, MOISTURE_MODERATE_MIN);
    Serial.printf("Plant state : %s\n",
                  plantStateName(evaluatePlantState(data)));

    if (data.soilMoisture < MOISTURE_HEALTHY_MIN) {
        Serial.printf("Watering    : %.1f %% < %.1f %% -> NEEDS WATER\n",
                      data.soilMoisture, MOISTURE_HEALTHY_MIN);
    }
    else {
        Serial.printf("Watering    : %.1f %% >= %.1f %% -> not needed\n",
                      data.soilMoisture, MOISTURE_HEALTHY_MIN);
    }

    if (pumpTriggered) {
        Serial.printf("Pump        : TRIGGERED for %d s\n", pumpSeconds);
    }
    else {
        Serial.println("Pump        : not triggered");
    }


    Serial.println("------------- CONTROL -------------");

    if (!FALLBACK_ENABLED) {
        Serial.println("Mode        : backend only (fallback disabled)");
    }
    else if (dryStartMs == 0) {
        Serial.println("Mode        : backend (soil is wet, fallback idle)");
    }
    else {
        Serial.printf("Mode        : backend, board fallback armed\n");
        Serial.printf("Dry for     : %lu s of %lu s before the board acts\n",
                      secondsSince(dryStartMs), FALLBACK_GRACE_MS / 1000UL);
    }

    if (lastRemoteMs == 0) {
        Serial.println("Backend     : NOT SEEN since boot");
    }
    else {
        Serial.printf("Backend     : last set the flag %lu s ago\n",
                      secondsSince(lastRemoteMs));
    }

    if (lastWaterSource == WaterSource::NONE) {
        Serial.println("Last water  : none since boot");
    }
    else {
        Serial.printf("Last water  : %lu s ago, by %s\n",
                      secondsSince(lastWaterMs),
                      waterSourceName(lastWaterSource));
    }
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
        runPump(pump_duration, WaterSource::REMOTE);
        firebaseRequest(
        "PUT",
        "/pump/trigger.json",
        "false");
    }
    else if (fallbackDue()) {
        // The backend had its window and did nothing. Water anyway.
        Serial.printf(
            "[fallback] dry for %lu s with no backend response -- watering\n",
            secondsSince(dryStartMs));
        runPump(FALLBACK_PUMP_SECONDS, WaterSource::FALLBACK);
    }

    digitalWrite(RELAY_LIGHT_PIN, HIGH);
    display.updateFor(3000);
    digitalWrite(RELAY_LIGHT_PIN, LOW);


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

        SensorData data =
            readSensors();

        // The face follows the newest reading.
        display.setPlantState(
            evaluatePlantState(data)
        );

        // Must run before the report so the CONTROL section reflects this
        // reading rather than the previous one.
        updateDryClock(data.soilMoisture);

        logSensorReport(data, lastPumpTrigger, pump_duration);

        // Show what was just measured, then fall back to the face on its own.
        display.showSensors(data, SENSOR_SCREEN_MS);

        // Blue LED while sending
        setLED(0, 0, 255);
        sendSensorData(data);
        setLED(0, 0, 0);
        lastSensorSend = millis();
    }

    // Keeps the screen animating instead of freezing on one frame.
    display.updateFor(LOOP_INTERVAL);
}
