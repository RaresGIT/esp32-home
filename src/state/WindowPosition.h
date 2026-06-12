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
