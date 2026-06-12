#pragma once
#include <Arduino.h>

// Native HomeKit accessory (HomeSpan): the device pairs directly with the
// Apple Home app — no Home Assistant or broker required. Exposes a Window
// service mapped to the open/close RF codes and the simulated state.
// HAP runs on its own TCP port (1201); the dashboard keeps port 80.
namespace HomeKit {
void begin(const String& deviceId, const String& ssid, const String& pass);
void poll();
bool active();
void notifyState(bool open);  // reflect state changes from other sources

const char* pairingCode();  // 8 digits
String setupUri();          // X-HM://... payload encoded in the pairing QR
}
