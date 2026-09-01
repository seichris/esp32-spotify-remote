#include "PhysicalButtons.h"

#include <Wire.h>

namespace {
constexpr uint8_t kUpperButtonPin = 0;  // BOOT, active low.

// The lower PWR key is connected to AXP2101 PWRON, not to an ESP32 GPIO.
// AXP2101's PKEY short-press event is INTSTS2 bit 3 (register 0x49).
constexpr uint8_t kPmuAddress = 0x34;
constexpr uint8_t kPmuInterruptEnable2 = 0x41;
constexpr uint8_t kPmuInterruptStatus2 = 0x49;
constexpr uint8_t kPmuPkeyShortMask = 0x08;
constexpr uint32_t kDebounceMs = 35;
}  // namespace

bool PhysicalButtons::begin() {
  pinMode(kUpperButtonPin, INPUT_PULLUP);
  const bool pressed = digitalRead(kUpperButtonPin) == LOW;
  upper_stable_pressed_ = pressed;
  upper_candidate_pressed_ = pressed;
  upper_candidate_since_ms_ = millis();

  // Probe the PMU on the same I2C bus used by the touch controller. The board
  // exposes the PWR key only through the AXP2101 PWRON input, so there is no
  // second ESP32 GPIO to read here.
  Wire.beginTransmission(kPmuAddress);
  if (Wire.endTransmission(true) != 0) {
    return true;
  }

  // Clear a stale press before enabling this one interrupt source. The
  // register is write-one-to-clear, so unrelated status bits are untouched.
  writeRegister(kPmuInterruptStatus2, kPmuPkeyShortMask);

  uint8_t interrupt_enable = 0;
  if (!readRegister(kPmuInterruptEnable2, interrupt_enable) ||
      !writeRegister(kPmuInterruptEnable2,
                     interrupt_enable | kPmuPkeyShortMask)) {
    return true;
  }

  // Avoid turning a press that happened during setup into a command.
  writeRegister(kPmuInterruptStatus2, kPmuPkeyShortMask);
  pmu_available_ = true;
  return true;
}

bool PhysicalButtons::poll(PhysicalButton& button) {
  button = PhysicalButton::kNone;
  const uint32_t now = millis();

  const bool upper_pressed = digitalRead(kUpperButtonPin) == LOW;
  if (upper_pressed != upper_candidate_pressed_) {
    upper_candidate_pressed_ = upper_pressed;
    upper_candidate_since_ms_ = now;
  }
  if (upper_candidate_pressed_ != upper_stable_pressed_ &&
      static_cast<uint32_t>(now - upper_candidate_since_ms_) >=
          kDebounceMs) {
    upper_stable_pressed_ = upper_candidate_pressed_;
    if (upper_stable_pressed_) {
      button = PhysicalButton::kUpper;
      return true;
    }
  }

  if (!pmu_available_) {
    return false;
  }

  uint8_t status = 0;
  if (!readRegister(kPmuInterruptStatus2, status) ||
      (status & kPmuPkeyShortMask) == 0) {
    return false;
  }

  // AXP2101 status bits are sticky until explicitly cleared. Clear only the
  // short power-key bit, leaving any other PMU status for its owner.
  writeRegister(kPmuInterruptStatus2, kPmuPkeyShortMask);
  button = PhysicalButton::kLower;
  return true;
}

bool PhysicalButtons::readRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(kPmuAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(kPmuAddress, 1, true);
  if (received != 1 || !Wire.available()) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  value = static_cast<uint8_t>(Wire.read());
  return true;
}

bool PhysicalButtons::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPmuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}
