#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>
#include "Sensors.h"

void initWiFi();

bool isWiFiConnected();

String firebaseRequest(
    const String& method,
    const String& path,
    const String& data = ""
);

bool checkPump(int &duration);

// `state` is the board's plant-health verdict, published alongside the raw
// readings so the web dashboard renders the same face as the OLED instead of
// deriving a second opinion from the numbers.
void sendSensorData(const SensorData& data, const char* state);

#endif
