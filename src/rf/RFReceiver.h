#pragma once
#include <Arduino.h>

// 433MHz receive engine with two modes:
//  - Decoded: rc-switch interrupt decoder (EV1527/PT2262 family and friends)
//  - Raw: our own edge-timing ISR, for protocols rc-switch can't decode
// The two modes attach different ISRs to the same pin, so they are exclusive.
namespace RFReceiver {
enum class Mode : uint8_t { Decoded, Raw };

struct Decoded {
  uint8_t protocol;
  uint8_t bits;
  uint32_t code;
  uint16_t pulse;
};

void begin();
void loop();  // applies pending mode switches (safe to request from any task)
void requestMode(Mode m);
Mode mode();

bool readDecoded(Decoded& out);
size_t readRaw(uint16_t* out, size_t maxLen);  // returns edge count, 0 if none

uint32_t rxCount();
}
