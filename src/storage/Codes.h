#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// A learned RF signal: either a decoded fixed-code (rc-switch protocol)
// or a raw pulse-train captured verbatim off the receiver.
struct RfCode {
  uint32_t id = 0;
  String name;
  bool raw = false;
  uint8_t protocol = 1;
  uint8_t bits = 24;
  uint32_t code = 0;
  uint16_t pulse = 0;  // pulse length in us, 0 = protocol default
  uint8_t repeats = 10;
  std::vector<uint16_t> timings;  // raw mode only, alternating high/low us
};

// Code library persisted to LittleFS (/codes.json).
namespace Codes {
void begin();
const std::vector<RfCode>& all();
RfCode* findById(uint32_t id);
RfCode* findByName(const String& name);
uint32_t add(RfCode c);
bool remove(uint32_t id);
void toJson(JsonArray out);
}
