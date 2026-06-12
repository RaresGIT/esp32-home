#pragma once
#include <stdint.h>

// Pure, hardware-free window position logic. No Arduino includes so it builds
// and unit-tests on the host. Position is 0 (fully closed) .. 100 (fully open).
namespace WindowModel {

enum class Motion : uint8_t { Idle = 0, Opening = 1, Closing = 2 };
enum class Command : uint8_t { None = 0, SendOpen = 1, SendClose = 2 };

struct Step {
  Command command;       // RF command to emit this tick (None = nothing)
  Motion  motion;        // resulting motion state
  bool    reachedTarget; // true once settled at target (caller clears target)
};

// Estimate position after `elapsedMs` of the given motion, starting from
// `startPct`. Clamped to [0,100]. Durations are full-travel times in ms.
float estimatePct(float startPct, uint32_t elapsedMs, Motion motion,
                  uint32_t openMs, uint32_t closeMs);

// Decide the next action. `leadPct` = how early (in position units) to stop for
// the active direction; `deadband` = settle tolerance; `settleOk` = enough time
// has passed since the last stop to begin a new directional move.
Step decideStep(float pos, float target, Motion motion,
                float leadPct, float deadband, bool settleOk);

}  // namespace WindowModel
