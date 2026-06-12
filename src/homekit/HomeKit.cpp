#include "HomeKit.h"
#include "HomeSpan.h"
#include "../config.h"
#include "../storage/Codes.h"
#include "../rf/RFTransmitter.h"
#include "../state/WindowPosition.h"

namespace {
constexpr char PAIRING_CODE[] = "47102386";
constexpr char QR_ID[] = "WNDW";
constexpr uint8_t CATEGORY_WINDOW = 13;  // HAP accessory category
constexpr uint8_t FLAG_IP = 2;           // HAP pairing flag: IP transport

bool isActive = false;

// Positionable window: the slider reports any 0–100 value. Commands are
// forwarded to WindowPosition::goTo; live position is pushed back via
// HomeKit::notifyPosition called from WindowPosition::notifySinks.
struct WindowService : Service::Window {
  SpanCharacteristic* cur;
  SpanCharacteristic* target;
  SpanCharacteristic* posState;

  WindowService() : Service::Window() {
    cur = new Characteristic::CurrentPosition(0);
    target = new Characteristic::TargetPosition(0);
    target->setRange(0, 100, 1);
    posState = new Characteristic::PositionState(2);  // stopped
  }

  boolean update() override {
    WindowPosition::goTo((uint8_t)target->getNewVal());
    return true;
  }

  void loop() override {}  // position is driven by WindowPosition via notifyPosition()
};

WindowService* win = nullptr;
}

void HomeKit::begin(const String& deviceId, const String& ssid, const String& pass) {
  homeSpan.setPortNum(1201);       // keep port 80 for the dashboard
  homeSpan.setHostNameSuffix("");  // hostname stays exactly "windowctl"
  homeSpan.setQRID(QR_ID);
  homeSpan.setPairingCode(PAIRING_CODE);
  homeSpan.setSketchVersion(FW_VERSION);
  homeSpan.setWifiCredentials(ssid.c_str(), pass.c_str());
  homeSpan.setWifiCallback([]() {
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // C3 Mini antenna quirk, see startAp()
    Serial.printf("HomeKit: WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
  });
  homeSpan.begin(Category::Windows, "Window", "windowctl");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Window");
  new Characteristic::Manufacturer("DIY");
  new Characteristic::Model("C3 Mini RF Bridge");
  new Characteristic::SerialNumber(deviceId.c_str());
  new Characteristic::FirmwareRevision(FW_VERSION);
  win = new WindowService();

  isActive = true;
}

void HomeKit::poll() {
  if (isActive) homeSpan.poll();
}

bool HomeKit::active() { return isActive; }

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

// If the accessory is removed from the Home app while the device is offline,
// the unpair never reaches it and stored controller data blocks re-pairing
// (Home app hangs on "Connecting"). 'U' deletes all controller data.
void HomeKit::resetPairing() {
  if (isActive) homeSpan.processSerialCommand("U");
}

const char* HomeKit::pairingCode() { return PAIRING_CODE; }

// HAP setup payload: 45-bit value, base36-encoded to 9 chars, plus setup ID.
// Layout: bits 0-26 setup code, 27-30 flags, 31-38 category, 39-44 zero.
String HomeKit::setupUri() {
  uint64_t payload = 0;
  payload |= (uint64_t)CATEGORY_WINDOW << 31;
  payload |= (uint64_t)FLAG_IP << 27;
  payload |= (uint64_t)strtoul(PAIRING_CODE, nullptr, 10) & 0x7FFFFFF;
  const char* alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char buf[10];
  for (int i = 8; i >= 0; i--) {
    buf[i] = alphabet[payload % 36];
    payload /= 36;
  }
  buf[9] = '\0';
  return String("X-HM://") + buf + QR_ID;
}
