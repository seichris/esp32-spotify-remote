#include "AppUi.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>

#include "Config.h"

namespace {
Arduino_GFX* jpeg_target = nullptr;
int16_t jpeg_offset_x = 0;
int16_t jpeg_offset_y = 0;
int16_t jpeg_limit_x = BoardDisplay::kWidth;
int16_t jpeg_limit_y = BoardDisplay::kHeight;

bool jpegOutput(int16_t x, int16_t y, uint16_t width, uint16_t height,
                uint16_t* bitmap) {
  if (jpeg_target == nullptr) {
    return false;
  }
  x += jpeg_offset_x;
  y += jpeg_offset_y;
  if (x >= jpeg_limit_x || y >= jpeg_limit_y) {
    return false;
  }
  jpeg_target->draw16bitRGBBitmap(x, y, bitmap, width, height);
  return true;
}

constexpr int16_t kHeaderHeight = 42;
constexpr int16_t kArtTop = 52;
constexpr int16_t kArtSize = 300;
constexpr int16_t kProgressTop = 405;
constexpr int16_t kControlsTop = 426;
constexpr int16_t kControlWidth = BoardDisplay::kWidth / 3;
}  // namespace

AppUi::AppUi(BoardDisplay& display) : display_(display) {}

void AppUi::begin() {
  TJpgDec.setCallback(jpegOutput);
  TJpgDec.setSwapBytes(false);
  display_.gfx().fillScreen(RGB565_BLACK);
}

void AppUi::showStatus(const String& heading, const String& detail,
                       uint16_t accent) {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify Remote", accent);
  drawCenteredText(heading, 170, 3, RGB565_WHITE, 380);
  if (!detail.isEmpty()) {
    drawCenteredText(detail, 225, 2, RGB565_LIGHTGREY, 380);
  }
}

void AppUi::showAuthorization(const String& device_url) {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify setup", RGB565_SPOTIFY_GREEN);
  drawCenteredText("Open on your computer", 135, 2, RGB565_WHITE);
  drawCenteredText(device_url, 175, 2, RGB565_SPOTIFY_GREEN, 390);
  drawCenteredText("Run the OAuth bridge first", 235, 2, RGB565_LIGHTGREY);
  drawCenteredText("then authorize in the browser", 270, 2, RGB565_LIGHTGREY);
  drawCenteredText("Redirect: 127.0.0.1:4381", 345, 1, RGB565_DARKGREY);
}

void AppUi::showNoPlayback() {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify Remote", RGB565_SPOTIFY_GREEN);
  drawPlaceholderArt();
  drawCenteredText("Nothing is playing", 370, 2, RGB565_WHITE);
  drawCenteredText("Start Spotify on any device", 397, 1, RGB565_LIGHTGREY);
  drawControls(false);
}

void AppUi::showError(const String& detail) {
  showStatus("Something went wrong", detail, RGB565_RED);
}

void AppUi::renderTrack(const TrackInfo& track, const char* artwork_path,
                        bool artwork_available) {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillScreen(RGB565_BLACK);
  drawHeader(track.is_playing ? "Now playing" : "Paused", RGB565_SPOTIFY_GREEN);

  if (!artwork_available || !drawJpeg(artwork_path, 55, kArtTop, kArtSize)) {
    drawPlaceholderArt();
  }

  drawCenteredText(track.title, 360, 2, RGB565_WHITE, 390);
  drawCenteredText(track.artists, 386, 1, RGB565_LIGHTGREY, 390);
  drawProgressBar(track.progress_ms, track.duration_ms);
  drawControls(track.is_playing);
  last_track_ = track;
}

void AppUi::updateProgress(const TrackInfo& track) {
  uint32_t progress = track.progress_ms;
  if (track.is_playing) {
    progress += millis() - track.sampled_at_ms;
  }
  if (track.duration_ms > 0 && progress > track.duration_ms) {
    progress = track.duration_ms;
  }
  drawProgressBar(progress, track.duration_ms);
}

void AppUi::showCommandFeedback(PlaybackCommand command) {
  Arduino_GFX& gfx = display_.gfx();
  int16_t x = 0;
  switch (command) {
    case PlaybackCommand::kPrevious:
      x = 0;
      break;
    case PlaybackCommand::kTogglePlayPause:
      x = kControlWidth;
      break;
    case PlaybackCommand::kNext:
      x = kControlWidth * 2;
      break;
  }
  gfx.fillRect(x + 4, kControlsTop + 4, kControlWidth - 8,
               BoardDisplay::kHeight - kControlsTop - 8, RGB565_DARKGREY);
}

bool AppUi::commandAt(const TouchPoint& point, PlaybackCommand& command) const {
  if (point.y < kControlsTop) {
    return false;
  }
  if (point.x < kControlWidth) {
    command = PlaybackCommand::kPrevious;
  } else if (point.x < kControlWidth * 2) {
    command = PlaybackCommand::kTogglePlayPause;
  } else {
    command = PlaybackCommand::kNext;
  }
  return true;
}

void AppUi::drawHeader(const String& text, uint16_t accent) {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillRect(0, 0, BoardDisplay::kWidth, kHeaderHeight, accent);
  gfx.setTextColor(RGB565_BLACK);
  gfx.setTextSize(2);
  const int16_t estimated_width = text.length() * 12;
  const int16_t cursor_x = (BoardDisplay::kWidth - estimated_width) / 2;
  gfx.setCursor(cursor_x > 8 ? cursor_x : 8, 13);
  gfx.print(text);
}

void AppUi::drawControls(bool is_playing) {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillRect(0, kControlsTop, BoardDisplay::kWidth,
               BoardDisplay::kHeight - kControlsTop, RGB565_BLACK);
  for (int i = 0; i < 3; ++i) {
    gfx.drawRoundRect(i * kControlWidth + 4, kControlsTop + 4,
                      kControlWidth - 8, BoardDisplay::kHeight - kControlsTop - 8,
                      12, RGB565_DARKGREY);
  }

  const int16_t center_y = kControlsTop + (BoardDisplay::kHeight - kControlsTop) / 2;
  // Previous.
  gfx.fillRect(43, center_y - 16, 5, 32, RGB565_WHITE);
  gfx.fillTriangle(48, center_y, 76, center_y - 19, 76, center_y + 19,
                   RGB565_WHITE);
  // Play / pause.
  const int16_t center_x = kControlWidth + kControlWidth / 2;
  if (is_playing) {
    gfx.fillRect(center_x - 13, center_y - 20, 8, 40, RGB565_WHITE);
    gfx.fillRect(center_x + 5, center_y - 20, 8, 40, RGB565_WHITE);
  } else {
    gfx.fillTriangle(center_x - 13, center_y - 22, center_x - 13,
                     center_y + 22, center_x + 23, center_y, RGB565_WHITE);
  }
  // Next.
  const int16_t right_base = kControlWidth * 2;
  gfx.fillTriangle(right_base + 60, center_y - 19, right_base + 60,
                   center_y + 19, right_base + 88, center_y, RGB565_WHITE);
  gfx.fillRect(right_base + 88, center_y - 16, 5, 32, RGB565_WHITE);
}

void AppUi::drawProgressBar(uint32_t progress_ms, uint32_t duration_ms) {
  Arduino_GFX& gfx = display_.gfx();
  constexpr int16_t x = 20;
  constexpr int16_t width = BoardDisplay::kWidth - 40;
  constexpr int16_t height = 8;
  gfx.fillRect(x, kProgressTop, width, height, RGB565_DARKGREY);
  if (duration_ms > 0) {
    const uint32_t clamped = min(progress_ms, duration_ms);
    const int16_t filled = static_cast<int16_t>(
        (static_cast<uint64_t>(clamped) * width) / duration_ms);
    if (filled > 0) {
      gfx.fillRect(x, kProgressTop, filled, height, RGB565_SPOTIFY_GREEN);
    }
  }
}

void AppUi::drawPlaceholderArt() {
  Arduino_GFX& gfx = display_.gfx();
  gfx.fillRoundRect(55, kArtTop, kArtSize, kArtSize, 16, 0x2104);
  gfx.fillCircle(175, 235, 36, RGB565_SPOTIFY_GREEN);
  gfx.fillCircle(270, 210, 36, RGB565_SPOTIFY_GREEN);
  gfx.fillRect(206, 120, 12, 118, RGB565_SPOTIFY_GREEN);
  gfx.fillRect(301, 95, 12, 118, RGB565_SPOTIFY_GREEN);
  gfx.fillRect(212, 95, 95, 14, RGB565_SPOTIFY_GREEN);
}

void AppUi::drawCenteredText(const String& text, int16_t y, uint8_t size,
                             uint16_t color, int16_t max_width) {
  Arduino_GFX& gfx = display_.gfx();
  const String rendered = ellipsize(text, size, max_width);
  const int16_t character_width = 6 * size;
  const int16_t estimated_width = rendered.length() * character_width;
  gfx.setTextSize(size);
  gfx.setTextColor(color, RGB565_BLACK);
  const int16_t cursor_x = (BoardDisplay::kWidth - estimated_width) / 2;
  gfx.setCursor(cursor_x > 4 ? cursor_x : 4, y);
  gfx.print(rendered);
}

String AppUi::ellipsize(const String& text, uint8_t size, int16_t max_width) const {
  const int16_t calculated_width = static_cast<int16_t>(6 * size);
  const int16_t character_width = calculated_width > 1 ? calculated_width : 1;
  const size_t calculated_characters = static_cast<size_t>(max_width / character_width);
  const size_t max_characters = calculated_characters > 1 ? calculated_characters : 1;
  if (text.length() <= max_characters) {
    return text;
  }
  if (max_characters <= 3) {
    return text.substring(0, max_characters);
  }
  return text.substring(0, max_characters - 3) + "...";
}

bool AppUi::drawJpeg(const char* path, int16_t target_x, int16_t target_y,
                     int16_t target_size) {
  if (!LittleFS.exists(path)) {
    return false;
  }
  uint16_t width = 0;
  uint16_t height = 0;
  if (TJpgDec.getFsJpgSize(&width, &height, path, LittleFS) != JDR_OK ||
      width == 0 || height == 0) {
    return false;
  }

  uint8_t scale = 1;
  while (scale < 8 && (width / scale > target_size || height / scale > target_size)) {
    scale *= 2;
  }
  TJpgDec.setJpgScale(scale);
  const int16_t scaled_width = width / scale;
  const int16_t scaled_height = height / scale;
  jpeg_target = &display_.gfx();
  jpeg_offset_x = target_x + (target_size - scaled_width) / 2;
  jpeg_offset_y = target_y + (target_size - scaled_height) / 2;
  jpeg_limit_x = target_x + target_size;
  jpeg_limit_y = target_y + target_size;

  const JRESULT result = TJpgDec.drawFsJpg(0, 0, path, LittleFS);
  jpeg_target = nullptr;
  return result == JDR_OK;
}
