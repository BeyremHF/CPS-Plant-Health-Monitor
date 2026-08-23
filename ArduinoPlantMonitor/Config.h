#ifndef CONFIG_H
#define CONFIG_H

// WiFi credentials live in Secrets.h, which is gitignored so that personal
// network passwords never reach the repository. Copy Secrets.example.h to
// Secrets.h and fill it in before building.
#if __has_include("Secrets.h")
  #include "Secrets.h"
#else
  #error "Secrets.h is missing -- copy Secrets.example.h to Secrets.h and put your WiFi details in it"
#endif

// Firebase
#define FIREBASE_URL \
  "https://plant-health-monitor-esp32-default-rtdb.europe-west1.firebasedatabase.app"

#define PLANT_ID "basil-1"

// Pins
#define LED_PIN 48
#define ADC_PIN 1
#define RELAY_PUMP_PIN 2
#define RELAY_LIGHT_PIN 4

#define I2C_SDA 8
#define I2C_SCL 9

// Sensors
#define BH1750_ADDR 0x23

// Soil moisture calibration
#define SOIL_DRY 2650
#define SOIL_WET 950

// Plant state. This is the ONE definition of "how is the plant doing" for the
// whole system: it picks the OLED face, and the board publishes the result to
// Firebase so the web dashboard renders the same verdict instead of computing
// a second opinion of its own. The frontend's alert thresholds in
// Frontend/src/App.jsx mirror these values.
#define MOISTURE_HEALTHY_MIN  40.0   // below this counts as a stress factor
#define MOISTURE_MODERATE_MIN 25.0   // below this is bad enough on its own
#define TEMP_MIN              10.0   // degrees C
#define TEMP_MAX              32.0
#define LIGHT_MIN            100.0   // lux

// Watering. THIS FILE IS THE SINGLE SOURCE OF TRUTH for both numbers --
// Backend/main.py parses them out of here at startup, so editing them here
// changes the backend's behaviour too. Do not add a second copy anywhere.
//
// Deliberately separate from MOISTURE_HEALTHY_MIN above even though the value
// matches today: "when do we pump" and "when does the face look happy" are
// different questions, and tying them together means changing the face would
// silently change when the plant gets watered.
#define WATERING_THRESHOLD    40.0
#define WATERING_PUMP_SECONDS 2

// Standalone fallback watering.
//
// Normally the backend decides when to water and the board just obeys the flag
// in Firebase. That means watering silently stops whenever the laptop running
// the backend is closed. If the soil stays below WATERING_THRESHOLD for this
// long and nothing has triggered the pump in that time, the board waters on its
// own so the plant survives.
//
// The grace period must comfortably exceed the backend's own cycle (it checks
// every 30 s) so the board never races it. Set FALLBACK_ENABLED to 0 to hand
// control back to the backend entirely.
#define FALLBACK_ENABLED  1
#define FALLBACK_GRACE_MS 180000UL   // 3 minutes of unbroken dryness

// Timing
#define SENSOR_INTERVAL 30000UL

// Display
#define SENSOR_SCREEN_MS 4000UL
#define SCAN_SCREEN_MS 1500UL
#define LOOP_INTERVAL 2000UL

#endif