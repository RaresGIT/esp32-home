#include <Arduino.h>

// Hardware smoke test for the LOLIN C3 Mini v2.1.0:
// cycles the onboard WS2812B RGB LED (GPIO7) and prints a serial heartbeat.
constexpr uint8_t RGB_PIN = 7;

void setup() {
  Serial.begin(115200);
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("blink %lu\n", (unsigned long)n);
  switch (n++ % 3) {
    case 0: neopixelWrite(RGB_PIN, 32, 0, 0); break;  // red
    case 1: neopixelWrite(RGB_PIN, 0, 32, 0); break;  // green
    case 2: neopixelWrite(RGB_PIN, 0, 0, 32); break;  // blue
  }
  delay(500);
  neopixelWrite(RGB_PIN, 0, 0, 0);  // off
  delay(500);
}
