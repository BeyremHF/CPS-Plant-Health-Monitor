#include "Firebase.h"
#include "Config.h"
#include "Led.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>


// WiFi
void initWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {
        setLED(255, 0, 0);
        delay(500);
        setLED(0, 0, 0);
        delay(500);
        Serial.print(".");
        Serial.println(WiFi.status());
    }

    Serial.println();
    Serial.println("WiFi connected");

    setLED(0, 255, 0);

    delay(2000);

    setLED(0, 0, 0);

    // NTP
    configTime(
        0,
        0,
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

// Send sensors
void sendSensorData(
    const SensorData& data,
    const char* state
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

    doc["state"] = state;

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