#pragma once

#include "secrets.h"

namespace Config {
inline constexpr char kHostname[] = "spotify-remote";
inline constexpr char kDisplayName[] = "Waveshare Spotify Remote";
inline constexpr char kRedirectUri[] = "http://127.0.0.1:4381/callback";
inline constexpr char kSpotifyScopes[] =
    "user-read-playback-state user-read-currently-playing user-modify-playback-state";
inline constexpr char kTimezone[] = "UTC0";

inline constexpr uint32_t kSpotifyPollMs = 3000;
inline constexpr uint32_t kProgressRefreshMs = 500;
inline constexpr uint32_t kTouchPollMs = 20;
inline constexpr uint32_t kOrientationPollMs = 80;
inline constexpr uint32_t kOrientationSettleMs = 450;
inline constexpr uint32_t kWiFiConnectTimeoutMs = 25000;
inline constexpr uint32_t kClockSyncTimeoutMs = 20000;
inline constexpr uint16_t kOAuthBridgePort = 4381;
inline constexpr uint8_t kAmoledBrightness = 210;
inline constexpr char kAlbumArtPath[] = "/album.jpg";
inline constexpr char kAlbumArtTempPath[] = "/album.tmp";
}  // namespace Config
