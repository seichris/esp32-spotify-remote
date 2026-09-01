# Hardware test checklist

The source and configuration are compile-oriented, but a physical board remains
necessary for final validation.

1. Flash the filesystem and application, then confirm the boot screen is upright.
2. Verify all four display edges and the 22-pixel controller offset.
3. Touch each playback button and confirm its hit box matches the rendered control.
4. Press the upper physical key once and confirm it advances to the next track.
5. Press the lower physical key once and confirm it returns to the previous track.
6. Run OAuth using the loopback helper and inspect that no token appears in serial.
7. Exercise play, pause, previous, and next against an active Premium player.
8. Change tracks repeatedly and check album-art replacement in LittleFS.
9. Disconnect Wi-Fi and confirm automatic recovery without disabling TLS checks.
10. Leave the device running for several hours while tracks change.

Useful serial facts are limited to hardware state and non-sensitive errors. Never
paste a refresh token, access token, authorization code, or real `secrets.h` into
an issue.
