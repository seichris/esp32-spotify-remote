#include "BoardDisplay.h"

#include <new>

namespace {
constexpr int8_t kLcdSdio0 = 4;
constexpr int8_t kLcdSdio1 = 5;
constexpr int8_t kLcdSdio2 = 6;
constexpr int8_t kLcdSdio3 = 7;
constexpr int8_t kLcdReset = 8;
constexpr int8_t kLcdSclk = 11;
constexpr int8_t kLcdCs = 12;
constexpr uint8_t kColumnOffset = 22;
}  // namespace

bool BoardDisplay::begin() {
  if (panel_ != nullptr) {
    return true;
  }

  bus_ = new (std::nothrow) Arduino_ESP32QSPI(
      kLcdCs, kLcdSclk, kLcdSdio0, kLcdSdio1, kLcdSdio2, kLcdSdio3);
  if (bus_ == nullptr) {
    return false;
  }

  panel_ = new (std::nothrow) Arduino_CO5300(
      bus_, kLcdReset, 0, kWidth, kHeight, kColumnOffset, 0, 0, 0);
  if (panel_ == nullptr) {
    delete bus_;
    bus_ = nullptr;
    return false;
  }

  if (!panel_->begin()) {
    return false;
  }

  panel_->setRotation(0);
  panel_->fillScreen(RGB565_BLACK);
  panel_->setBrightness(210);
  return true;
}

Arduino_CO5300& BoardDisplay::gfx() { return *panel_; }

void BoardDisplay::setBrightness(uint8_t value) {
  if (panel_ != nullptr) {
    panel_->setBrightness(value);
  }
}

void BoardDisplay::sleep() {
  if (panel_ != nullptr) {
    panel_->displayOff();
  }
}

void BoardDisplay::wake() {
  if (panel_ != nullptr) {
    panel_->displayOn();
  }
}
