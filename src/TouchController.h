#pragma once

#include <Arduino.h>
#include <Wire.h>

struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
};

class TouchController {
 public:
  bool begin();
  bool read(TouchPoint& point);
  uint8_t deviceId() const { return device_id_; }

 private:
  bool readRegisters(uint8_t reg, uint8_t* output, size_t length);
  bool writeRegister(uint8_t reg, uint8_t value);

  uint8_t device_id_ = 0xFF;
};
