#pragma once
#include <Arduino.h>

// Tracked window state. There is no feedback from the real window, so this
// is inferred from the commands we send (and doubles as the test harness
// while the device isn't installed yet). The onboard RGB LED mirrors it:
// green = open, red = closed, blue = unknown.
namespace WindowSim {
enum class State : uint8_t { Unknown, Open, Closed };

void begin();
void set(State s, const char* source);
State get();
const char* name();  // "unknown" | "open" | "closed"
}
