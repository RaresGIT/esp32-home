#include "Codes.h"
#include <LittleFS.h>

namespace {
std::vector<RfCode> codes;
uint32_t nextId = 1;
const char* PATH = "/codes.json";

void persist() {
  JsonDocument doc;
  doc["nextId"] = nextId;
  JsonArray arr = doc["codes"].to<JsonArray>();
  Codes::toJson(arr);
  File f = LittleFS.open(PATH, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}
}

void Codes::begin() {
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  nextId = doc["nextId"] | 1;
  for (JsonObject o : doc["codes"].as<JsonArray>()) {
    RfCode c;
    c.id = o["id"] | 0;
    c.name = (const char*)(o["name"] | "");
    c.raw = o["raw"] | false;
    c.repeats = o["repeats"] | 10;
    if (c.raw) {
      for (JsonVariant v : o["timings"].as<JsonArray>()) c.timings.push_back(v.as<uint16_t>());
    } else {
      c.protocol = o["protocol"] | 1;
      c.bits = o["bits"] | 24;
      c.code = o["code"] | 0UL;
      c.pulse = o["pulse"] | 0;
    }
    if (c.id) codes.push_back(c);
  }
}

const std::vector<RfCode>& Codes::all() { return codes; }

RfCode* Codes::findById(uint32_t id) {
  for (auto& c : codes)
    if (c.id == id) return &c;
  return nullptr;
}

RfCode* Codes::findByName(const String& name) {
  for (auto& c : codes)
    if (c.name.equalsIgnoreCase(name)) return &c;
  return nullptr;
}

uint32_t Codes::add(RfCode c) {
  c.id = nextId++;
  codes.push_back(std::move(c));
  persist();
  return codes.back().id;
}

bool Codes::remove(uint32_t id) {
  for (auto it = codes.begin(); it != codes.end(); ++it) {
    if (it->id == id) {
      codes.erase(it);
      persist();
      return true;
    }
  }
  return false;
}

void Codes::toJson(JsonArray out) {
  for (const auto& c : codes) {
    JsonObject o = out.add<JsonObject>();
    o["id"] = c.id;
    o["name"] = c.name;
    o["raw"] = c.raw;
    o["repeats"] = c.repeats;
    if (c.raw) {
      JsonArray t = o["timings"].to<JsonArray>();
      for (uint16_t v : c.timings) t.add(v);
    } else {
      o["protocol"] = c.protocol;
      o["bits"] = c.bits;
      o["code"] = c.code;
      o["pulse"] = c.pulse;
    }
  }
}
