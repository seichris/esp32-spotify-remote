# Hardware test checklist

The source and configuration are compile-oriented, but a physical board remains
necessary for final validation.

1. Flash the filesystem and application, then confirm the boot screen is upright.
2. Hold each of the four sides uppermost for at least half a second and confirm
   the display becomes upright without clipped or mirrored content.
3. In both landscape orientations, confirm the 286-pixel album image appears to
   the left of the title and artist and does not overlap progress or controls.
4. Verify all four display edges and the 22-pixel controller offset.
5. In every orientation, touch each playback button and confirm its hit box
   matches the rendered control.
6. Press the upper physical key once and confirm it advances to the next track.
7. Press the lower physical key once and confirm it returns to the previous track.
8. Run OAuth using the loopback helper and inspect that no token appears in serial.
9. Exercise play, pause, previous, and next against an active Premium player.
10. Change tracks repeatedly and check album-art replacement in LittleFS.
11. Disconnect Wi-Fi and confirm automatic recovery without disabling TLS checks.
12. Leave the device running for several hours while tracks change.

Useful serial facts are limited to hardware state and non-sensitive errors. Never
paste a refresh token, access token, authorization code, or real `secrets.h` into
an issue.
