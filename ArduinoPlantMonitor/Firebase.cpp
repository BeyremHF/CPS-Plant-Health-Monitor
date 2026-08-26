#include "Firebase.h"
#include "Config.h"
#include "Led.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char* wifiStatusName(wl_status_t status) {

    if (status == WL_CONNECTED) {
        return "connected";
    }

    if (status == WL_NO_SSID_AVAIL) {
        return "network not found -- wrong SSID, or it is 5 GHz "
               "(this board is 2.4 GHz only)";
    }

    if (status == WL_CONNECT_FAILED) {
        return "connection refused -- usually a wrong password";
    }

    if (status == WL_CONNECTION_LOST) {
        return "connection lost";
    }

    if (status == WL_DISCONNECTED) {
        return "not connected -- still trying to associate "
               "(wrong password or the network is out of range)";
    }

    if (status == WL_IDLE_STATUS) {
        return "idle, no attempt in progress yet";
    }

    if (status == WL_SCAN_COMPLETED) {
        return "scan finished";
    }

    return "unknown status";
}


// WiFi
void initWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Connecting to WiFi \"%s\"\n", WIFI_SSID);

    unsigned long attemptStart = millis();

    while (WiFi.status() != WL_CONNECTED) {
        setLED(255, 0, 0);
        delay(500);
        setLED(0, 0, 0);
        delay(500);

        wl_status_t status = WiFi.status();

        Serial.printf("[wifi] %lu s: %s (status %d)\n",
                      (millis() - attemptStart) / 1000UL,
                      wifiStatusName(status),
                      (int)status);
    }

    Serial.println();
    Serial.printf("WiFi connected -- IP %s, %d dBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());

    setLED(0, 255, 0);

    delay(2000);

    setLED(0, 0, 0);

    // NTP. Local German time
    configTzTime(
        TZ_GERMANY,
        "pool.ntp.org",
        "time.nist.gov"
    );
}


bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}


String firebaseRequest(
    const String& method,
    const String& path,
    const String& data
) {

    HTTPClient http;

    http.begin(String(FIREBASE_URL) + path);

    if (method == "PUT" || method == "POST") {
        http.addHeader("Content-Type", "application/json");
    }

    int code;

    if (method == "GET") {
        code = http.GET();
    }
    else if (method == "PUT") {
        code = http.PUT(data);
    }
    else if (method == "POST") {
        code = http.POST(data);
    }
    else {
        http.end();
        return "";
    }

    String response = "";

    if (code > 0) {
        response = http.getString();
    }

    http.end();

    return response;
}

// Pump
bool checkPump(int &duration) {
    String response = firebaseRequest("GET", "/pump.json");
    if (response == "") {
        return false;
    }
    JsonDocument doc;
    DeserializationError error =
        deserializeJson(doc, response);
    if (error) {
        Serial.print("Pump JSON error: ");
        Serial.println(error.c_str());
        return false;
    }
    bool trigger = doc["trigger"] | false;
    duration = doc["duration"] | 0;
    if (trigger && duration > 0) {
        return true;
    }
    return false;
}

// Health
//
// The verdict the model in Backend/main.py published. Any failure returns "",
// which the caller reads as "nothing new" and keeps whatever it had.
String checkHealthState() {

    String response = firebaseRequest("GET", "/health/state.json");

    if (response == "") {
        return "";
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        Serial.print("Health JSON error: ");
        Serial.println(error.c_str());
        return "";
    }

    const char* slug = doc.as<const char*>();

    if (slug == nullptr) {
        return "";
    }

    String value(slug);

    if (value == "healthy" || value == "moderate_stress" || value == "high_stress") {
        return value;
    }

    return "";
}


// Light
//
// Any failure here returns "auto"
String checkLightMode() {

    String response = firebaseRequest("GET", "/light/mode.json");

    if (response == "") {
        return "auto";
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        Serial.print("Light JSON error: ");
        Serial.println(error.c_str());
        return "auto";
    }

    const char* mode = doc.as<const char*>();

    if (mode == nullptr) {
        return "auto";
    }

    String value(mode);

    if (value == "auto" || value == "on" || value == "off") {
        return value;
    }

    return "auto";
}


// Used by the board to clear its own expired manual override, the same way it
// clears the pump trigger after acting on it.
void sendLightMode(const char* mode) {
    firebaseRequest(
        "PUT",
        "/light/mode.json",
        String("\"") + mode + "\""
    );
}


void sendLightState(const char* state) {
    firebaseRequest(
        "PUT",
        "/light/state.json",
        String("\"") + state + "\""
    );
}


// Send sensors
void sendSensorData(
    const SensorData& data,
    const char* state,
    bool lampOn,
    float effectiveLux
) {

    JsonDocument doc;

    doc["temperature"] =
        round(data.temperature * 10) / 10.0;

    doc["humidity"] =
        round(data.humidity * 10) / 10.0;

    doc["pressure"] =
        round(data.pressure * 10) / 10.0;

    doc["light"] =
        round(data.light * 10) / 10.0;

    doc["soil_moisture"] =
        round(data.soilMoisture * 10) / 10.0;

    doc["soil_raw"] = data.soilRaw;

    doc["water_tank_empty"] = data.waterTankEmpty;
    
    // Absent until the backend has been heard from, rather than a guess.
    if (state != nullptr && state[0] != '\0') {
        doc["state"] = state;
    }

    doc["light_effective"] =
        round(effectiveLux * 10) / 10.0;

    doc["lamp"] = lampOn;

    time_t now;

    time(&now);

    doc["timestamp"] =
        (unsigned long)now;

    String json;

    serializeJson(doc, json);

    // Current sensor values
    firebaseRequest(
        "PUT",
        "/sensors.json",
        json
    );

    // History
    firebaseRequest(
        "POST",
        "/history/" + String(PLANT_ID) + ".json",
        json
    );

    Serial.println("Sent:");
    Serial.println(json);
}