#pragma once

// Copy this file to include/secrets.h. Never commit the real file.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Spotify Developer Dashboard -> your app -> Client ID.
// This firmware uses Authorization Code with PKCE, so no client secret is stored
// on the ESP32 or in this repository.
#define SPOTIFY_CLIENT_ID "YOUR_SPOTIFY_CLIENT_ID"
