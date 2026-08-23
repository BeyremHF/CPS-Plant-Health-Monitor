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
        lastPumpTrigger = pumpTriggered;
    }

    if (pumpTriggered) {
        Serial.print("Pump ON for ");
        Serial.print(pump_duration);
        Serial.println(" seconds");
        display.showPumping(pump_duration);

        digitalWrite(RELAY_PUMP_PIN, HIGH);
        delay(pump_duration * 1000UL);
        digitalWrite(RELAY_PUMP_PIN, LOW);
        Serial.println("Pump OFF");
        display.clearOverride();
        firebaseRequest(
        "PUT",
        "/pump/trigger.json",
        "false");
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
