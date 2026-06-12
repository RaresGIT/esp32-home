#include "state/WindowModel.h"

namespace {
float clampPct(float p) { return p < 0.0f ? 0.0f : (p > 100.0f ? 100.0f : p); }
}

namespace WindowModel {

float estimatePct(float startPct, uint32_t elapsedMs, Motion motion,
                  uint32_t openMs, uint32_t closeMs) {
  float p = startPct;
  if (motion == Motion::Opening && openMs > 0)
    p = startPct + (float)elapsedMs / (float)openMs * 100.0f;
  else if (motion == Motion::Closing && closeMs > 0)
    p = startPct - (float)elapsedMs / (float)closeMs * 100.0f;
  return clampPct(p);
}

}  // namespace WindowModel
