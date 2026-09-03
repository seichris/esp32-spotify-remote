#pragma once

#include <Arduino.h>
#include <SensorQMI8658.hpp>

class OrientationSensor {
 public:
  bool begin();
  bool poll(uint8_t& rotation);

 private:
  bool classify(float x, float y, uint8_t& rotation) const;

  SensorQMI8658 sensor_;
  uint8_t current_rotation_ = 0;
  uint8_t candidate_rotation_ = 0;
  uint32_t candidate_since_ms_ = 0;
  uint32_t next_sample_ms_ = 0;
  bool candidate_valid_ = false;
  bool ready_ = false;
};
