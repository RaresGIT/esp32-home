#pragma once
#include <Arduino.h>
#include "../storage/Codes.h"

// 433MHz transmit engine. Sends are queued and executed from the main loop
// so web/MQTT handlers (which run in other FreeRTOS tasks) never bit-bang
// the pin themselves — that would jitter the timing.
namespace RFTransmitter {
void begin();
void requestSend(const RfCode& code);
bool loop(String& sentName);  // sends one queued job; true if something was sent
uint32_t txCount();
}
