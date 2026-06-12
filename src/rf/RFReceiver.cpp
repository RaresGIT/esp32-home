#include "RFReceiver.h"
#include "../config.h"
#include <RCSwitch.h>

namespace {
RCSwitch rc;
RFReceiver::Mode curMode = RFReceiver::Mode::Decoded;
volatile int pendingMode = -1;
uint32_t rxCnt = 0;

volatile uint32_t lastEdgeUs = 0;
volatile uint16_t rawBuf[RAW_MAX_EDGES];
volatile size_t rawLen = 0;
volatile bool rawReady = false;

// Records pulse durations between edges. A long silence ends the frame;
// the edge that ends a silence is a rising edge, so rawBuf[0] is always
// a high period — raw replay relies on that.
void IRAM_ATTR onEdge() {
  uint32_t now = micros();
  uint32_t d = now - lastEdgeUs;
  lastEdgeUs = now;
  if (rawReady) return;  // previous frame not consumed yet
  if (d > RAW_GAP_US) {
    if (rawLen >= RAW_MIN_EDGES) rawReady = true;
    else rawLen = 0;
    return;
  }
  if (d < RAW_MIN_PULSE_US) {  // glitch — restart frame
    rawLen = 0;
    return;
  }
  if (rawLen < RAW_MAX_EDGES) rawBuf[rawLen++] = (uint16_t)d;
  else rawReady = true;
}

void applyMode(RFReceiver::Mode m) {
  if (m == curMode) return;
  if (curMode == RFReceiver::Mode::Decoded) rc.disableReceive();
  else detachInterrupt(digitalPinToInterrupt(PIN_RF_RX));
  curMode = m;
  if (m == RFReceiver::Mode::Decoded) {
    rc.enableReceive(digitalPinToInterrupt(PIN_RF_RX));
  } else {
    rawLen = 0;
    rawReady = false;
    lastEdgeUs = micros();
    attachInterrupt(digitalPinToInterrupt(PIN_RF_RX), onEdge, CHANGE);
  }
}
}

void RFReceiver::begin() {
  pinMode(PIN_RF_RX, INPUT);
  rc.enableReceive(digitalPinToInterrupt(PIN_RF_RX));
}

void RFReceiver::loop() {
  int p = pendingMode;
  if (p >= 0) {
    pendingMode = -1;
    applyMode((Mode)p);
  }
}

void RFReceiver::requestMode(Mode m) { pendingMode = (int)m; }
RFReceiver::Mode RFReceiver::mode() { return curMode; }
uint32_t RFReceiver::rxCount() { return rxCnt; }

bool RFReceiver::readDecoded(Decoded& out) {
  if (curMode != Mode::Decoded || !rc.available()) return false;
  out.code = rc.getReceivedValue();
  out.bits = (uint8_t)rc.getReceivedBitlength();
  out.protocol = (uint8_t)rc.getReceivedProtocol();
  out.pulse = (uint16_t)rc.getReceivedDelay();
  rc.resetAvailable();
  rxCnt++;
  return true;
}

size_t RFReceiver::readRaw(uint16_t* out, size_t maxLen) {
  if (curMode != Mode::Raw || !rawReady) return 0;
  size_t n = rawLen < maxLen ? rawLen : maxLen;
  for (size_t i = 0; i < n; i++) out[i] = rawBuf[i];
  rawLen = 0;
  rawReady = false;
  rxCnt++;
  return n;
}
