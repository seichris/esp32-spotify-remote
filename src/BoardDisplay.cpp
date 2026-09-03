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

    // JPEG decoder blocks and the marquee canvas both arrive as RGB565
    // bitmaps. Rotate them into short horizontal panel scanlines. In
    // particular, avoid one-pixel-wide vertical address windows: the CO5300
    // accepts them but does not reliably advance streamed bitmap data through
    // them on this panel.
    uint16_t scanline[BoardDisplay::kHeight];
    if (_rotation == 1) {
      for (int16_t column = 0; column < w; ++column) {
        for (int16_t row = 0; row < h; ++row) {
          scanline[h - row - 1] =
              bitmap[static_cast<int32_t>(row) * w + column];
        }
        panel_.draw16bitRGBBitmap(BoardDisplay::kWidth - y - h, x + column,
                                  scanline, h, 1);
      }
      return;
    }

    if (_rotation == 2) {
      for (int16_t row = 0; row < h; ++row) {
        PixelPointer source = bitmap + static_cast<int32_t>(row) * w;
        for (int16_t column = 0; column < w; ++column) {
          scanline[w - column - 1] = source[column];
        }
        panel_.draw16bitRGBBitmap(BoardDisplay::kWidth - x - w,
                                  BoardDisplay::kHeight - y - row - 1,
                                  scanline, w, 1);
      }
      return;
    }

    for (int16_t column = 0; column < w; ++column) {
      for (int16_t row = 0; row < h; ++row) {
        scanline[row] = bitmap[static_cast<int32_t>(row) * w + column];
      }
      panel_.draw16bitRGBBitmap(y,
                                BoardDisplay::kHeight - x - column - 1,
                                scanline, h, 1);
    }
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
