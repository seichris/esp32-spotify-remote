#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "AppUi.h"
#include "BoardDisplay.h"
#include "Config.h"
#include "OAuthPortal.h"
#include "SpotifyClient.h"
#include "TouchController.h"

namespace {
BoardDisplay display;
TouchController touch;
SpotifyClient spotify;
OAuthPortal oauth_portal(spotify);
AppUi ui(display);

TrackInfo current_track;
String current_artwork_url;
bool artwork_available = false;
bool touch_was_down = false;
bool services_started = false;
uint32_t next_spotify_poll_ms = 0;
uint32_t next_progress_refresh_ms = 0;
uint32_t next_touch_poll_ms = 0;
uint32_t next_wifi_retry_ms = 0;

bool deadlineReached(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

String deviceUrl() {
  return String("http://") + Config::kHostname + ".local";
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  ui.showStatus("Connecting to Wi-Fi", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(Config::kHostname);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t deadline = millis() + Config::kWiFiConnectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && !deadlineReached(deadline)) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    ui.showError("Wi-Fi connection failed");
    return false;
  }
  return true;
}

bool synchronizeClock() {
  ui.showStatus("Synchronizing clock", "TLS remains verified");
  configTzTime(Config::kTimezone, "time.cloudflare.com", "pool.ntp.org",
               "time.nist.gov");
  const uint32_t deadline = millis() + Config::kClockSyncTimeoutMs;
  time_t now = time(nullptr);
  while (now < 1700000000 && !deadlineReached(deadline)) {
    delay(100);
    now = time(nullptr);
  }
  if (now < 1700000000) {
    ui.showError("Clock sync failed; HTTPS was not downgraded");
    return false;
  }
  return true;
}

void startNetworkServices() {
  if (services_started) {
    return;
  }
  MDNS.begin(Config::kHostname);
  MDNS.addService("http", "tcp", 80);
  oauth_portal.begin();
  services_started = true;
}

void renderCurrentTrack(bool force_artwork_download) {
  String error;
  if (!spotify.fetchCurrentlyPlaying(current_track, error)) {
    ui.showError(error);
    next_spotify_poll_ms = millis() + Config::kSpotifyPollMs;
    return;
  }

  if (!current_track.available) {
    ui.showNoPlayback();
    current_artwork_url.clear();
    artwork_available = false;
    next_spotify_poll_ms = millis() + Config::kSpotifyPollMs;
    return;
  }

  const bool artwork_changed = current_track.artwork_url != current_artwork_url;
  if (current_track.artwork_url.isEmpty()) {
    current_artwork_url.clear();
    artwork_available = false;
  } else if (artwork_changed) {
    // Do not show artwork belonging to the previous track while the new image
    // is downloading.  The now-playing screen remains visible with its
    // placeholder, controls, title, artist, and progress bar.
    artwork_available = false;
  }

  ui.renderTrack(current_track, Config::kAlbumArtPath, artwork_available);

  if ((force_artwork_download || artwork_changed) &&
      !current_track.artwork_url.isEmpty()) {
    const bool downloaded = spotify.downloadArtwork(
        current_track.artwork_url, LittleFS, Config::kAlbumArtPath,
        Config::kAlbumArtTempPath, error);
    current_artwork_url = current_track.artwork_url;
    if (!downloaded) {
      Serial.printf("Artwork unavailable: %s\n", error.c_str());
    } else {
      // Replace only the artwork area by redrawing the now-playing screen once
      // the new image is ready; no loading/status screen is shown.
      artwork_available = true;
      ui.renderTrack(current_track, Config::kAlbumArtPath, artwork_available);
    }
  }

  next_spotify_poll_ms = millis() + Config::kSpotifyPollMs;
  next_progress_refresh_ms = millis() + Config::kProgressRefreshMs;
}

void handleTouch() {
  TouchPoint point;
  const bool touch_is_down = touch.read(point);
  if (touch_is_down && !touch_was_down && current_track.available) {
    PlaybackCommand command;
    if (ui.commandAt(point, command)) {
      ui.showCommandFeedback(command);
      String error;
      if (!spotify.sendPlaybackCommand(command, current_track.is_playing, error)) {
        ui.showError(error);
      } else {
        delay(120);
        renderCurrentTrack(false);
      }
    }
  }
  touch_was_down = touch_is_down;
}

void showReadyState() {
  if (!spotify.hasRefreshToken()) {
    ui.showAuthorization(deviceUrl());
    return;
  }
  ui.showStatus("Connecting to Spotify");
  if (!spotify.ensureAccessToken()) {
    ui.showAuthorization(deviceUrl());
    return;
  }
  renderCurrentTrack(true);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!display.begin()) {
    Serial.println("Fatal: CO5300 display initialization failed.");
    while (true) {
      delay(1000);
    }
  }
  display.setBrightness(Config::kAmoledBrightness);
  ui.begin();
  ui.showStatus("Starting hardware");

  const bool touch_ok = touch.begin();
  Serial.printf("Touch: %s, device id 0x%02X\n", touch_ok ? "ready" : "not found",
                touch.deviceId());

  // The partition table uses the explicit LittleFS label/subtype.  Passing
  // the label avoids Arduino's legacy default of "spiffs".
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    ui.showError("LittleFS initialization failed");
    while (true) {
      delay(1000);
    }
  }

  if (!spotify.begin(SPOTIFY_CLIENT_ID)) {
    ui.showError("Set SPOTIFY_CLIENT_ID in include/secrets.h");
    while (true) {
      delay(1000);
    }
  }

  if (connectWiFi() && synchronizeClock()) {
    startNetworkServices();
    showReadyState();
  } else {
    next_wifi_retry_ms = millis() + 10000;
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (deadlineReached(next_wifi_retry_ms)) {
      next_wifi_retry_ms = millis() + 10000;
      if (connectWiFi() && synchronizeClock()) {
        startNetworkServices();
        showReadyState();
      }
    }
    delay(10);
    return;
  }

  if (!services_started) {
    startNetworkServices();
  }
  oauth_portal.handleClient();

  if (oauth_portal.takeAuthorizationCompleted()) {
    ui.showStatus("Spotify connected");
    delay(400);
    renderCurrentTrack(true);
  }

  const uint32_t now = millis();
  if (deadlineReached(next_touch_poll_ms)) {
    next_touch_poll_ms = now + Config::kTouchPollMs;
    handleTouch();
  }

  if (spotify.isAuthorized() && deadlineReached(next_spotify_poll_ms)) {
    renderCurrentTrack(false);
  }

  if (current_track.available && deadlineReached(next_progress_refresh_ms)) {
    next_progress_refresh_ms = now + Config::kProgressRefreshMs;
    ui.updateProgress(current_track);
  }

  if (current_track.available) {
    ui.updateTitleMarquee();
  }

  delay(2);
}
