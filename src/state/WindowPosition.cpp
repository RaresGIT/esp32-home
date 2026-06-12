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
