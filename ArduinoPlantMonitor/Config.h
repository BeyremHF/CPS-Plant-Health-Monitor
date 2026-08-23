#ifndef CONFIG_H
#define CONFIG_H

// WiFi credentials should be in Secrets.h
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

// Plant state, from soil moisture (%)
#define MOISTURE_HEALTHY_MIN  40.0   // below this counts as a stress factor
#define MOISTURE_MODERATE_MIN 25.0   // below this is bad enough on its own
#define TEMP_MIN              10.0   // degrees C
#define TEMP_MAX              32.0

// Frontend/src/App.jsx raises a "light level is too low" notice below this. 
// It does NOT feed the plant state above, and it is NOT the lamp's control threshold either
#define LIGHT_MIN            100.0   // lux

// Watering. THIS FILE IS THE SINGLE SOURCE OF TRUTH:
// Backend/main.py parses them out of here at startup
#define WATERING_THRESHOLD    40.0
#define WATERING_PUMP_SECONDS 2

// Standalone fallback watering
#define FALLBACK_ENABLED  1
#define FALLBACK_GRACE_MS 180000UL   // 3 minutes of unbroken dryness

// Grow light
#define LIGHT_CONTROL_ENABLED  1
#define LIGHT_HOUR_START       6      // local hour, inclusive
#define LIGHT_HOUR_END        22      // local hour, exclusive -- 16 h of light
#define LIGHT_VETO_LUX       300.0    // above this ambient, the lamp is pointless
#define LIGHT_VETO_CLEAR_LUX 200.0    // below this the veto lifts; the gap stops chatter
#define LIGHT_COOLDOWN_MS   300000UL  // relay cannot switch again for 5 min
#define LIGHT_MANUAL_MS     7200000UL // a manual "on" reverts to auto after 2 h

// Local time zone for the photoperiod. Accounted for Germany's DST rules
#define TZ_GERMANY "CET-1CEST,M3.5.0,M10.5.0/3"

// How long to leave the lamp dark before trusting a light reading
#define LIGHT_SETTLE_MS      220

// How often to actually take that ambient reading while the lamp is running
// if the interval is too short, the relay may wear out from switching too often.
#define LIGHT_AMBIENT_MS  600000UL   // 10 minutes

// effective brightness at the plant's position, for fallback purposes only
#define LAMP_LUX_AT_PLANT   1800.0

// Timing
#define SENSOR_INTERVAL 30000UL

// Display
#define SENSOR_SCREEN_MS 4000UL
#define SCAN_SCREEN_MS 1500UL
#define LOOP_INTERVAL 2000UL

#endif