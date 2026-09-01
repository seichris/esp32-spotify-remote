#include "TouchController.h"

namespace {
constexpr uint8_t kAddress = 0x38;
constexpr int8_t kSda = 15;
constexpr int8_t kScl = 14;
constexpr int8_t kInterrupt = 38;
constexpr int8_t kReset = 9;
constexpr uint8_t kFingerCountRegister = 0x02;
constexpr uint8_t kDeviceIdRegister = 0xA0;
constexpr uint8_t kPowerModeRegister = 0xA5;
constexpr int16_t kDisplayWidth = 410;
constexpr int16_t kDisplayHeight = 502;
}  // namespace

bool TouchController::begin() {
  pinMode(kInterrupt, INPUT_PULLUP);
  pinMode(kReset, OUTPUT);
  digitalWrite(kReset, HIGH);
  delay(1);
  digitalWrite(kReset, LOW);
  delay(20);
  digitalWrite(kReset, HIGH);
  delay(60);

  Wire.begin(kSda, kScl, 400000);

  uint8_t id = 0;
  if (!readRegisters(kDeviceIdRegister, &id, 1)) {
    return false;
  }
  device_id_ = id;

  // Active mode is more reliable for a simple polling UI than monitor mode.
  writeRegister(kPowerModeRegister, 0x00);
  return true;
}

bool TouchController::read(TouchPoint& point) {
  uint8_t data[5] = {};
  if (!readRegisters(kFingerCountRegister, data, sizeof(data))) {
    return false;
  }

  const uint8_t fingers = data[0] & 0x0F;
  if (fingers == 0 || fingers > 2) {
    return false;
  }

  const int16_t x = static_cast<int16_t>(((data[1] & 0x0F) << 8) | data[2]);
  const int16_t y = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
  if (x < 0 || y < 0 || x >= kDisplayWidth || y >= kDisplayHeight) {
    return false;
  }

  point.x = x;
  point.y = y;
  return true;
}

bool TouchController::readRegisters(uint8_t reg, uint8_t* output, size_t length) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(kAddress, length, true);
  if (received != length) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    output[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool TouchController::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}
