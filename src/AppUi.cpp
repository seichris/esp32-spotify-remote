#include "AppUi.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <canvas/Arduino_Canvas.h>
#include <new>

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

constexpr int16_t kTitleScrollGap = 24;
constexpr uint32_t kTitleScrollIntervalMs = 90;
constexpr uint32_t kTitleScrollPauseMs = 1200;
constexpr int16_t kTitleScrollStepPx = 4;

constexpr int16_t kPlaceholderDesignSize = 300;
int16_t scalePlaceholder(int16_t value, int16_t art_size) {
  return static_cast<int16_t>((value * art_size + kPlaceholderDesignSize / 2) /
                              kPlaceholderDesignSize);
}

struct UiLayout {
  int16_t width;
  int16_t height;
  bool landscape;
  int16_t header_height;
  int16_t art_left;
  int16_t art_top;
  int16_t art_size;
  int16_t metadata_top;
  int16_t title_left;
  int16_t title_top;
  int16_t title_width;
  int16_t title_line_height;
  uint8_t title_text_size;
  int16_t artist_top;
  uint8_t artist_text_size;
  int16_t progress_top;
  int16_t controls_top;
  int16_t control_width;
};

UiLayout makeLayout(const Arduino_GFX& gfx) {
  const int16_t width = gfx.width();
  const int16_t height = gfx.height();
  const bool landscape = width > height;
  if (landscape) {
    constexpr int16_t art_size = 286;
    constexpr int16_t metadata_left = 306;
    return {width, height, true,
            42,
            10, 8, art_size,
            8,
            metadata_left, 102,
            static_cast<int16_t>(width - metadata_left - 10), 24, 3,
            151, 2,
            static_cast<int16_t>(height - 92),
            static_cast<int16_t>(height - 71),
            static_cast<int16_t>(width / 3)};
  }

  constexpr uint8_t title_size = 4;
  constexpr uint8_t artist_size = 3;
  constexpr int16_t title_height = 8 * title_size;
  constexpr int16_t artist_height = 8 * artist_size;
  constexpr int16_t progress_top = 410;
  constexpr int16_t art_size =
      progress_top - 8 - title_height - 8 - artist_height - 8;
  return {width, height, false,
          42,
          static_cast<int16_t>((width - art_size) / 2), 0, art_size,
          art_size,
          10, art_size + 8, static_cast<int16_t>(width - 20), title_height,
          title_size,
          art_size + 8 + title_height + 8, artist_size,
          progress_top, 431, static_cast<int16_t>(width / 3)};
}

// Some CO5300/Arduino_GFX combinations do not render generic fillTriangle()
// or one-pixel scanlines reliably. Build each triangle from short, filled
// columns instead; these use the same multi-pixel rectangle primitive as the
// working pause bars while retaining a clear triangular silhouette.
void drawPointingTriangle(Arduino_GFX& gfx, int16_t base_x, int16_t tip_x,
                          int16_t center_y, int16_t height, uint16_t color) {
  const int16_t left = min(base_x, tip_x);
  const int16_t right = max(base_x, tip_x);
  const int16_t width = right - left;
  const bool points_left = tip_x < base_x;
  constexpr int16_t kColumnWidth = 3;
  constexpr int16_t kMinimumHeight = 4;

  for (int16_t x = left; x <= right; x += kColumnWidth) {
    const int16_t column_width =
        min<int16_t>(kColumnWidth, right - x + 1);
    const int16_t distance_from_base = points_left ? x - left : right - x;
    const int16_t column_height =
        kMinimumHeight + ((height - kMinimumHeight) * distance_from_base) /
                             max<int16_t>(width, 1);
    gfx.fillRect(x, center_y - column_height / 2, column_width,
                 column_height, color);
  }
}
}  // namespace

AppUi::AppUi(BoardDisplay& display) : display_(display) {}

AppUi::~AppUi() { delete title_canvas_; }

void AppUi::begin() {
  TJpgDec.setCallback(jpegOutput);
  TJpgDec.setSwapBytes(false);
  rebuildTitleCanvas();
  invalidateTrack();
  display_.gfx().fillScreen(RGB565_BLACK);
}

void AppUi::setRotation(uint8_t rotation) {
  rotation &= 3;
  if (rotation == display_.rotation()) {
    return;
  }

  const ScreenMode previous_mode = screen_mode_;
  const TrackInfo previous_track = last_track_;
  const bool previous_artwork_available = last_artwork_available_;
  const String previous_heading = status_heading_;
  const String previous_detail = status_detail_;
  const uint16_t previous_accent = status_accent_;
  const String previous_authorization_url = authorization_url_;

  display_.setRotation(rotation);
  rebuildTitleCanvas();
  invalidateTrack();
  screen_mode_ = ScreenMode::kUnknown;
  display_.gfx().fillScreen(RGB565_BLACK);

  switch (previous_mode) {
    case ScreenMode::kStatus:
      showStatus(previous_heading, previous_detail, previous_accent);
      break;
    case ScreenMode::kAuthorization:
      showAuthorization(previous_authorization_url);
      break;
    case ScreenMode::kNoPlayback:
      showNoPlayback();
      break;
    case ScreenMode::kTrack:
      renderTrack(previous_track, Config::kAlbumArtPath,
                  previous_artwork_available);
      break;
    case ScreenMode::kUnknown:
      break;
  }
}

void AppUi::showStatus(const String& heading, const String& detail,
                       uint16_t accent) {
  if (screen_mode_ == ScreenMode::kStatus && heading == status_heading_ &&
      detail == status_detail_ && accent == status_accent_) {
    return;
  }

  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  invalidateTrack();
  screen_mode_ = ScreenMode::kStatus;
  status_heading_ = heading;
  status_detail_ = detail;
  status_accent_ = accent;
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify Remote", accent);
  drawCenteredText(heading, layout.height * 34 / 100, 3, RGB565_WHITE);
  if (!detail.isEmpty()) {
    drawCenteredText(detail, layout.height * 45 / 100, 2, RGB565_LIGHTGREY);
  }
}

void AppUi::showAuthorization(const String& device_url) {
  if (screen_mode_ == ScreenMode::kAuthorization &&
      device_url == authorization_url_) {
    return;
  }

  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  invalidateTrack();
  screen_mode_ = ScreenMode::kAuthorization;
  authorization_url_ = device_url;
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify setup", RGB565_SPOTIFY_GREEN);
  drawCenteredText("Open on your computer", layout.height * 27 / 100, 2,
                   RGB565_WHITE);
  drawCenteredText(device_url, layout.height * 35 / 100, 2,
                   RGB565_SPOTIFY_GREEN);
  drawCenteredText("Run the OAuth bridge first", layout.height * 47 / 100, 2,
                   RGB565_LIGHTGREY);
  drawCenteredText("then authorize in the browser", layout.height * 54 / 100,
                   2, RGB565_LIGHTGREY);
  drawCenteredText("Redirect: 127.0.0.1:4381", layout.height * 69 / 100, 1,
                   RGB565_DARKGREY);
}

void AppUi::showNoPlayback() {
  if (screen_mode_ == ScreenMode::kNoPlayback) {
    return;
  }

  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  invalidateTrack();
  screen_mode_ = ScreenMode::kNoPlayback;
  gfx.fillScreen(RGB565_BLACK);
  drawHeader("Spotify Remote", RGB565_SPOTIFY_GREEN);
  drawPlaceholderArt();
  if (layout.landscape) {
    drawCenteredText("Nothing is playing", 126, 2, RGB565_WHITE,
                     layout.title_left, layout.title_width);
    drawCenteredText("Start Spotify on any device", 165, 1,
                     RGB565_LIGHTGREY, layout.title_left, layout.title_width);
  } else {
    drawCenteredText("Nothing is playing", 370, 2, RGB565_WHITE);
    drawCenteredText("Start Spotify on any device", 397, 1,
                     RGB565_LIGHTGREY);
  }
  drawControls(false);
}

void AppUi::showError(const String& detail) {
  showStatus("Something went wrong", detail, RGB565_RED);
}

void AppUi::renderTrack(const TrackInfo& track, const char* artwork_path,
                        bool artwork_available) {
  Arduino_GFX& gfx = display_.gfx();
  const bool first_render = screen_mode_ != ScreenMode::kTrack ||
                            !track_frame_valid_;
  const bool artwork_changed =
      first_render || track.artwork_url != last_track_.artwork_url ||
      artwork_available != last_artwork_available_;
  const bool metadata_changed =
      first_render || track.uri != last_track_.uri ||
      track.title != last_track_.title || track.artists != last_track_.artists;
  const bool playback_changed =
      first_render || track.is_playing != last_track_.is_playing;

  // Keep the existing frame on the panel during normal polling. A full clear
  // is reserved for the first track after a screen transition.
  if (first_render) {
    gfx.fillScreen(RGB565_BLACK);
  }

  if (artwork_changed) {
    drawTrackArtwork(artwork_path, artwork_available);
  }
  if (metadata_changed) {
    drawTrackMetadata(track);
  }
  drawProgressBar(track.progress_ms, track.duration_ms);
  if (first_render || playback_changed || controls_dirty_) {
    drawControls(track.is_playing);
  }

  last_track_ = track;
  last_artwork_available_ = artwork_available;
  track_frame_valid_ = true;
  controls_dirty_ = false;
  screen_mode_ = ScreenMode::kTrack;
}

void AppUi::updateProgress(const TrackInfo& track) {
  if (!track_frame_valid_) {
    return;
  }

  uint32_t progress = track.progress_ms;
  if (track.is_playing) {
    progress += millis() - track.sampled_at_ms;
  }
  if (track.duration_ms > 0 && progress > track.duration_ms) {
    progress = track.duration_ms;
  }
  drawProgressBar(progress, track.duration_ms);
}

void AppUi::updateTitleMarquee() {
  if (!track_frame_valid_ || screen_mode_ != ScreenMode::kTrack) {
    return;
  }

  const UiLayout layout = makeLayout(display_.gfx());
  const int32_t title_width = static_cast<int32_t>(last_track_.title.length()) *
                              (6 * layout.title_text_size);
  if (title_width <= layout.title_width) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_title_scroll_ms_) < 0) {
    return;
  }

  const int32_t cycle_width = title_width + kTitleScrollGap;
  title_scroll_offset_ += kTitleScrollStepPx;
  if (title_scroll_offset_ >= cycle_width) {
    title_scroll_offset_ = 0;
    next_title_scroll_ms_ = now + kTitleScrollPauseMs;
  } else {
    next_title_scroll_ms_ = now + kTitleScrollIntervalMs;
  }
  drawTrackTitle(last_track_.title);
}

void AppUi::showCommandFeedback(PlaybackCommand command) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  int16_t x = 0;
  switch (command) {
    case PlaybackCommand::kPrevious:
      x = 0;
      break;
    case PlaybackCommand::kTogglePlayPause:
      x = layout.control_width;
      break;
    case PlaybackCommand::kNext:
      x = layout.control_width * 2;
      break;
  }
  gfx.fillRect(x + 4, layout.controls_top + 4, layout.control_width - 8,
               layout.height - layout.controls_top - 8, RGB565_DARKGREY);
  controls_dirty_ = true;
}

bool AppUi::commandAt(const TouchPoint& point, PlaybackCommand& command) const {
  const UiLayout layout = makeLayout(display_.gfx());
  if (point.y < layout.controls_top) {
    return false;
  }
  if (point.x < layout.control_width) {
    command = PlaybackCommand::kPrevious;
  } else if (point.x < layout.control_width * 2) {
    command = PlaybackCommand::kTogglePlayPause;
  } else {
    command = PlaybackCommand::kNext;
  }
  return true;
}

void AppUi::invalidateTrack() {
  track_frame_valid_ = false;
  controls_dirty_ = false;
}

void AppUi::rebuildTitleCanvas() {
  delete title_canvas_;
  title_canvas_ = nullptr;
  const UiLayout layout = makeLayout(display_.gfx());
  Arduino_Canvas* canvas = new (std::nothrow) Arduino_Canvas(
      layout.title_width, layout.title_line_height, &display_.gfx(),
      layout.title_left, layout.title_top);
  if (canvas != nullptr && canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    title_canvas_ = canvas;
  } else {
    delete canvas;
  }
}

void AppUi::drawHeader(const String& text, uint16_t accent) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  gfx.fillRect(0, 0, layout.width, layout.header_height, accent);
  gfx.setTextColor(RGB565_BLACK);
  gfx.setTextSize(2);
  const int16_t estimated_width = text.length() * 12;
  const int16_t cursor_x = (layout.width - estimated_width) / 2;
  gfx.setCursor(cursor_x > 8 ? cursor_x : 8, 13);
  gfx.print(text);
}

void AppUi::drawControls(bool is_playing) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  gfx.fillRect(0, layout.controls_top, layout.width,
               layout.height - layout.controls_top, RGB565_BLACK);
  for (int i = 0; i < 3; ++i) {
    gfx.drawRoundRect(i * layout.control_width + 4, layout.controls_top + 4,
                      layout.control_width - 8,
                      layout.height - layout.controls_top - 8, 12,
                      RGB565_DARKGREY);
  }

  const int16_t center_y =
      layout.controls_top + (layout.height - layout.controls_top) / 2;
  constexpr int16_t kSkipBarWidth = 6;
  constexpr int16_t kSkipGap = 5;
  constexpr int16_t kSkipTriangleWidth = 30;
  constexpr int16_t kSkipHeight = 40;
  constexpr int16_t kSkipWidth =
      kSkipBarWidth + kSkipGap + kSkipTriangleWidth;

  // Previous: a centered bar followed by a left-pointing triangle.
  const int16_t previous_left = layout.control_width / 2 - kSkipWidth / 2;
  const int16_t previous_tip = previous_left + kSkipBarWidth + kSkipGap;
  const int16_t previous_base = previous_tip + kSkipTriangleWidth;
  gfx.fillRect(previous_left, center_y - kSkipHeight / 2, kSkipBarWidth,
               kSkipHeight, RGB565_WHITE);
  drawPointingTriangle(gfx, previous_base, previous_tip, center_y,
                       kSkipHeight, RGB565_WHITE);

  // Play / pause.
  const int16_t center_x = layout.control_width + layout.control_width / 2;
  if (is_playing) {
    gfx.fillRect(center_x - 13, center_y - 20, 8, 40, RGB565_WHITE);
    gfx.fillRect(center_x + 5, center_y - 20, 8, 40, RGB565_WHITE);
  } else {
    drawPointingTriangle(gfx, center_x - 13, center_x + 23, center_y, 44,
                         RGB565_WHITE);
  }

  // Next: a right-pointing triangle followed by a centered bar.
  const int16_t next_left = layout.control_width * 2 +
                            layout.control_width / 2 - kSkipWidth / 2;
  const int16_t next_tip = next_left + kSkipTriangleWidth;
  const int16_t next_bar = next_tip + kSkipGap;
  drawPointingTriangle(gfx, next_left, next_tip, center_y, kSkipHeight,
                       RGB565_WHITE);
  gfx.fillRect(next_bar, center_y - kSkipHeight / 2, kSkipBarWidth,
               kSkipHeight, RGB565_WHITE);
}

void AppUi::drawTrackArtwork(const char* artwork_path, bool artwork_available) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  gfx.fillRect(layout.art_left, layout.art_top, layout.art_size,
               layout.art_size, RGB565_BLACK);
  if (!artwork_available ||
      !drawJpeg(artwork_path, layout.art_left, layout.art_top,
                layout.art_size)) {
    drawPlaceholderArt();
  }
}

void AppUi::drawTrackMetadata(const TrackInfo& track) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  gfx.fillRect(layout.title_left, layout.metadata_top, layout.title_width,
               layout.progress_top - layout.metadata_top, RGB565_BLACK);
  title_scroll_offset_ = 0;
  next_title_scroll_ms_ = millis() + kTitleScrollPauseMs;
  drawTrackTitle(track.title);
  drawCenteredText(track.artists, layout.artist_top, layout.artist_text_size,
                   RGB565_LIGHTGREY, layout.title_left, layout.title_width);
}

void AppUi::drawTrackTitle(const String& title) {
  Arduino_GFX& output = display_.gfx();
  const UiLayout layout = makeLayout(output);
  Arduino_GFX* target = &output;
  int16_t origin_x = layout.title_left;
  int16_t origin_y = layout.title_top;
  int16_t reset_width = layout.width;
  int16_t reset_height = layout.height;
  if (title_canvas_ != nullptr) {
    target = title_canvas_;
    origin_x = 0;
    origin_y = 0;
    reset_width = layout.title_width;
    reset_height = layout.title_line_height;
  }

  const int32_t title_width = static_cast<int32_t>(title.length()) *
                              (6 * layout.title_text_size);
  target->fillRect(origin_x, origin_y, layout.title_width,
                   layout.title_line_height,
                   RGB565_BLACK);
  target->setTextSize(layout.title_text_size);
  target->setTextColor(RGB565_WHITE, RGB565_BLACK);
  target->setTextWrap(false);

  if (title_width <= layout.title_width) {
    const int16_t cursor_x =
        origin_x + (layout.title_width - static_cast<int16_t>(title_width)) / 2;
    target->setCursor(cursor_x, origin_y);
    target->print(title);
  } else {
    target->setTextBound(origin_x, origin_y, layout.title_width,
                         layout.title_line_height);
    const int32_t first_x = origin_x - title_scroll_offset_;
    target->setCursor(static_cast<int16_t>(first_x), origin_y);
    target->print(title);

    const int32_t second_x = first_x + title_width + kTitleScrollGap;
    if (second_x < origin_x + layout.title_width) {
      target->setCursor(static_cast<int16_t>(second_x), origin_y);
      target->print(title);
    }
  }

  target->setTextBound(0, 0, reset_width, reset_height);
  target->setTextWrap(true);
  if (title_canvas_ != nullptr) {
    title_canvas_->flush();
  }
}

void AppUi::drawProgressBar(uint32_t progress_ms, uint32_t duration_ms) {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  constexpr int16_t x = 20;
  const int16_t width = layout.width - 40;
  constexpr int16_t height = 8;
  gfx.fillRect(x, layout.progress_top, width, height, RGB565_DARKGREY);
  if (duration_ms > 0) {
    const uint32_t clamped = min(progress_ms, duration_ms);
    const int16_t filled = static_cast<int16_t>(
        (static_cast<uint64_t>(clamped) * width) / duration_ms);
    if (filled > 0) {
      gfx.fillRect(x, layout.progress_top, filled, height,
                   RGB565_SPOTIFY_GREEN);
    }
  }
}

void AppUi::drawPlaceholderArt() {
  Arduino_GFX& gfx = display_.gfx();
  const UiLayout layout = makeLayout(gfx);
  const auto scale = [&layout](int16_t value) {
    return scalePlaceholder(value, layout.art_size);
  };
  gfx.fillRoundRect(layout.art_left, layout.art_top, layout.art_size,
                    layout.art_size, scale(16), 0x2104);
  gfx.fillCircle(layout.art_left + scale(120),
                 layout.art_top + scale(183), scale(36),
                 RGB565_SPOTIFY_GREEN);
  gfx.fillCircle(layout.art_left + scale(215),
                 layout.art_top + scale(158), scale(36),
                 RGB565_SPOTIFY_GREEN);
  gfx.fillRect(layout.art_left + scale(151), layout.art_top + scale(68),
               scale(12), scale(118), RGB565_SPOTIFY_GREEN);
  gfx.fillRect(layout.art_left + scale(246), layout.art_top + scale(43),
               scale(12), scale(118), RGB565_SPOTIFY_GREEN);
  gfx.fillRect(layout.art_left + scale(157), layout.art_top + scale(43),
               scale(95), scale(14), RGB565_SPOTIFY_GREEN);
}

void AppUi::drawCenteredText(const String& text, int16_t y, uint8_t size,
                             uint16_t color, int16_t box_left,
                             int16_t box_width) {
  Arduino_GFX& gfx = display_.gfx();
  if (box_width < 0) {
    box_width = gfx.width();
  }
  const String rendered = ellipsize(text, size, box_width - 8);
  const int16_t character_width = 6 * size;
  const int16_t estimated_width = rendered.length() * character_width;
  gfx.setTextSize(size);
  gfx.setTextColor(color, RGB565_BLACK);
  const int16_t cursor_x = box_left + (box_width - estimated_width) / 2;
  gfx.setCursor(cursor_x > box_left + 4 ? cursor_x : box_left + 4, y);
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
