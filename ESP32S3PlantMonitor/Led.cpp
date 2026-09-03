#include "Led.h"
#include "Config.h"
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel led(
    1,
    LED_PIN,
    NEO_GRB + NEO_KHZ800
);

void initLED() {
    led.begin();
    led.clear();
    led.show();
}

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}