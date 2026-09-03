# Architecture

The firmware intentionally separates board-specific I/O from Spotify behavior:

- `BoardDisplay` owns the CO5300 QSPI AMOLED, brightness, and software rotation
  for the two orientations that require row/column exchange. RGB565 bitmaps are
  transformed as complete PSRAM rectangles before one panel transfer because
  the CO5300 does not reliably stream varied pixels through single-row or
  single-column rotated windows.
- `OrientationSensor` debounces four-side orientation from the QMI8658
  accelerometer while retaining the current orientation when the board is flat.
- `TouchController` talks directly to the FT3168-compatible controller over I2C
  and maps native touch coordinates into the current display orientation.
- `PhysicalButtons` debounces the BOOT GPIO and reads the AXP2101 PWR-key event.
- `SpotifyClient` owns PKCE, refresh-token persistence, verified HTTPS requests,
  playback state, controls, and album-art downloads.
- `OAuthPortal` serves the local setup page and accepts the callback forwarded by
  the loopback helper.
- `AppUi` selects a portrait or landscape layout and maps touch zones to actions.
  Landscape places a 320-pixel album image beside the track metadata. This size
  matches the decoder's half-scale output for Spotify's usual 640-pixel cover,
  avoiding a further drop to 160 pixels and a large empty border. Album JPEGs
  decode into a scaled PSRAM RGB565 buffer before one rotation-aware panel
  transfer, so decoder-block lifetime and panel orientation stay independent.

## Authorization flow

1. The ESP32 generates a PKCE verifier, challenge, and CSRF state value.
2. A browser opens the ESP32 setup page and follows its Spotify authorization URL.
3. Spotify redirects to `http://127.0.0.1:4381/callback`.
4. `tools/spotify_oauth_bridge.py` forwards only the callback query to the ESP32.
5. The ESP32 validates `state`, exchanges the code using its PKCE verifier, and
   stores only the refresh token in NVS. Deployments that require at-rest encryption should enable ESP32 flash encryption and NVS encryption.

A Spotify client secret is not needed and is never embedded in firmware.

## TLS

Every HTTPS operation creates a fresh `WiFiClientSecure` and applies the Mozilla
root bundle from `ESP32CertBundle`. The code deliberately contains no insecure
fallback. A valid wall clock is required before Spotify requests begin.
