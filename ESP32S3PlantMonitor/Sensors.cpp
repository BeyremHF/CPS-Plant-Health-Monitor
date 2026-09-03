#include "Sensors.h"
#include "Config.h"
#include <Wire.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

bool initSensors() {
    //I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    // Try BME280 address 0x76
    if (bme.begin(0x76, &Wire)) {
        Serial.println("BME280 found at 0x76");
    }
    // Try 0x77
    else if (bme.begin(0x77, &Wire)) {
        Serial.println("BME280 found at 0x77");
    }
    else {
        Serial.println("BME280 not found!");
        return false;
    }
    // ADC
    analogReadResolution(12);
    analogSetPinAttenuation(ADC_PIN, ADC_11db);
    pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);

    return true;
}


// BH1750
float readLight() {
    Wire.beginTransmission(BH1750_ADDR);
    Wire.write(0x10);
    Wire.endTransmission();
    delay(200);
    Wire.requestFrom(BH1750_ADDR, 2);
    if (Wire.available() < 2) {
        return 0;
    }
    uint16_t raw =
        (Wire.read() << 8) |
        Wire.read();

    return raw / 1.2;
}

// Soil moisture
float readSoilMoisture(int &raw) {
    raw = analogRead(ADC_PIN);
    float moisture =
        ((float)(SOIL_DRY - raw) /
         (float)(SOIL_DRY - SOIL_WET)) * 100.0;
    moisture = constrain(moisture, 0, 100);
    return moisture;
}

bool isWaterTankEmpty() {
    return digitalRead(WATER_LEVEL_PIN) == LOW;
}

// Read all sensors
SensorData readSensors() {
    SensorData data;
    data.temperature = bme.readTemperature();
    data.humidity = bme.readHumidity();
    data.pressure =
        bme.readPressure() / 100.0;
    data.light =
        readLight();
    data.soilMoisture =
        readSoilMoisture(data.soilRaw);
    data.waterTankEmpty = isWaterTankEmpty();

    return data;
}