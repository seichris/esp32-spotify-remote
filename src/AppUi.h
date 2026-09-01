#pragma once

#include <Arduino.h>

#include "BoardDisplay.h"
#include "SpotifyClient.h"
#include "TouchController.h"

class AppUi {
 public:
  explicit AppUi(BoardDisplay& display);

  void begin();
  void showStatus(const String& heading, const String& detail = String(),
                  uint16_t accent = 0x1EC7);
  void showAuthorization(const String& device_url);
  void showNoPlayback();
  void showError(const String& detail);
  void renderTrack(const TrackInfo& track, const char* artwork_path,
                   bool artwork_available);
  void updateProgress(const TrackInfo& track);
  void showCommandFeedback(PlaybackCommand command);

  bool commandAt(const TouchPoint& point, PlaybackCommand& command) const;

 private:
  static constexpr uint16_t RGB565_SPOTIFY_GREEN = 0x1EC7;

  void drawHeader(const String& text, uint16_t accent);
  void drawControls(bool is_playing);
  void drawProgressBar(uint32_t progress_ms, uint32_t duration_ms);
  void drawPlaceholderArt();
  void drawCenteredText(const String& text, int16_t y, uint8_t size,
                        uint16_t color, int16_t max_width = 390);
  String ellipsize(const String& text, uint8_t size, int16_t max_width) const;
  bool drawJpeg(const char* path, int16_t target_x, int16_t target_y,
                int16_t target_size);

  BoardDisplay& display_;
  TrackInfo last_track_;
};
