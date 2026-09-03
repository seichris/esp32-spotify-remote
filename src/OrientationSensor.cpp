#include "OrientationSensor.h"

#include <Wire.h>
#include <math.h>

#include "Config.h"

namespace {
constexpr uint8_t kQmi8658Address = 0x6B;
constexpr float kMinimumGravityG = 0.62f;
constexpr float kDominanceMarginG = 0.12f;
}  // namespace

bool OrientationSensor::begin() {
  // Touch has already initialized the shared GPIO14/GPIO15 I2C bus.
  if (!sensor_.begin(Wire, kQmi8658Address)) {
    return false;
  }
  sensor_.configAccelerometer(SensorQMI8658::ACC_RANGE_2G,
                              SensorQMI8658::ACC_ODR_62_5Hz,
                              SensorQMI8658::LPF_MODE_0);
  ready_ = sensor_.enableAccelerometer();
  next_sample_ms_ = millis();
  return ready_;
}

bool OrientationSensor::poll(uint8_t& rotation) {
  if (!ready_) {
    return false;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_sample_ms_) < 0) {
    return false;
  }
  next_sample_ms_ = now + Config::kOrientationPollMs;

  if (!sensor_.getDataReady()) {
    return false;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!sensor_.getAccelerometer(x, y, z)) {
    return false;
  }

  uint8_t next_rotation = current_rotation_;
  if (!classify(x, y, next_rotation)) {
    candidate_valid_ = false;
    return false;
  }

  if (!candidate_valid_ || next_rotation != candidate_rotation_) {
    candidate_rotation_ = next_rotation;
    candidate_since_ms_ = now;
    candidate_valid_ = true;
    return false;
  }

  if (candidate_rotation_ == current_rotation_ ||
      now - candidate_since_ms_ < Config::kOrientationSettleMs) {
    return false;
  }

  current_rotation_ = candidate_rotation_;
  rotation = current_rotation_;
  return true;
}

bool OrientationSensor::classify(float x, float y, uint8_t& rotation) const {
  const float abs_x = fabsf(x);
  const float abs_y = fabsf(y);
  const float dominant = max(abs_x, abs_y);
  const float secondary = min(abs_x, abs_y);
  if (dominant < kMinimumGravityG ||
      dominant - secondary < kDominanceMarginG) {
    return false;
  }

  // Physical-device calibration for the 2.06-inch board. The native portrait
  // bottom is +X; the USB/buttons long edge is -Y. A static accelerometer
  // reports support force opposite gravity, so map the dominant axis directly
  // to the display edge that is physically down.
  if (abs_x > abs_y) {
    rotation = x > 0.0f ? 0 : 2;
  } else {
    rotation = y > 0.0f ? 1 : 3;
  }
  return true;
}
