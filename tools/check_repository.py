#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TEXT_EXTENSIONS = {".cpp", ".h", ".ini"}
FORBIDDEN = {
    "setInsecure(": "TLS verification must not be disabled",
    "Refresh token:": "tokens must not be logged",
    "Access token:": "tokens must not be logged",
    "SPOTIFY_CLIENT_SECRET": "PKCE target must not require a client secret",
}

errors: list[str] = []
scan_roots = [ROOT / "src", ROOT / "include"]
paths = [ROOT / "platformio.ini"]
for scan_root in scan_roots:
    paths.extend(scan_root.rglob("*"))
for path in paths:
    if not path.is_file() or path.suffix.lower() not in TEXT_EXTENSIONS:
        continue
    text = path.read_text(encoding="utf-8")
    for needle, reason in FORBIDDEN.items():
        if needle in text:
            errors.append(f"{path.relative_to(ROOT)}: {reason}: {needle}")

secrets = ROOT / "include" / "secrets.h"
if secrets.exists():
    errors.append("include/secrets.h must not be committed")

bridge = (ROOT / "tools" / "spotify_oauth_bridge.py").read_text(encoding="utf-8")
if "127.0.0.1" not in bridge or "/oauth/callback" not in bridge:
    errors.append("OAuth bridge is missing loopback or device callback handling")

if errors:
    print("Repository checks failed:")
    for error in errors:
        print(f"- {error}")
    raise SystemExit(1)

print("Repository checks passed.")
