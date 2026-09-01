#!/usr/bin/env python3
"""Loopback-to-device bridge for Spotify OAuth on the ESP32 remote.

Spotify permits HTTP redirect URIs only for explicit loopback IP literals. This
helper listens on 127.0.0.1, receives Spotify's short-lived authorization
response, and forwards only its query parameters to the ESP32's local callback.
It never receives or stores the access token, refresh token, client secret, or
Wi-Fi password.
"""

from __future__ import annotations

import argparse
import html
import http.server
import socketserver
import sys
import urllib.error
import urllib.parse
import urllib.request


class BridgeServer(socketserver.TCPServer):
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], device_url: str) -> None:
        super().__init__(address, CallbackHandler)
        self.device_url = device_url.rstrip("/")


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    server: BridgeServer

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path != "/callback":
            self.send_error(404)
            return

        forward_url = f"{self.server.device_url}/oauth/callback"
        if parsed.query:
            forward_url += "?" + parsed.query

        try:
            request = urllib.request.Request(
                forward_url,
                headers={"User-Agent": "spotify-oauth-loopback-bridge/1.0"},
            )
            with urllib.request.urlopen(request, timeout=20) as response:
                body = response.read(256 * 1024)
                status = response.status
                content_type = response.headers.get(
                    "Content-Type", "text/plain; charset=utf-8"
                )
        except urllib.error.HTTPError as exc:
            body = exc.read(256 * 1024)
            status = exc.code
            content_type = exc.headers.get(
                "Content-Type", "text/plain; charset=utf-8"
            )
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            message = (
                "Could not reach the ESP32 at "
                f"{html.escape(self.server.device_url)}.<br><br>"
                "Confirm that the computer and remote are on the same Wi-Fi, "
                "then retry authorization.<br><br>"
                f"Local error: {html.escape(str(exc))}"
            )
            self._send_html(502, message)
            return

        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

        # One callback completes one authorization attempt. Shut down after the
        # current response has been delivered.
        self.server._BaseServer__shutdown_request = True  # type: ignore[attr-defined]

    def log_message(self, format: str, *args: object) -> None:
        # Never print the callback URL because it includes the authorization code.
        sys.stderr.write("OAuth callback received; forwarding to the ESP32.\n")

    def _send_html(self, status: int, message: str) -> None:
        body = (
            "<!doctype html><meta name='viewport' content='width=device-width'>"
            "<body style='font-family:system-ui;background:#101010;color:white;"
            "padding:30px'><h1>Spotify Remote</h1><p>"
            + message
            + "</p></body>"
        ).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Forward Spotify's loopback callback to the ESP32 remote."
    )
    parser.add_argument(
        "--device-url",
        default="http://spotify-remote.local",
        help="ESP32 base URL (default: %(default)s)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=4381,
        help="loopback port registered in Spotify (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1 <= args.port <= 65535:
        print("Port must be between 1 and 65535.", file=sys.stderr)
        return 2

    address = ("127.0.0.1", args.port)
    try:
        with BridgeServer(address, args.device_url) as server:
            print(f"Listening at http://127.0.0.1:{args.port}/callback")
            print(f"Forwarding the callback to {args.device_url}/oauth/callback")
            print("Open the ESP32 setup page and click 'Authorize with Spotify'.")
            server.serve_forever(poll_interval=0.1)
    except OSError as exc:
        print(f"Could not start the loopback listener: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
