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

void sendSensorData(
    const SensorData& data,
    bool lampOn,
    float effectiveLux
);

String checkHealthState();

String checkLightMode();

void sendLightMode(const char* mode);

void sendLightState(const char* state);

#endif
