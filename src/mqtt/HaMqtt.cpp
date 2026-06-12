#include "HaMqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
#include "../config.h"
#include "../storage/Storage.h"
#include "../storage/Codes.h"
#include "../rf/RFReceiver.h"
#include "../rf/RFTransmitter.h"
#include "../state/WindowSim.h"

namespace {
WiFiClient net;
PubSubClient mq(net);
String devId, baseT;
String hostStr;  // PubSubClient keeps the pointer, so this must stay alive
bool enabled = false;
bool coverPublished = false;
std::vector<uint32_t> publishedButtons;
uint32_t lastAttempt = 0, lastState = 0;

void pubJson(const String& topic, JsonDocument& doc, bool retain = true) {
  String out;
  serializeJson(doc, out);
  mq.publish(topic.c_str(), out.c_str(), retain);
}

void addDevice(JsonDocument& doc) {
  doc["avty_t"] = baseT + "/status";
  JsonObject dev = doc["dev"].to<JsonObject>();
  dev["ids"].add("windowctl_" + devId);
  dev["name"] = "Window Controller";
  dev["mf"] = "DIY";
  dev["mdl"] = "LOLIN C3 Mini 433MHz bridge";
  dev["sw"] = FW_VERSION;
}

void publishSensors() {
  {
    JsonDocument doc;
    doc["name"] = "RSSI";
    doc["uniq_id"] = "windowctl_" + devId + "_rssi";
    doc["stat_t"] = baseT + "/state";
    doc["val_tpl"] = "{{ value_json.rssi }}";
    doc["unit_of_meas"] = "dBm";
    doc["dev_cla"] = "signal_strength";
    doc["ent_cat"] = "diagnostic";
    addDevice(doc);
    pubJson("homeassistant/sensor/windowctl_" + devId + "/rssi/config", doc);
  }
  {
    JsonDocument doc;
    doc["name"] = "Uptime";
    doc["uniq_id"] = "windowctl_" + devId + "_uptime";
    doc["stat_t"] = baseT + "/state";
    doc["val_tpl"] = "{{ value_json.uptime }}";
    doc["unit_of_meas"] = "s";
    doc["ent_cat"] = "diagnostic";
    doc["ic"] = "mdi:timer-outline";
    addDevice(doc);
    pubJson("homeassistant/sensor/windowctl_" + devId + "/uptime/config", doc);
  }
}

void publishState() {
  JsonDocument doc;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["tx"] = RFTransmitter::txCount();
  doc["rx"] = RFReceiver::rxCount();
  pubJson(baseT + "/state", doc, false);
}

void handleMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String p;
  p.reserve(len);
  for (unsigned int i = 0; i < len; i++) p += (char)payload[i];

  if (t == baseT + "/cmd/cover") {
    const char* name = p == "OPEN" ? "open" : p == "CLOSE" ? "close" : p == "STOP" ? "stop" : nullptr;
    if (!name) return;
    // Track state even when no RF code is saved yet, so the command chain
    // (and the LED) can be exercised before the device is installed.
    if (p == "OPEN") WindowSim::set(WindowSim::State::Open, "mqtt cover");
    else if (p == "CLOSE") WindowSim::set(WindowSim::State::Closed, "mqtt cover");
    RfCode* c = Codes::findByName(name);
    if (c) RFTransmitter::requestSend(*c);
  } else if (t == baseT + "/cmd/replay") {
    RfCode* c = nullptr;
    long id = p.toInt();
    if (id > 0) c = Codes::findById((uint32_t)id);
    if (!c) c = Codes::findByName(p);
    if (c) RFTransmitter::requestSend(*c);
  }
}

bool tryConnect() {
  String cid = "windowctl-" + devId;
  String will = baseT + "/status";
  bool ok;
  if (Storage::mqttUser().length())
    ok = mq.connect(cid.c_str(), Storage::mqttUser().c_str(), Storage::mqttPass().c_str(),
                    will.c_str(), 0, true, "offline");
  else
    ok = mq.connect(cid.c_str(), will.c_str(), 0, true, "offline");
  if (!ok) return false;

  mq.publish(will.c_str(), "online", true);
  mq.subscribe((baseT + "/cmd/#").c_str());
  coverPublished = false;
  publishedButtons.clear();
  publishSensors();
  HaMqtt::publishDiscovery();
  publishState();
  HaMqtt::publishCoverState();
  return true;
}
}

void HaMqtt::begin(const String& deviceId) {
  devId = deviceId;
  baseT = "windowctl/" + devId;
  hostStr = Storage::mqttHost();
  enabled = hostStr.length() > 0;
  if (!enabled) return;
  mq.setServer(hostStr.c_str(), Storage::mqttPort());
  mq.setCallback(handleMessage);
  mq.setBufferSize(1024);
}

void HaMqtt::loop() {
  if (!enabled || WiFi.status() != WL_CONNECTED) return;
  if (!mq.connected()) {
    if (millis() - lastAttempt < 5000) return;
    lastAttempt = millis();
    if (!tryConnect()) return;
  }
  mq.loop();
  if (millis() - lastState > 30000) {
    lastState = millis();
    publishState();
  }
}

bool HaMqtt::connected() { return enabled && mq.connected(); }

void HaMqtt::publishDiscovery() {
  if (!enabled || !mq.connected()) return;

  // The cover entity is always published: commands drive the simulated state
  // even before open/close codes are learned (RF only fires once they exist).
  {
    JsonDocument doc;
    doc["name"] = "Window";
    doc["uniq_id"] = "windowctl_" + devId + "_window";
    doc["cmd_t"] = baseT + "/cmd/cover";
    doc["stat_t"] = baseT + "/cover/state";
    doc["dev_cla"] = "window";
    if (!Codes::findByName("stop")) doc["pl_stop"] = nullptr;  // hide stop arrow
    addDevice(doc);
    pubJson("homeassistant/cover/windowctl_" + devId + "/window/config", doc);
    coverPublished = true;
  }

  // Remove buttons for deleted codes, then (re)publish current ones.
  std::vector<uint32_t> current;
  for (const auto& c : Codes::all()) current.push_back(c.id);
  for (uint32_t old : publishedButtons) {
    if (std::find(current.begin(), current.end(), old) == current.end())
      mq.publish(("homeassistant/button/windowctl_" + devId + "/c" + String(old) + "/config").c_str(), "", true);
  }
  for (const auto& c : Codes::all()) {
    JsonDocument doc;
    doc["name"] = c.name;
    doc["uniq_id"] = "windowctl_" + devId + "_c" + String(c.id);
    doc["cmd_t"] = baseT + "/cmd/replay";
    doc["pl_prs"] = String(c.id);
    doc["ic"] = "mdi:remote";
    addDevice(doc);
    pubJson("homeassistant/button/windowctl_" + devId + "/c" + String(c.id) + "/config", doc);
  }
  publishedButtons = current;
}

void HaMqtt::publishCoverState() {
  if (!enabled || !mq.connected()) return;
  if (WindowSim::get() == WindowSim::State::Unknown) return;
  mq.publish((baseT + "/cover/state").c_str(), WindowSim::name(), true);
}

void HaMqtt::publishRx(const String& json) {
  if (!enabled || !mq.connected()) return;
  mq.publish((baseT + "/rx").c_str(), json.c_str());
}
