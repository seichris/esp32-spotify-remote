#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class BoardDisplay {
 public:
  static constexpr int16_t kWidth = 410;
  static constexpr int16_t kHeight = 502;

  BoardDisplay() = default;
  BoardDisplay(const BoardDisplay&) = delete;
  BoardDisplay& operator=(const BoardDisplay&) = delete;

  bool begin();
  Arduino_GFX& gfx();
  void setRotation(uint8_t rotation);
  uint8_t rotation() const { return rotation_; }
  int16_t width() const;
  int16_t height() const;
  void setBrightness(uint8_t value);
  void sleep();
  void wake();

 private:
  Arduino_DataBus* bus_ = nullptr;
  Arduino_CO5300* panel_ = nullptr;
  Arduino_GFX* surface_ = nullptr;
  uint8_t rotation_ = 0;
};
