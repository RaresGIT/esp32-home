#include "Portal.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../storage/Storage.h"
#include "../storage/Codes.h"
#include "../rf/RFReceiver.h"
#include "../rf/RFTransmitter.h"
#include "../mqtt/HaMqtt.h"
#include "../state/WindowSim.h"
#include "../homekit/HomeKit.h"

namespace {
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
String devId;
bool ap = false;
volatile bool rebootPending = false;
uint32_t rebootAt = 0;

void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  res->setCode(code);
  serializeJson(doc, *res);
  req->send(res);
}

void sendOk(AsyncWebServerRequest* req) { req->send(200, "application/json", "{\"ok\":true}"); }

void sendErr(AsyncWebServerRequest* req, const char* msg) {
  String s = String("{\"ok\":false,\"error\":\"") + msg + "\"}";
  req->send(400, "application/json", s);
}

// Parses an inline signal from JSON: either {timings:[...]} (raw) or
// {protocol,bits,code,pulse} (decoded). Shared by save and replay endpoints.
RfCode codeFromJson(JsonObject o, bool& ok) {
  RfCode c;
  ok = false;
  c.name = (const char*)(o["name"] | "");
  c.repeats = o["repeats"] | DEFAULT_REPEATS;
  if (o["timings"].is<JsonArray>()) {
    c.raw = true;
    for (JsonVariant v : o["timings"].as<JsonArray>()) c.timings.push_back(v.as<uint16_t>());
    ok = c.timings.size() >= RAW_MIN_EDGES;
  } else if (o["code"].is<uint32_t>()) {
    c.raw = false;
    c.protocol = o["protocol"] | 1;
    c.bits = o["bits"] | 24;
    c.code = o["code"].as<uint32_t>();
    c.pulse = o["pulse"] | 0;
    ok = c.code != 0;
  }
  return c;
}

void registerApi() {
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["fw"] = FW_VERSION;
    doc["id"] = devId;
    doc["ap"] = ap;
    doc["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["ssid"] = ap ? "" : WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["tx"] = RFTransmitter::txCount();
    doc["rxc"] = RFReceiver::rxCount();
    doc["mqtt"] = HaMqtt::connected();
    doc["mode"] = RFReceiver::mode() == RFReceiver::Mode::Raw ? "raw" : "decoded";
    doc["codes"] = Codes::all().size();
    doc["window"] = WindowSim::name();
    sendJson(req, doc);
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ssid"] = Storage::wifiSsid();
    doc["mqttHost"] = Storage::mqttHost();
    doc["mqttPort"] = Storage::mqttPort();
    doc["mqttUser"] = Storage::mqttUser();
    sendJson(req, doc);
  });

  server.on("/api/homekit", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["active"] = HomeKit::active();
    doc["code"] = HomeKit::pairingCode();
    doc["uri"] = HomeKit::setupUri();
    sendJson(req, doc);
  });

  server.on("/api/codes", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc["codes"].to<JsonArray>();
    Codes::toJson(arr);
    sendJson(req, doc);
  });

  server.on("/api/codes", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("id")) return sendErr(req, "missing id");
    uint32_t id = req->getParam("id")->value().toInt();
    if (!Codes::remove(id)) return sendErr(req, "unknown id");
    HaMqtt::publishDiscovery();
    sendOk(req);
  });

  auto* codesPost = new AsyncCallbackJsonWebHandler("/api/codes", [](AsyncWebServerRequest* req, JsonVariant& json) {
    JsonObject o = json.as<JsonObject>();
    bool ok;
    RfCode c = codeFromJson(o, ok);
    if (!ok) return sendErr(req, "invalid signal");
    if (!c.name.length()) return sendErr(req, "name required");
    uint32_t id = Codes::add(c);
    HaMqtt::publishDiscovery();
    JsonDocument doc;
    doc["ok"] = true;
    doc["id"] = id;
    sendJson(req, doc);
  });
  codesPost->setMethod(HTTP_POST);
  server.addHandler(codesPost);

  auto* replayH = new AsyncCallbackJsonWebHandler("/api/replay", [](AsyncWebServerRequest* req, JsonVariant& json) {
    JsonObject o = json.as<JsonObject>();
    if (o["id"].is<uint32_t>()) {
      RfCode* c = Codes::findById(o["id"].as<uint32_t>());
      if (!c) return sendErr(req, "unknown id");
      RFTransmitter::requestSend(*c);
      return sendOk(req);
    }
    bool ok;
    RfCode c = codeFromJson(o, ok);
    if (!ok) return sendErr(req, "invalid signal");
    RFTransmitter::requestSend(c);
    sendOk(req);
  });
  replayH->setMethod(HTTP_POST);
  server.addHandler(replayH);

  auto* modeH = new AsyncCallbackJsonWebHandler("/api/mode", [](AsyncWebServerRequest* req, JsonVariant& json) {
    String m = (const char*)(json["mode"] | "");
    if (m == "raw") RFReceiver::requestMode(RFReceiver::Mode::Raw);
    else if (m == "decoded") RFReceiver::requestMode(RFReceiver::Mode::Decoded);
    else return sendErr(req, "mode must be raw|decoded");
    sendOk(req);
  });
  modeH->setMethod(HTTP_POST);
  server.addHandler(modeH);

  // Scans while connected (STA) are slow — the radio only sneaks off-channel
  // between beacons, easily 30s+ for a full sweep. So results are cached:
  // callers get the previous list instantly while a fresh scan runs behind it.
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    static String scanCache;
    int n = WiFi.scanComplete();
    if (n >= 0) {
      JsonDocument doc;
      JsonArray arr = doc.to<JsonArray>();
      for (int i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
      WiFi.scanDelete();
      serializeJson(doc, scanCache);
      req->send(200, "application/json", "{\"networks\":" + scanCache + "}");
      return;
    }
    if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true, false, false, 120);
    if (scanCache.length())
      req->send(200, "application/json", "{\"refreshing\":true,\"networks\":" + scanCache + "}");
    else
      req->send(202, "application/json", "{\"scanning\":true}");
  });

  auto* wifiH = new AsyncCallbackJsonWebHandler("/api/wifi", [](AsyncWebServerRequest* req, JsonVariant& json) {
    String ssid = (const char*)(json["ssid"] | "");
    String pass = (const char*)(json["pass"] | "");
    if (!ssid.length()) return sendErr(req, "ssid required");
    Storage::setWifi(ssid, pass);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    Portal::requestReboot();
  });
  wifiH->setMethod(HTTP_POST);
  server.addHandler(wifiH);

  auto* mqttH = new AsyncCallbackJsonWebHandler("/api/mqtt", [](AsyncWebServerRequest* req, JsonVariant& json) {
    String host = (const char*)(json["host"] | "");
    uint16_t port = json["port"] | 1883;
    String user = (const char*)(json["user"] | "");
    String pass = (const char*)(json["pass"] | "");
    Storage::setMqtt(host, port, user, pass);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    Portal::requestReboot();
  });
  mqttH->setMethod(HTTP_POST);
  server.addHandler(mqttH);

  server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* req) {
    Storage::factoryReset();
    LittleFS.remove("/codes.json");
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    Portal::requestReboot();
  });

  // OTA: POST /update?type=fw (firmware) or ?type=fs (dashboard filesystem)
  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        bool ok = !Update.hasError();
        AsyncWebServerResponse* res =
            req->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
        res->addHeader("Connection", "close");
        req->send(res);
        if (ok) Portal::requestReboot();
      },
      [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
          int cmd = U_FLASH;
          if (req->hasParam("type") && req->getParam("type")->value() == "fs") cmd = U_SPIFFS;
          Update.begin(UPDATE_SIZE_UNKNOWN, cmd);
        }
        if (len) Update.write(data, len);
        if (final) Update.end(true);
      });
}
}

void Portal::begin(const String& deviceId, bool apMode) {
  devId = deviceId;
  ap = apMode;

  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) Serial.printf("WS client %u connected\n", client->id());
  });
  server.addHandler(&ws);

  registerApi();

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest* req) {
    // Captive portal: send everything to the dashboard while in setup mode.
    if (ap) req->redirect("http://" + WiFi.softAPIP().toString() + "/");
    else req->send(404, "text/plain", "Not found");
  });

  server.begin();
}

void Portal::loop() {
  ws.cleanupClients();
  if (rebootPending && millis() > rebootAt) ESP.restart();
}

void Portal::broadcast(const String& json) {
  if (ws.count()) ws.textAll(json);
}

void Portal::requestReboot() {
  rebootPending = true;
  rebootAt = millis() + 1500;  // let the HTTP response flush first
}
