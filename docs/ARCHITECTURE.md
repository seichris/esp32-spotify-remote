# Architecture

The firmware intentionally separates board-specific I/O from Spotify behavior:

- `BoardDisplay` owns the CO5300 QSPI AMOLED and its brightness control.
- `TouchController` talks directly to the FT3168-compatible controller over I2C.
- `SpotifyClient` owns PKCE, refresh-token persistence, verified HTTPS requests,
  playback state, controls, and album-art downloads.
- `OAuthPortal` serves the local setup page and accepts the callback forwarded by
  the loopback helper.
- `AppUi` renders the 410 × 502 portrait interface and maps touch zones to actions.

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
