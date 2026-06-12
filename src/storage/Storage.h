#pragma once
#include <Arduino.h>

// Persistent settings in NVS (survives reflash of the filesystem image).
namespace Storage {
void begin();

String wifiSsid();
String wifiPass();
void setWifi(const String& ssid, const String& pass);

String mqttHost();
uint16_t mqttPort();
String mqttUser();
String mqttPass();
void setMqtt(const String& host, uint16_t port, const String& user, const String& pass);

void factoryReset();
}
