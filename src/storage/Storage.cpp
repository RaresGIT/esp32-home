#include "Storage.h"
#include <Preferences.h>

namespace {
Preferences prefs;
}

void Storage::begin() { prefs.begin("windowctl", false); }

String Storage::wifiSsid() { return prefs.getString("wssid", ""); }
String Storage::wifiPass() { return prefs.getString("wpass", ""); }
void Storage::setWifi(const String& ssid, const String& pass) {
  prefs.putString("wssid", ssid);
  prefs.putString("wpass", pass);
}

String Storage::mqttHost() { return prefs.getString("mhost", ""); }
uint16_t Storage::mqttPort() { return prefs.getUShort("mport", 1883); }
String Storage::mqttUser() { return prefs.getString("muser", ""); }
String Storage::mqttPass() { return prefs.getString("mpass", ""); }
void Storage::setMqtt(const String& host, uint16_t port, const String& user, const String& pass) {
  prefs.putString("mhost", host);
  prefs.putUShort("mport", port);
  prefs.putString("muser", user);
  prefs.putString("mpass", pass);
}

uint32_t Storage::windowOpenMs() { return prefs.getUInt("wopen", 23500); }
uint32_t Storage::windowCloseMs() { return prefs.getUInt("wclose", 27000); }
uint32_t Storage::windowStopLeadMs() { return prefs.getUInt("wlead", 800); }
void Storage::setWindowCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs) {
  prefs.putUInt("wopen", openMs);
  prefs.putUInt("wclose", closeMs);
  prefs.putUInt("wlead", stopLeadMs);
}
uint8_t Storage::windowLastPos() { return prefs.getUChar("wpos", 0); }
void Storage::setWindowLastPos(uint8_t pct) { prefs.putUChar("wpos", pct); }

void Storage::factoryReset() { prefs.clear(); }
