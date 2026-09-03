#include "BoardDisplay.h"

#include <esp_heap_caps.h>
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

// CO5300 supports axis flips but not row/column exchange. This adapter keeps
// the panel in its native 410 x 502 address space and maps logical drawing
// operations into it, including the two true landscape orientations.
class RotatedGfx : public Arduino_GFX {
 public:
  explicit RotatedGfx(Arduino_CO5300& panel)
      : Arduino_GFX(BoardDisplay::kWidth, BoardDisplay::kHeight), panel_(panel) {}

  bool begin(int32_t = GFX_NOT_DEFINED) override { return true; }

  void setRotation(uint8_t rotation) override {
    Arduino_GFX::setRotation(rotation & 3);
  }

  void startWrite() override { panel_.startWrite(); }
  void endWrite() override { panel_.endWrite(); }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    int16_t panel_x = 0;
    int16_t panel_y = 0;
    mapPoint(x, y, panel_x, panel_y);
    panel_.writePixelPreclipped(panel_x, panel_y, color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h,
                      uint16_t color) override {
    writeFillRectPreclipped(x, y, 1, h, color);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w,
                      uint16_t color) override {
    writeFillRectPreclipped(x, y, w, 1, color);
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) override {
    switch (_rotation) {
      case 1:
        panel_.writeFillRectPreclipped(BoardDisplay::kWidth - y - h, x, h, w,
                                       color);
        break;
      case 2:
        panel_.writeFillRectPreclipped(BoardDisplay::kWidth - x - w,
                                       BoardDisplay::kHeight - y - h, w, h,
                                       color);
        break;
      case 3:
        panel_.writeFillRectPreclipped(y, BoardDisplay::kHeight - x - w, h, w,
                                       color);
        break;
      default:
        panel_.writeFillRectPreclipped(x, y, w, h, color);
        break;
    }
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w,
                          int16_t h) override {
    drawRgbBitmap(x, y, bitmap, w, h);
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t* bitmap,
                          int16_t w, int16_t h) override {
    drawRgbBitmap(x, y, bitmap, w, h);
  }

 private:
  template <typename PixelPointer>
  void drawRgbBitmap(int16_t x, int16_t y, PixelPointer bitmap, int16_t w,
                     int16_t h) {
    if (_rotation == 0) {
      panel_.draw16bitRGBBitmap(x, y, bitmap, w, h);
      return;
    }

    // The CO5300 reliably streams RGB pixels through ordinary multi-row
    // windows, but not through the one-row/one-column windows that a naive
    // software rotation produces. Build one transformed rectangle in PSRAM,
    // then use the exact same panel path as native rotation.
    const int16_t output_width = (_rotation & 1) ? h : w;
    const int16_t output_height = (_rotation & 1) ? w : h;
    const size_t pixel_count = static_cast<size_t>(w) * h;
    uint16_t* transformed = static_cast<uint16_t*>(heap_caps_malloc(
        pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (transformed == nullptr) {
      // Allocation failure should degrade drawing speed, not remove the title
      // or artwork. This path uses the primitive that already renders text and
      // shapes correctly in every orientation.
      panel_.startWrite();
      for (int16_t row = 0; row < h; ++row) {
        for (int16_t column = 0; column < w; ++column) {
          int16_t panel_x = 0;
          int16_t panel_y = 0;
          mapPoint(x + column, y + row, panel_x, panel_y);
          panel_.writePixelPreclipped(
              panel_x, panel_y,
              bitmap[static_cast<int32_t>(row) * w + column]);
        }
      }
      panel_.endWrite();
      return;
    }

    for (int16_t row = 0; row < h; ++row) {
      for (int16_t column = 0; column < w; ++column) {
        const uint16_t color =
            bitmap[static_cast<int32_t>(row) * w + column];
        int32_t destination = 0;
        if (_rotation == 1) {
          destination = static_cast<int32_t>(column) * output_width +
                        (h - row - 1);
        } else if (_rotation == 2) {
          destination = static_cast<int32_t>(h - row - 1) * output_width +
                        (w - column - 1);
        } else {
          destination = static_cast<int32_t>(w - column - 1) * output_width +
                        row;
        }
        transformed[destination] = color;
      }
    }

    int16_t panel_x = 0;
    int16_t panel_y = 0;
    mapPoint(x, y, panel_x, panel_y);
    if (_rotation == 1) {
      panel_x -= h - 1;
    } else if (_rotation == 2) {
      panel_x -= w - 1;
      panel_y -= h - 1;
    } else {
      panel_y -= w - 1;
    }
    panel_.draw16bitRGBBitmap(panel_x, panel_y, transformed, output_width,
                              output_height);
    heap_caps_free(transformed);
  }

  void mapPoint(int16_t x, int16_t y, int16_t& panel_x,
                int16_t& panel_y) const {
    switch (_rotation) {
      case 1:
        panel_x = BoardDisplay::kWidth - y - 1;
        panel_y = x;
        break;
      case 2:
        panel_x = BoardDisplay::kWidth - x - 1;
        panel_y = BoardDisplay::kHeight - y - 1;
        break;
      case 3:
        panel_x = y;
        panel_y = BoardDisplay::kHeight - x - 1;
        break;
      default:
        panel_x = x;
        panel_y = y;
        break;
    }
  }

  Arduino_CO5300& panel_;
};
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
  surface_ = new (std::nothrow) RotatedGfx(*panel_);
  if (surface_ == nullptr) {
    return false;
  }
  surface_->setRotation(rotation_);
  return true;
}

Arduino_GFX& BoardDisplay::gfx() { return *surface_; }

void BoardDisplay::setRotation(uint8_t rotation) {
  rotation_ = rotation & 3;
  if (surface_ != nullptr) {
    surface_->setRotation(rotation_);
  }
}

int16_t BoardDisplay::width() const {
  return surface_ != nullptr ? surface_->width() : kWidth;
}

int16_t BoardDisplay::height() const {
  return surface_ != nullptr ? surface_->height() : kHeight;
}

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
