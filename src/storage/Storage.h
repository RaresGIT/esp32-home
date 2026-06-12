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

uint32_t windowOpenMs();     // full close->open travel time (default 23500)
uint32_t windowCloseMs();    // full open->close travel time (default 27000)
uint32_t windowStopLeadMs(); // stop lead compensation (default 800)
void setWindowCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs);
uint8_t windowLastPos();           // last known position % (default 0)
void setWindowLastPos(uint8_t pct);

void factoryReset();
}
