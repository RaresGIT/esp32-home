# Window Position Estimation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add timed dead-reckoning window position estimation (0–100%) with stop-at-target control, reliable RF re-assertion, an animated HomeKit/HA cover, and a web-portal Window tab with calibration.

**Architecture:** A pure, host-testable `WindowModel` (estimator + state-machine functions) underpins a stateful `WindowPosition` module that owns position/motion, drives the RF transmitter, persists calibration, and notifies HomeKit/MQTT/portal. `WindowPosition` replaces `WindowSim`. The motor latches each command to a full end; the opposite command stops it; intermediate positions are reached by firing the opposite command when the timed estimate hits the target.

**Tech Stack:** PlatformIO (Arduino-ESP32, LOLIN C3 Mini), HomeSpan (HomeKit), PubSubClient (MQTT), ESPAsyncWebServer, ArduinoJson, Unity (native unit tests), vanilla JS dashboard on LittleFS.

**Motor rules (authoritative):** OPEN/CLOSE only; each runs to a full end on its own; holding/repeating the same command does not interrupt; the **opposite** command **stops** (does not reverse); from a positional stop the **next** command sets direction; full open = 23.5 s, full close = 27 s.

---

## File Structure

- **Create** `src/state/WindowModel.h` / `WindowModel.cpp` — pure logic (`estimatePct`, `decideStep`), no Arduino deps.
- **Create** `src/state/WindowPosition.h` / `WindowPosition.cpp` — stateful owner; replaces `WindowSim`.
- **Create** `test/test_window_model/test_main.cpp` — Unity host tests for the pure logic.
- **Delete** `src/state/WindowSim.h` / `WindowSim.cpp`.
- **Modify** `platformio.ini` — add `[env:native]` test env.
- **Modify** `src/storage/Storage.h` / `Storage.cpp` — calibration + last-position persistence.
- **Modify** `src/homekit/HomeKit.h` / `HomeKit.cpp` — positionable cover.
- **Modify** `src/mqtt/HaMqtt.cpp` — positionable HA cover.
- **Modify** `src/web/Portal.cpp` — `/api/window` endpoints, status field, replay-notify.
- **Modify** `src/rf/RFTransmitter.cpp` — drop window-state coupling.
- **Modify** `src/main.cpp` — begin/tick wiring.
- **Modify** `data/index.html`, `data/app.js`, `data/style.css` — Window tab + calibration.

**Toolchain note (use in every `pio` step):** if `pio` is not on `PATH`, use the bundled binary:
```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" --version    # if this fails: pip3 install -U platformio, then re-resolve PIO
```

---

## Task 1: Native test env + `estimatePct`

**Files:**
- Create: `src/state/WindowModel.h`, `src/state/WindowModel.cpp`
- Create: `test/test_window_model/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Add the native test env to `platformio.ini`**

Append at the end of the file:

```ini
[env:native]
platform = native
test_framework = unity
build_src_filter = +<state/WindowModel.cpp>
build_flags = -Isrc -std=gnu++17
```

- [ ] **Step 2: Create the header `src/state/WindowModel.h`**

```cpp
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
```

- [ ] **Step 3: Write the failing test `test/test_window_model/test_main.cpp`**

```cpp
#include <unity.h>
#include "state/WindowModel.h"

using namespace WindowModel;

void test_estimate_opening_half(void) {
  // Opening from 0 for half of openMs (20000) -> 50%
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, estimatePct(0, 10000, Motion::Opening, 20000, 27000));
}
void test_estimate_closing_quarter(void) {
  // Closing from 100 for a quarter of closeMs (20000) -> 75%
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 75.0f, estimatePct(100, 5000, Motion::Closing, 23500, 20000));
}
void test_estimate_clamps_high(void) {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, estimatePct(50, 999999, Motion::Opening, 20000, 27000));
}
void test_estimate_clamps_low(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, estimatePct(50, 999999, Motion::Closing, 23500, 27000));
}
void test_estimate_idle_unchanged(void) {
  TEST_ASSERT_EQUAL_FLOAT(42.0f, estimatePct(42, 5000, Motion::Idle, 20000, 27000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_estimate_opening_half);
  RUN_TEST(test_estimate_closing_quarter);
  RUN_TEST(test_estimate_clamps_high);
  RUN_TEST(test_estimate_clamps_low);
  RUN_TEST(test_estimate_idle_unchanged);
  return UNITY_END();
}
```

- [ ] **Step 4: Run the test to verify it fails (no implementation yet)**

```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" test -e native
```
Expected: FAIL/link error — `estimatePct` undefined (and `WindowModel.cpp` missing).

- [ ] **Step 5: Implement `src/state/WindowModel.cpp` (estimator only)**

```cpp
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
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
"$PIO" test -e native
```
Expected: PASS (5 tests).

- [ ] **Step 7: Commit**

```bash
git add platformio.ini src/state/WindowModel.h src/state/WindowModel.cpp test/test_window_model/test_main.cpp
git commit -m "feat: window position estimator (pure, host-tested)"
```

---

## Task 2: `decideStep` state machine

**Files:**
- Modify: `src/state/WindowModel.cpp`
- Modify: `test/test_window_model/test_main.cpp`

- [ ] **Step 1: Add failing tests for `decideStep`**

Insert these test functions above `main()` in `test/test_window_model/test_main.cpp`:

```cpp
// lead/deadband chosen for clarity; settleOk=true unless a test needs the guard.
void test_idle_starts_open_toward_higher_target(void) {
  Step s = decideStep(0, 100, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendOpen, s.command);
  TEST_ASSERT_EQUAL(Motion::Opening, s.motion);
}
void test_idle_starts_close_toward_lower_target(void) {
  Step s = decideStep(80, 30, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, s.command);
  TEST_ASSERT_EQUAL(Motion::Closing, s.motion);
}
void test_idle_at_target_is_done(void) {
  Step s = decideStep(50, 50, Motion::Idle, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_TRUE(s.reachedTarget);
}
void test_idle_waits_when_not_settled(void) {
  Step s = decideStep(80, 30, Motion::Idle, 3.0f, 1.5f, false);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_FALSE(s.reachedTarget);
}
void test_opening_full_keeps_going_until_limit(void) {
  Step s = decideStep(60, 100, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Opening, s.motion);
}
void test_opening_full_done_at_limit(void) {
  Step s = decideStep(100, 100, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
  TEST_ASSERT_TRUE(s.reachedTarget);
}
void test_opening_stops_at_intermediate_with_lead(void) {
  // target 50, lead 3 -> stop once pos >= 47
  Step below = decideStep(46, 50, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, below.command);
  Step at = decideStep(47, 50, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, at.command);   // close-cmd stops an opening window
  TEST_ASSERT_EQUAL(Motion::Idle, at.motion);
}
void test_closing_stops_at_intermediate_with_lead(void) {
  // target 50, lead 3 -> stop once pos <= 53
  Step above = decideStep(54, 50, Motion::Closing, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::None, above.command);
  Step at = decideStep(53, 50, Motion::Closing, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendOpen, at.command);    // open-cmd stops a closing window
  TEST_ASSERT_EQUAL(Motion::Idle, at.motion);
}
void test_opening_retarget_below_triggers_stop(void) {
  // moving open at 70 but target dropped to 30 -> must stop now
  Step s = decideStep(70, 30, Motion::Opening, 3.0f, 1.5f, true);
  TEST_ASSERT_EQUAL(Command::SendClose, s.command);
  TEST_ASSERT_EQUAL(Motion::Idle, s.motion);
}
```

Register them in `main()` (add `RUN_TEST(...)` lines for each, before `return UNITY_END();`):

```cpp
  RUN_TEST(test_idle_starts_open_toward_higher_target);
  RUN_TEST(test_idle_starts_close_toward_lower_target);
  RUN_TEST(test_idle_at_target_is_done);
  RUN_TEST(test_idle_waits_when_not_settled);
  RUN_TEST(test_opening_full_keeps_going_until_limit);
  RUN_TEST(test_opening_full_done_at_limit);
  RUN_TEST(test_opening_stops_at_intermediate_with_lead);
  RUN_TEST(test_closing_stops_at_intermediate_with_lead);
  RUN_TEST(test_opening_retarget_below_triggers_stop);
```

- [ ] **Step 2: Run to verify the new tests fail**

```bash
"$PIO" test -e native
```
Expected: FAIL — `decideStep` undefined.

- [ ] **Step 3: Implement `decideStep` in `src/state/WindowModel.cpp`**

Add inside `namespace WindowModel { ... }`, after `estimatePct`:

```cpp
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
      // intermediate target, or a retarget now below us -> stop early by leadPct
      if (pos >= target - leadPct) return {Command::SendClose, Motion::Idle, false};
      return {Command::None, Motion::Opening, false};

    case Motion::Closing:
      if (fullClose) {
        if (pos <= 0.0f + 0.001f) return {Command::None, Motion::Idle, true};
        return {Command::None, Motion::Closing, false};
      }
      if (pos <= target + leadPct) return {Command::SendOpen, Motion::Idle, false};
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
```

- [ ] **Step 4: Run to verify all tests pass**

```bash
"$PIO" test -e native
```
Expected: PASS (14 tests).

- [ ] **Step 5: Commit**

```bash
git add src/state/WindowModel.cpp test/test_window_model/test_main.cpp
git commit -m "feat: window move/stop decision state machine (host-tested)"
```

---

## Task 3: Storage — calibration + last position

**Files:**
- Modify: `src/storage/Storage.h`
- Modify: `src/storage/Storage.cpp`

- [ ] **Step 1: Declare the new functions in `src/storage/Storage.h`**

Add inside `namespace Storage { ... }`, before `void factoryReset();`:

```cpp
uint32_t windowOpenMs();     // full close->open travel time (default 23500)
uint32_t windowCloseMs();    // full open->close travel time (default 27000)
uint32_t windowStopLeadMs(); // stop lead compensation (default 800)
void setWindowCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs);
uint8_t windowLastPos();           // last known position % (default 0)
void setWindowLastPos(uint8_t pct);
```

- [ ] **Step 2: Implement them in `src/storage/Storage.cpp`**

Add before `void Storage::factoryReset() { prefs.clear(); }`:

```cpp
uint32_t Storage::windowOpenMs() { return prefs.getUInt("wopen", 23500); }
uint32_t Storage::windowCloseMs() { return prefs.getUInt("wclose", 27000); }
uint32_t Storage::windowStopLeadMs() { return prefs.getUInt("wlead", 800); }
void Storage::setWindowCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs) {
  prefs.putUInt("wopen", openMs);
  prefs.putUInt("wclose", closeMs);
  prefs.putUInt("wlead", stopLeadMs);
}
uint8_t Storage::windowLastPos() { return prefs.getUChar("wpos", 0); }
void Storage::setWindowLastPos(uint8_t pct) { prefs.putUChar("wpos", pct); }
```

- [ ] **Step 3: Verify firmware still compiles**

```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" run -e lolin_c3_mini
```
Expected: SUCCESS (compiles; `WindowSim` still present and used at this point).

- [ ] **Step 4: Commit**

```bash
git add src/storage/Storage.h src/storage/Storage.cpp
git commit -m "feat: persist window calibration and last position"
```

---

## Task 4: `WindowPosition` stateful module

**Files:**
- Create: `src/state/WindowPosition.h`, `src/state/WindowPosition.cpp`

> Note: this module references `HomeKit::notifyPosition`, which is added in Task 6. To keep this task compiling on its own, the notify call to HomeKit is added in Task 6; here we notify MQTT + portal + LED only and leave a marked insertion point.

- [ ] **Step 1: Create `src/state/WindowPosition.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "WindowModel.h"

// Stateful owner of window position/motion. Drives the RF transmitter, persists
// calibration + last position, and notifies HomeKit/MQTT/portal. Replaces the
// old binary WindowSim. The onboard RGB LED mirrors state: green=open, red=
// closed, amber=resting partway, blue=moving.
namespace WindowPosition {
void begin();
void tick();                       // call every loop()

void goTo(uint8_t pct);            // 0..100; tick() drives the motor there
void openFull();                   // goTo(100)
void closeFull();                  // goTo(0)
void stop();                       // halt at current estimate now
void noteExternalCommand(bool opening);  // a sniffer replay already sent RF; just track

uint8_t position();                // current estimated %
WindowModel::Motion motion();
int target();                      // -1 if none
const char* motionName();          // "idle" | "opening" | "closing"

void setCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs);
uint32_t openMs();
uint32_t closeMs();
uint32_t stopLeadMs();
}  // namespace WindowPosition
```

- [ ] **Step 2: Create `src/state/WindowPosition.cpp`**

```cpp
#include "WindowPosition.h"
#include "../config.h"
#include "../storage/Storage.h"
#include "../storage/Codes.h"
#include "../rf/RFTransmitter.h"
#include "../mqtt/HaMqtt.h"
#include "../web/Portal.h"
#include <ArduinoJson.h>

using WindowModel::Motion;
using WindowModel::Command;

namespace {
float s_pos = 0;            // estimated position
float s_startPct = 0;       // position when current motion began
uint32_t s_startMs = 0;     // millis() when current motion began
uint32_t s_lastSendMs = 0;  // last RF send (for re-assertion throttle)
uint32_t s_lastStopMs = 0;  // last stop (settle guard before reversing direction)
Motion s_motion = Motion::Idle;
int s_target = -1;          // -1 = none

uint32_t cfgOpenMs = 23500, cfgCloseMs = 27000, cfgStopLeadMs = 800;

const float DEADBAND = 1.5f;
const uint32_t SETTLE_MS = 1000;
const uint32_t RESEND_MS = 4000;
const uint32_t NOTIFY_MS = 500;
uint32_t s_lastNotifyMs = 0;
int s_lastNotifyPos = -1;
Motion s_lastNotifyMotion = Motion::Idle;

uint8_t roundPct(float p) { return (uint8_t)(p + 0.5f); }

const char* motionStr() {
  return s_motion == Motion::Opening ? "opening" : s_motion == Motion::Closing ? "closing" : "idle";
}

void showLed() {
  if (s_motion != Motion::Idle) { neopixelWrite(PIN_LED, 0, 0, LED_BRIGHTNESS); return; }  // blue
  if (s_pos >= 99.0f) neopixelWrite(PIN_LED, 0, LED_BRIGHTNESS, 0);                          // green
  else if (s_pos <= 1.0f) neopixelWrite(PIN_LED, LED_BRIGHTNESS, 0, 0);                      // red
  else neopixelWrite(PIN_LED, LED_BRIGHTNESS, LED_BRIGHTNESS / 2, 0);                        // amber
}

void notifySinks(bool force) {
  uint32_t now = millis();
  if (!force && now - s_lastNotifyMs < NOTIFY_MS) return;
  s_lastNotifyMs = now;
  // HOMEKIT_NOTIFY_HOOK (added in Task 6)
  HaMqtt::publishCoverState();
  JsonDocument doc;
  doc["type"] = "window";
  doc["pos"] = roundPct(s_pos);
  doc["motion"] = motionStr();
  doc["target"] = s_target;
  String out;
  serializeJson(doc, out);
  Portal::broadcast(out);
  showLed();
}

void sendDir(bool open) {
  RfCode* c = Codes::findByName(open ? "open" : "close");
  if (c) RFTransmitter::requestSend(*c);
  else Serial.printf("WindowPosition: no '%s' code saved\n", open ? "open" : "close");
  s_lastSendMs = millis();
}

void recompute() {
  s_pos = WindowModel::estimatePct(s_startPct, millis() - s_startMs, s_motion, cfgOpenMs, cfgCloseMs);
}
}  // namespace

void WindowPosition::begin() {
  cfgOpenMs = Storage::windowOpenMs();
  cfgCloseMs = Storage::windowCloseMs();
  cfgStopLeadMs = Storage::windowStopLeadMs();
  s_pos = Storage::windowLastPos();
  s_motion = Motion::Idle;
  s_target = -1;
  showLed();
}

void WindowPosition::goTo(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_target = pct;
}
void WindowPosition::openFull() { goTo(100); }
void WindowPosition::closeFull() { goTo(0); }

void WindowPosition::stop() {
  recompute();
  if (s_motion == Motion::Opening) sendDir(false);
  else if (s_motion == Motion::Closing) sendDir(true);
  s_motion = Motion::Idle;
  s_target = -1;
  s_lastStopMs = millis();
  Storage::setWindowLastPos(roundPct(s_pos));
  notifySinks(true);
}

void WindowPosition::noteExternalCommand(bool opening) {
  recompute();
  s_motion = opening ? Motion::Opening : Motion::Closing;
  s_startMs = millis();
  s_startPct = s_pos;
  s_target = opening ? 100 : 0;
  s_lastSendMs = millis();  // RF already sent by caller; don't double-send now
  notifySinks(true);
}

void WindowPosition::tick() {
  uint32_t now = millis();
  recompute();

  if (s_target >= 0) {
    uint32_t dur = (s_motion == Motion::Closing) ? cfgCloseMs : cfgOpenMs;
    float leadPct = dur ? (float)cfgStopLeadMs / (float)dur * 100.0f : 0.0f;
    bool settleOk = (now - s_lastStopMs) > SETTLE_MS;
    WindowModel::Step st =
        WindowModel::decideStep(s_pos, (float)s_target, s_motion, leadPct, DEADBAND, settleOk);

    if (st.command == Command::SendOpen) sendDir(true);
    else if (st.command == Command::SendClose) sendDir(false);

    if (st.motion != s_motion) {
      if (st.motion == Motion::Idle) {
        s_lastStopMs = now;
        Storage::setWindowLastPos(roundPct(s_pos));
      } else {
        s_startMs = now;
        s_startPct = s_pos;
      }
      s_motion = st.motion;
      notifySinks(true);
    }
    if (st.reachedTarget) {
      s_target = -1;
      Storage::setWindowLastPos(roundPct(s_pos));
    }

    // Re-assert the same direction mid-travel so the motor reliably latches,
    // but not in the final stretch (avoid colliding with the stop).
    if (st.command == Command::None && s_motion != Motion::Idle &&
        (now - s_lastSendMs) > RESEND_MS &&
        (now - s_startMs) < (dur > 3000 ? dur - 3000 : 0)) {
      sendDir(s_motion == Motion::Opening);
    }
  }

  if (s_motion != Motion::Idle &&
      ((int)roundPct(s_pos) != s_lastNotifyPos || s_motion != s_lastNotifyMotion)) {
    notifySinks(false);
    s_lastNotifyPos = roundPct(s_pos);
    s_lastNotifyMotion = s_motion;
  }
}

uint8_t WindowPosition::position() { return roundPct(s_pos); }
WindowModel::Motion WindowPosition::motion() { return s_motion; }
int WindowPosition::target() { return s_target; }
const char* WindowPosition::motionName() { return motionStr(); }

void WindowPosition::setCalibration(uint32_t openMs, uint32_t closeMs, uint32_t stopLeadMs) {
  cfgOpenMs = openMs;
  cfgCloseMs = closeMs;
  cfgStopLeadMs = stopLeadMs;
  Storage::setWindowCalibration(openMs, closeMs, stopLeadMs);
}
uint32_t WindowPosition::openMs() { return cfgOpenMs; }
uint32_t WindowPosition::closeMs() { return cfgCloseMs; }
uint32_t WindowPosition::stopLeadMs() { return cfgStopLeadMs; }
```

- [ ] **Step 3: Commit (compiles after Task 5 swaps the includes; do not build yet)**

```bash
git add src/state/WindowPosition.h src/state/WindowPosition.cpp
git commit -m "feat: stateful WindowPosition module (RF drive, calibration, notify)"
```

---

## Task 5: Remove `WindowSim`, swap call sites

**Files:**
- Delete: `src/state/WindowSim.h`, `src/state/WindowSim.cpp`
- Modify: `src/main.cpp`
- Modify: `src/rf/RFTransmitter.cpp`

(HomeKit, HaMqtt, Portal are migrated in Tasks 6–8; this task does main + RFTransmitter so the tree converges. The build is expected to fail until Task 8 — that's fine; the final build/verify is Task 10. If you need an intermediate green build, do Tasks 6–8 before building.)

- [ ] **Step 1: Delete the old module**

```bash
git rm src/state/WindowSim.h src/state/WindowSim.cpp
```

- [ ] **Step 2: Update `src/main.cpp` include**

Replace:
```cpp
#include "state/WindowSim.h"
```
with:
```cpp
#include "state/WindowPosition.h"
```

- [ ] **Step 3: Update `src/main.cpp` setup() call**

In `setup()`, replace:
```cpp
  WindowSim::begin();
```
with:
```cpp
  WindowPosition::begin();
```

- [ ] **Step 4: Add the tick to `src/main.cpp` loop()**

In `loop()`, immediately after the `if (apMode) { ... } else { HomeKit::poll(); }` block and before `RFReceiver::loop();`, add:
```cpp
  WindowPosition::tick();
```

- [ ] **Step 5: Decouple `src/rf/RFTransmitter.cpp` from window state**

Remove the include:
```cpp
#include "../state/WindowSim.h"
```
And in `RFTransmitter::loop`, delete these two lines:
```cpp
  if (job.name.equalsIgnoreCase("open")) WindowSim::set(WindowSim::State::Open, "rf replay");
  else if (job.name.equalsIgnoreCase("close")) WindowSim::set(WindowSim::State::Closed, "rf replay");
```
(Leave the `sentName = ...` line and everything else intact.)

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: remove WindowSim; wire WindowPosition into main and RF"
```

---

## Task 6: HomeKit positionable cover

**Files:**
- Modify: `src/homekit/HomeKit.h`
- Modify: `src/homekit/HomeKit.cpp`
- Modify: `src/state/WindowPosition.cpp`

- [ ] **Step 1: Update `src/homekit/HomeKit.h`**

Add the model include near the top (after `#include <Arduino.h>`):
```cpp
#include "../state/WindowModel.h"
```
Replace:
```cpp
void notifyState(bool open);  // reflect state changes from other sources
```
with:
```cpp
void notifyPosition(uint8_t pct, WindowModel::Motion motion);  // reflect live position
```

- [ ] **Step 2: Update the `WindowService` in `src/homekit/HomeKit.cpp`**

Add the include near the other includes at the top:
```cpp
#include "../state/WindowPosition.h"
```

Replace the `WindowService` constructor body's range line:
```cpp
    target->setRange(0, 100, 100);
```
with:
```cpp
    target->setRange(0, 100, 1);
```

Replace the entire `update()` method:
```cpp
  boolean update() override {
    inUpdate = true;
    int t = target->getNewVal();
    bool open = t >= 50;
    RfCode* c = Codes::findByName(open ? "open" : "close");
    if (c) RFTransmitter::requestSend(*c);
    WindowSim::set(open ? WindowSim::State::Open : WindowSim::State::Closed, "homekit");
    int norm = open ? 100 : 0;
    cur->setVal(norm);
    if (t != norm) pendingNorm = norm;  // can't touch target inside its own update
    inUpdate = false;
    return true;
  }
```
with:
```cpp
  boolean update() override {
    WindowPosition::goTo((uint8_t)target->getNewVal());
    return true;
  }
```

Replace the entire `loop()` method:
```cpp
  void loop() override {
    if (pendingNorm >= 0) {
      target->setVal(pendingNorm);
      pendingNorm = -1;
    }
  }
```
with:
```cpp
  void loop() override {}  // position is driven by WindowPosition via notifyPosition()
```

Remove the now-unused member `int pendingNorm = -1;` and the now-unused `inUpdate`/`inUpdate=...` references in this struct if present (the `inUpdate` namespace flag stays; just stop setting it here).

- [ ] **Step 3: Replace `HomeKit::notifyState` with `HomeKit::notifyPosition` in `HomeKit.cpp`**

Replace:
```cpp
void HomeKit::notifyState(bool open) {
  if (!isActive || !win || inUpdate) return;
  int v = open ? 100 : 0;
  if (win->cur->getVal() != v) win->cur->setVal(v);
  if (win->target->getVal() != v) win->target->setVal(v);
}
```
with:
```cpp
void HomeKit::notifyPosition(uint8_t pct, WindowModel::Motion motion) {
  if (!isActive || !win) return;
  if (win->cur->getVal<int>() != pct) win->cur->setVal(pct);
  uint8_t ps = motion == WindowModel::Motion::Opening ? 1   // increasing
             : motion == WindowModel::Motion::Closing ? 0   // decreasing
                                                      : 2;  // stopped
  if (win->posState->getVal<int>() != ps) win->posState->setVal(ps);
  if (motion == WindowModel::Motion::Idle && win->target->getVal<int>() != pct)
    win->target->setVal(pct);
}
```

- [ ] **Step 4: Enable the HomeKit notify hook in `src/state/WindowPosition.cpp`**

Add the include near the others:
```cpp
#include "../homekit/HomeKit.h"
```
In `notifySinks`, replace the comment line:
```cpp
  // HOMEKIT_NOTIFY_HOOK (added in Task 6)
```
with:
```cpp
  HomeKit::notifyPosition(roundPct(s_pos), s_motion);
```

- [ ] **Step 5: Commit**

```bash
git add src/homekit/HomeKit.h src/homekit/HomeKit.cpp src/state/WindowPosition.cpp
git commit -m "feat: HomeKit positionable cover driven by WindowPosition"
```

---

## Task 7: MQTT positionable cover

**Files:**
- Modify: `src/mqtt/HaMqtt.cpp`

- [ ] **Step 1: Swap the include**

Replace:
```cpp
#include "../state/WindowSim.h"
```
with:
```cpp
#include "../state/WindowPosition.h"
#include "../state/WindowModel.h"
```

- [ ] **Step 2: Add position topics to cover discovery**

In `HaMqtt::publishDiscovery`, in the cover block, replace:
```cpp
    doc["cmd_t"] = baseT + "/cmd/cover";
    doc["stat_t"] = baseT + "/cover/state";
    doc["dev_cla"] = "window";
    if (!Codes::findByName("stop")) doc["pl_stop"] = nullptr;  // hide stop arrow
```
with:
```cpp
    doc["cmd_t"] = baseT + "/cmd/cover";
    doc["stat_t"] = baseT + "/cover/state";
    doc["pos_t"] = baseT + "/cover/position";
    doc["set_pos_t"] = baseT + "/cmd/position";
    doc["pos_open"] = 100;
    doc["pos_clsd"] = 0;
    doc["dev_cla"] = "window";
```

- [ ] **Step 3: Rewrite command handling**

In `handleMessage`, replace the `cmd/cover` block:
```cpp
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
```
with:
```cpp
  if (t == baseT + "/cmd/cover") {
    if (p == "OPEN") WindowPosition::openFull();
    else if (p == "CLOSE") WindowPosition::closeFull();
    else if (p == "STOP") WindowPosition::stop();
  } else if (t == baseT + "/cmd/position") {
    long v = p.toInt();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    WindowPosition::goTo((uint8_t)v);
  } else if (t == baseT + "/cmd/replay") {
```

- [ ] **Step 4: Rewrite `publishCoverState`**

Replace:
```cpp
void HaMqtt::publishCoverState() {
  if (!enabled || !mq.connected()) return;
  if (WindowSim::get() == WindowSim::State::Unknown) return;
  mq.publish((baseT + "/cover/state").c_str(), WindowSim::name(), true);
}
```
with:
```cpp
void HaMqtt::publishCoverState() {
  if (!enabled || !mq.connected()) return;
  uint8_t pos = WindowPosition::position();
  mq.publish((baseT + "/cover/position").c_str(), String(pos).c_str(), true);
  WindowModel::Motion m = WindowPosition::motion();
  const char* st = m == WindowModel::Motion::Opening ? "opening"
                 : m == WindowModel::Motion::Closing ? "closing"
                 : pos <= 1 ? "closed" : "open";
  mq.publish((baseT + "/cover/state").c_str(), st, true);
}
```

- [ ] **Step 5: Commit**

```bash
git add src/mqtt/HaMqtt.cpp
git commit -m "feat: MQTT positionable HA cover (position + set_position + stop)"
```

---

## Task 8: Portal API

**Files:**
- Modify: `src/web/Portal.cpp`

- [ ] **Step 1: Swap the include**

Replace:
```cpp
#include "../state/WindowSim.h"
```
with:
```cpp
#include "../state/WindowPosition.h"
```

- [ ] **Step 2: Update `/api/status` window fields**

In the `/api/status` handler, replace:
```cpp
    doc["window"] = WindowSim::name();
```
with:
```cpp
    doc["window"] = WindowPosition::motionName();
    doc["pos"] = WindowPosition::position();
```

- [ ] **Step 3: Add window control + calibration endpoints**

In `registerApi()`, immediately after the `/api/homekit` GET handler block, add:

```cpp
  server.on("/api/window", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["pos"] = WindowPosition::position();
    doc["motion"] = WindowPosition::motionName();
    doc["target"] = WindowPosition::target();
    doc["openMs"] = WindowPosition::openMs();
    doc["closeMs"] = WindowPosition::closeMs();
    doc["stopLeadMs"] = WindowPosition::stopLeadMs();
    sendJson(req, doc);
  });

  auto* windowH = new AsyncCallbackJsonWebHandler("/api/window", [](AsyncWebServerRequest* req, JsonVariant& json) {
    JsonObject o = json.as<JsonObject>();
    String a = (const char*)(o["action"] | "");
    if (a == "open") WindowPosition::openFull();
    else if (a == "close") WindowPosition::closeFull();
    else if (a == "stop") WindowPosition::stop();
    else if (a == "goto") {
      int v = o["value"] | -1;
      if (v < 0 || v > 100) return sendErr(req, "value 0-100 required");
      WindowPosition::goTo((uint8_t)v);
    } else return sendErr(req, "unknown action");
    sendOk(req);
  });
  windowH->setMethod(HTTP_POST);
  server.addHandler(windowH);

  auto* calibH = new AsyncCallbackJsonWebHandler("/api/window/calibrate", [](AsyncWebServerRequest* req, JsonVariant& json) {
    JsonObject o = json.as<JsonObject>();
    uint32_t om = o["openMs"] | 0u, cm = o["closeMs"] | 0u, sl = o["stopLeadMs"] | 0u;
    if (om < 1000 || cm < 1000) return sendErr(req, "openMs/closeMs too small");
    WindowPosition::setCalibration(om, cm, sl);
    sendOk(req);
  });
  calibH->setMethod(HTTP_POST);
  server.addHandler(calibH);
```

- [ ] **Step 4: Notify the model on direct open/close replays**

In the `/api/replay` handler, in the `id` branch, replace:
```cpp
      RFTransmitter::requestSend(*c);
      return sendOk(req);
```
with:
```cpp
      RFTransmitter::requestSend(*c);
      if (c->name.equalsIgnoreCase("open")) WindowPosition::noteExternalCommand(true);
      else if (c->name.equalsIgnoreCase("close")) WindowPosition::noteExternalCommand(false);
      return sendOk(req);
```
And at the end of the same handler (inline-signal branch), replace:
```cpp
    RFTransmitter::requestSend(c);
    sendOk(req);
```
with:
```cpp
    RFTransmitter::requestSend(c);
    if (c.name.equalsIgnoreCase("open")) WindowPosition::noteExternalCommand(true);
    else if (c.name.equalsIgnoreCase("close")) WindowPosition::noteExternalCommand(false);
    sendOk(req);
```

- [ ] **Step 5: Build the firmware (full tree should now compile)**

```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" run -e lolin_c3_mini
```
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/web/Portal.cpp
git commit -m "feat: /api/window control + calibration endpoints; notify model on replay"
```

---

## Task 9: Web portal Window tab

**Files:**
- Modify: `data/index.html`
- Modify: `data/app.js`
- Modify: `data/style.css`

- [ ] **Step 1: Add the nav button and section in `data/index.html`**

In `<nav>`, add as the first tab (and remove `active` from the Sniffer tab):
```html
  <button class="tab active" data-tab="window">Window</button>
```
Change the existing Sniffer button to not be active:
```html
  <button class="tab" data-tab="sniffer">Sniffer</button>
```
Then add this section as the first child inside `<main>` (before `#tab-sniffer`), and remove `active` from the `#tab-sniffer` section's class:
```html
<section id="tab-window" class="tabpane active">
  <div class="card">
    <h2>Window</h2>
    <div class="posbar"><div id="posFill" class="posfill"></div></div>
    <p id="winState" class="muted">—</p>
    <label>Target position <input id="winSlider" type="range" min="0" max="100" value="0"></label>
    <div class="row">
      <button id="winOpen">Open</button>
      <button id="winClose">Close</button>
      <button id="winStop" class="ghost">Stop</button>
    </div>
  </div>
  <div class="card">
    <h2>Calibration</h2>
    <p class="muted">Time for a full travel in each direction. Used to estimate position.</p>
    <form id="calForm">
      <label>Open time (s) <input name="openS" type="number" step="0.1" min="1" value="23.5"></label>
      <label>Close time (s) <input name="closeS" type="number" step="0.1" min="1" value="27"></label>
      <label>Stop lead (ms) <input name="stopLeadMs" type="number" min="0" value="800"></label>
      <button>Save calibration</button>
    </form>
  </div>
</section>
```
Change `#tab-sniffer` opening tag from:
```html
<section id="tab-sniffer" class="tabpane active">
```
to:
```html
<section id="tab-sniffer" class="tabpane">
```

- [ ] **Step 2: Add styles in `data/style.css`**

Append:
```css
.posbar { height: 14px; border-radius: 7px; background: var(--card, #1c2333); overflow: hidden; border: 1px solid rgba(255,255,255,.12); }
.posfill { height: 100%; width: 0%; background: var(--green, #38c172); transition: width .4s ease; }
#winSlider { width: 100%; }
```

- [ ] **Step 3: Add Window logic in `data/app.js`**

Add near the top, after the `// ---- tabs` block (the tab click handler), extend the tab handler to load the window on open. Replace:
```javascript
    if (b.dataset.tab === 'settings') scanWifi();  // results ready by the time you look
```
with:
```javascript
    if (b.dataset.tab === 'settings') scanWifi();  // results ready by the time you look
    if (b.dataset.tab === 'window') loadWindow();
```

In `wsConnect()`, replace the `ws.onmessage` handler:
```javascript
  ws.onmessage = e => {
    try { addEvent(JSON.parse(e.data)); } catch {}
  };
```
with:
```javascript
  ws.onmessage = e => {
    try {
      const m = JSON.parse(e.data);
      if (m.type === 'window') updateWindow(m);
      else addEvent(m);
    } catch {}
  };
```

Add this new section (e.g. after the `// ---- receiver mode` block):
```javascript
// ---- window
function describeWindow(pos, motion) {
  if (motion === 'opening') return `Opening… ${pos}%`;
  if (motion === 'closing') return `Closing… ${pos}%`;
  if (pos >= 99) return 'Open';
  if (pos <= 1) return 'Closed';
  return `Partly open ${pos}%`;
}
function updateWindow(m) {
  $('#posFill').style.width = m.pos + '%';
  $('#winState').textContent = describeWindow(m.pos, m.motion);
  if (document.activeElement !== $('#winSlider')) $('#winSlider').value = m.pos;
}
async function loadWindow() {
  const w = await api('/api/window').catch(() => null);
  if (!w) return;
  updateWindow(w);
  $('#calForm [name=openS]').value = (w.openMs / 1000).toFixed(1);
  $('#calForm [name=closeS]').value = (w.closeMs / 1000).toFixed(1);
  $('#calForm [name=stopLeadMs]').value = w.stopLeadMs;
}
$('#winOpen').onclick = () => post('/api/window', { action: 'open' }).catch(e => toast('Error: ' + e.message));
$('#winClose').onclick = () => post('/api/window', { action: 'close' }).catch(e => toast('Error: ' + e.message));
$('#winStop').onclick = () => post('/api/window', { action: 'stop' }).catch(e => toast('Error: ' + e.message));
$('#winSlider').addEventListener('change', e =>
  post('/api/window', { action: 'goto', value: +e.target.value }).catch(err => toast('Error: ' + err.message)));
$('#calForm').addEventListener('submit', e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  post('/api/window/calibrate', {
    openMs: Math.round(+fd.get('openS') * 1000),
    closeMs: Math.round(+fd.get('closeS') * 1000),
    stopLeadMs: +fd.get('stopLeadMs'),
  }).then(() => toast('Calibration saved')).catch(err => toast('Error: ' + err.message));
});
```

Update the stats row in `refreshStats()`. Replace:
```javascript
    ['Window (simulated)', s.window || 'unknown'],
```
with:
```javascript
    ['Window', (s.pos != null ? s.pos + '% · ' : '') + (s.window || 'idle')],
```

At the bottom of the file, add `loadWindow()` to the startup calls (after `refreshStats();`):
```javascript
loadWindow();
```

- [ ] **Step 4: Build the filesystem image to confirm assets are valid**

```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" run -e lolin_c3_mini -t buildfs
```
Expected: SUCCESS (LittleFS image built from `data/`).

- [ ] **Step 5: Commit**

```bash
git add data/index.html data/app.js data/style.css
git commit -m "feat: web portal Window tab with live position, controls, calibration"
```

---

## Task 10: Flash + on-device verification (manual, with the user)

**Files:** none (verification only).

- [ ] **Step 1: Re-run host unit tests (regression)**

```bash
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" test -e native
```
Expected: PASS (14 tests).

- [ ] **Step 2: Flash firmware and filesystem over USB**

Confirm the device port (e.g. `/dev/cu.usbmodem*`), then:
```bash
"$PIO" run -e lolin_c3_mini -t upload
"$PIO" run -e lolin_c3_mini -t uploadfs
```
Expected: both succeed; device reboots.

- [ ] **Step 3: Verify the portal Window tab**

Open `http://windowctl.local/` (or the device IP). On the Window tab:
- Press **Open** → window travels fully open; bar animates 0→100%; state shows "Opening…" then "Open". Confirms issue-1 fix (reliable full travel).
- Press **Close** → travels fully closed; bar animates to 0%.
- Drag the **target slider** to ~50% → window stops near 50%; note the actual physical stop point.

- [ ] **Step 4: Tune `stopLeadMs`**

If the window overshoots the target (stops late), increase **Stop lead (ms)** in Calibration and save; if it stops short, decrease it. Re-test a mid-travel target until the physical stop matches the requested %.

- [ ] **Step 5: Verify Apple Home**

In the Home app, the Window accessory slider should animate during travel and settle at the chosen %; dragging it drives the window. Verify open, close, and an intermediate target.

- [ ] **Step 6: Verify MQTT/Home Assistant (if a broker is configured)**

In Home Assistant the cover should show a position %, accept the position slider, and respond to open/close/stop. (Skip if no broker configured.)

- [ ] **Step 7: Final commit (record tuned defaults if changed)**

If you changed the default `stopLeadMs` in `Storage.cpp` to match measured latency, commit it:
```bash
git add -A
git commit -m "chore: set measured stopLead default after on-device tuning"
```
Otherwise no commit is needed (calibration lives in NVS).

---

## Self-Review

**Spec coverage:** timed estimator (Tasks 1,4) ✓; stop-at-target + reverse-direction-from-rest (Task 2) ✓; full-end run-to-limit self-calibration (Task 2) ✓; issue-1 re-assertion (Task 4) ✓; HomeKit animated/positionable cover (Task 6) ✓; portal Window tab + calibration (Tasks 8,9) ✓; MQTT positionable cover (Task 7) ✓; calibration/last-pos persistence (Task 3) ✓; WindowSim removal + LED states (Tasks 4,5) ✓; host unit tests (Tasks 1,2,10) ✓.

**Type consistency:** `WindowModel::Motion` / `Command` / `Step` used identically across Tasks 1,2,4,6,7. `WindowPosition` API (`goTo`,`openFull`,`closeFull`,`stop`,`noteExternalCommand`,`position`,`motion`,`target`,`motionName`,`setCalibration`,`openMs`,`closeMs`,`stopLeadMs`) defined in Task 4 and consumed consistently in Tasks 6–9. `Storage` calibration getters/setters match between Task 3 and Task 4. HomeSpan `getVal<int>()` used for typed reads.

**Placeholders:** none — every code step contains complete content. The single deferred line (HomeKit notify hook in Task 4) is explicitly resolved in Task 6 Step 4.
