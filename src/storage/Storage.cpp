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

void Storage::factoryReset() { prefs.clear(); }
