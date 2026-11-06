#include <Arduino.h>
#include <stdlib.h>
#include <stdint.h>


const uint8_t BRIGHTNESS = 10;   // 0-255
const uint16_t BLINK_MS = 500;

void setup() {
    rgbLedWrite(RGB_BUILTIN, BRIGHTNESS, BRIGHTNESS, 0);  // yellow on

}

void loop() {
  rgbLedWrite(RGB_BUILTIN, BRIGHTNESS, BRIGHTNESS, 0);  // yellow on
  delay(BLINK_MS);

  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);           // off
  delay(BLINK_MS);
}
