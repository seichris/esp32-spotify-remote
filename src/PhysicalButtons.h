#pragma once

#include <Arduino.h>

enum class PhysicalButton {
  kNone,
  kUpper,
  kLower,
};

class PhysicalButtons {
 public:
  // Initializes the BOOT GPIO and the AXP2101 power-key interrupt path.
  // The BOOT GPIO remains usable even if the PMU is not present on I2C.
  bool begin();

  // Returns one button press event. Events are emitted once on press, after
  // debounce; holding a key does not repeat playback commands.
  bool poll(PhysicalButton& button);

  bool pmuAvailable() const { return pmu_available_; }

 private:
  bool readRegister(uint8_t reg, uint8_t& value);
  bool writeRegister(uint8_t reg, uint8_t value);

  bool pmu_available_ = false;
  bool upper_stable_pressed_ = false;
  bool upper_candidate_pressed_ = false;
  uint32_t upper_candidate_since_ms_ = 0;
};
