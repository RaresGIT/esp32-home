#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "storage/Storage.h"
#include "storage/Codes.h"
#include "rf/RFReceiver.h"
#include "rf/RFTransmitter.h"
#include "web/Portal.h"
#include "mqtt/HaMqtt.h"
#include "state/WindowPosition.h"
#include "homekit/HomeKit.h"

static DNSServer dns;
static bool apMode = false;
static String devId;

static String deviceId() {
  // Unique (non-OUI) half of the base MAC.
  uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  snprintf(buf, sizeof(buf), "%06x", (uint32_t)(mac >> 24) & 0xFFFFFF);
  return String(buf);
}

// First-time setup AP with captive portal. Once Wi-Fi credentials exist,
// HomeSpan owns the STA connection (and mDNS hostname) instead.
static void startAp() {
  String host = "windowctl-" + devId;
  apMode = true;
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t) {
    if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) Serial.println("AP: station associated");
    else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) Serial.println("AP: station left");
    else if (e == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) Serial.println("AP: station got IP");
  });
  WiFi.mode(WIFI_AP_STA);  // AP_STA so the portal can still scan networks
  WiFi.softAP(host.c_str(), AP_PASSWORD);
  // C3 Mini antenna quirk: full TX power distorts when the antenna is
  // detuned by nearby metal/wires; 8.5dBm is the known-good setting.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Setup AP '%s' (pass '%s'), portal at http://%s/\n", host.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

// Minimal serial console for provisioning without the captive portal.
// Reads space-separated tokens; values containing spaces can be "quoted".
static String nextToken(String& s) {
  s.trim();
  if (!s.length()) return "";
  String tok;
  if (s[0] == '"') {
    int e = s.indexOf('"', 1);
    if (e < 0) { tok = s.substring(1); s = ""; }
    else { tok = s.substring(1, e); s = s.substring(e + 1); }
  } else {
    int e = s.indexOf(' ');
    if (e < 0) { tok = s; s = ""; }
    else { tok = s.substring(0, e); s = s.substring(e + 1); }
  }
  return tok;
}

static void handleSerialCmd(String line) {
  line.trim();
  if (!line.length()) return;
  String cmd = nextToken(line);
  if (cmd == "wifi") {
    String ssid = nextToken(line);
    String pass = nextToken(line);
    if (!ssid.length()) {
      Serial.println("usage: wifi <ssid> <password>   (use \"quotes\" around values with spaces)");
      return;
    }
    Storage::setWifi(ssid, pass);
    Serial.printf("Saved Wi-Fi '%s', rebooting...\n", ssid.c_str());
    delay(200);
    ESP.restart();
  } else if (cmd == "status") {
    Serial.printf("fw %s  ap=%d  wifi=%s  ip=%s  rssi=%d  heap=%u\n", FW_VERSION, (int)apMode,
                  WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "(not connected)",
                  apMode ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str(),
                  (int)WiFi.RSSI(), ESP.getFreeHeap());
  } else if (cmd == "reboot") {
    ESP.restart();
  } else if (cmd == "factory") {
    Storage::factoryReset();
    LittleFS.remove("/codes.json");
    ESP.restart();
  } else {
    Serial.println("commands: wifi <ssid> <pass> | status | reboot | factory");
  }
}

static void pollSerial() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      handleSerialCmd(line);
      line = "";
    } else if (line.length() < 200) {
      line += ch;
    }
  }
}

static void pollRf() {
  RFReceiver::Decoded d;
  if (RFReceiver::readDecoded(d)) {
    JsonDocument doc;
    doc["type"] = "rx";
    doc["mode"] = "decoded";
    doc["protocol"] = d.protocol;
    doc["bits"] = d.bits;
    doc["code"] = d.code;
    doc["pulse"] = d.pulse;
    String s;
    serializeJson(doc, s);
    Serial.println(s);
    Portal::broadcast(s);
    HaMqtt::publishRx(s);
  }

  static uint16_t raw[RAW_MAX_EDGES];
  size_t n = RFReceiver::readRaw(raw, RAW_MAX_EDGES);
  if (n) {
    JsonDocument doc;
    doc["type"] = "rx";
    doc["mode"] = "raw";
    doc["edges"] = n;
    JsonArray t = doc["timings"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) t.add(raw[i]);
    String s;
    serializeJson(doc, s);
    Serial.printf("{\"type\":\"rx\",\"mode\":\"raw\",\"edges\":%u}\n", (unsigned)n);
    Portal::broadcast(s);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nWindow Controller %s\n", FW_VERSION);

  Storage::begin();
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  Codes::begin();
  RFTransmitter::begin();
  RFReceiver::begin();
  WindowPosition::begin();

  devId = deviceId();
  String ssid = Storage::wifiSsid();
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);  // brings up the TCP/IP stack before the web server starts
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // C3 Mini antenna quirk, see startAp()
    HomeKit::begin(devId, ssid, Storage::wifiPass());
    HaMqtt::begin(devId);
  } else {
    startAp();
  }
  Portal::begin(devId, apMode);
}

void loop() {
  if (apMode) {
    dns.processNextRequest();
    pollSerial();  // in STA mode the serial console belongs to HomeSpan's CLI
  } else {
    HomeKit::poll();
  }
  WindowPosition::tick();
  RFReceiver::loop();

  String sent;
  if (RFTransmitter::loop(sent)) {
    JsonDocument doc;
    doc["type"] = "tx";
    doc["name"] = sent;
    String s;
    serializeJson(doc, s);
    Portal::broadcast(s);
  }

  pollRf();
  HaMqtt::loop();
  Portal::loop();
}
