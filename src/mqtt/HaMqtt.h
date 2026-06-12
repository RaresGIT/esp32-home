#pragma once
#include <Arduino.h>

// MQTT client with Home Assistant discovery.
// Entities published:
//  - cover "Window" (when codes named open + close exist; stop optional)
//  - one button per saved code
//  - diagnostic sensors (RSSI, uptime)
namespace HaMqtt {
void begin(const String& deviceId);
void loop();
bool connected();
void publishDiscovery();           // call after the code library changes
void publishRx(const String& json);  // forward received signals to MQTT
void publishCoverState();          // push the (simulated) window state
}
