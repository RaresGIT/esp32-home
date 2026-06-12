#include "WindowSim.h"
#include "../config.h"
#include "../mqtt/HaMqtt.h"
#include "../homekit/HomeKit.h"

namespace {
WindowSim::State cur = WindowSim::State::Unknown;

void showLed() {
  switch (cur) {
    case WindowSim::State::Open:   neopixelWrite(PIN_LED, 0, LED_BRIGHTNESS, 0); break;
    case WindowSim::State::Closed: neopixelWrite(PIN_LED, LED_BRIGHTNESS, 0, 0); break;
    default:                       neopixelWrite(PIN_LED, 0, 0, LED_BRIGHTNESS); break;
  }
}
}

void WindowSim::begin() { showLed(); }

WindowSim::State WindowSim::get() { return cur; }

const char* WindowSim::name() {
  return cur == State::Open ? "open" : cur == State::Closed ? "closed" : "unknown";
}

void WindowSim::set(State s, const char* source) {
  if (s == cur) return;
  cur = s;
  showLed();
  Serial.printf("window state -> %s (via %s)\n", name(), source);
  HaMqtt::publishCoverState();
  if (s != State::Unknown) HomeKit::notifyState(s == State::Open);
}
