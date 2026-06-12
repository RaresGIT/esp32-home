#include "RFTransmitter.h"
#include "../config.h"
#include <RCSwitch.h>
#include <deque>
#include <mutex>

namespace {
RCSwitch rc;
std::deque<RfCode> jobs;
std::mutex mtx;
uint32_t txCnt = 0;

void sendDecoded(const RfCode& c) {
  rc.setProtocol(c.protocol ? c.protocol : 1);
  if (c.pulse) rc.setPulseLength(c.pulse);
  rc.setRepeatTransmit(c.repeats ? c.repeats : DEFAULT_REPEATS);
  rc.send(c.code, c.bits ? c.bits : 24);
}

void sendRaw(const RfCode& c) {
  uint8_t reps = c.repeats ? c.repeats : DEFAULT_REPEATS;
  for (uint8_t r = 0; r < reps; r++) {
    bool level = true;  // captured frames always start with a high period
    for (uint16_t t : c.timings) {
      digitalWrite(PIN_RF_TX, level ? HIGH : LOW);
      delayMicroseconds(t);
      level = !level;
    }
    digitalWrite(PIN_RF_TX, LOW);
    delayMicroseconds(RAW_FRAME_GAP_US);
  }
}
}

void RFTransmitter::begin() {
  pinMode(PIN_RF_TX, OUTPUT);
  digitalWrite(PIN_RF_TX, LOW);
  rc.enableTransmit(PIN_RF_TX);
}

void RFTransmitter::requestSend(const RfCode& code) {
  std::lock_guard<std::mutex> lock(mtx);
  if (jobs.size() < 8) jobs.push_back(code);
}

bool RFTransmitter::loop(String& sentName) {
  RfCode job;
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (jobs.empty()) return false;
    job = jobs.front();
    jobs.pop_front();
  }
  if (job.raw) sendRaw(job);
  else sendDecoded(job);
  txCnt++;
  sentName = job.name.length() ? job.name : (job.raw ? "raw signal" : "code " + String(job.code));
  return true;
}

uint32_t RFTransmitter::txCount() { return txCnt; }
