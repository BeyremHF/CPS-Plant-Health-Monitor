#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

struct SensorData {
    float temperature;
    float humidity;
    float pressure;
    float light;
    float soilMoisture;
    int soilRaw;
    bool waterTankEmpty;

};

bool initSensors();

// Exposed so the light control can flush a stale conversion after switching the light strip
float readLight();

SensorData readSensors();
bool isWaterTankEmpty();

#endif