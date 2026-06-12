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

Step decideStep(float pos, float target, Motion motion,
                float leadPct, float deadband, bool settleOk) {
  const bool fullOpen  = target >= 100.0f - 0.001f;
  const bool fullClose = target <= 0.0f + 0.001f;

  switch (motion) {
    case Motion::Opening:
      if (fullOpen) {
        if (pos >= 100.0f - 0.001f) return {Command::None, Motion::Idle, true};
        return {Command::None, Motion::Opening, false};
      }
      if (pos >= target - leadPct) {
        // Stop the opening. "Reached" when approaching target from just below;
        // if target is well below us this is a retarget -> reverse next (idle).
        bool reached = pos <= target + deadband;
        return {Command::SendClose, Motion::Idle, reached};
      }
      return {Command::None, Motion::Opening, false};

    case Motion::Closing:
      if (fullClose) {
        if (pos <= 0.0f + 0.001f) return {Command::None, Motion::Idle, true};
        return {Command::None, Motion::Closing, false};
      }
      if (pos <= target + leadPct) {
        bool reached = pos >= target - deadband;
        return {Command::SendOpen, Motion::Idle, reached};
      }
      return {Command::None, Motion::Closing, false};

    case Motion::Idle:
    default:
      if (pos > target - deadband && pos < target + deadband)
        return {Command::None, Motion::Idle, true};
      if (!settleOk) return {Command::None, Motion::Idle, false};
      if (target > pos) return {Command::SendOpen, Motion::Opening, false};
      return {Command::SendClose, Motion::Closing, false};
  }
}

}  // namespace WindowModel
