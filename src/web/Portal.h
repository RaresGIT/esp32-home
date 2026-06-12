#pragma once
#include <Arduino.h>

// Async web server: dashboard (LittleFS), REST API, WebSocket event stream,
// OTA updates, and captive-portal behavior while in setup-AP mode.
namespace Portal {
void begin(const String& deviceId, bool apMode);
void loop();
void broadcast(const String& json);  // push an event to all dashboard clients
void requestReboot();
}
