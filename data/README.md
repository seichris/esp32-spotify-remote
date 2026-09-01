# LittleFS image

This directory is intentionally almost empty. The firmware formats LittleFS on
first boot and downloads current album artwork at runtime. `pio run --target
uploadfs` is therefore optional; it is useful only when preloading additional
assets later.
