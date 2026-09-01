#include "OAuthPortal.h"

#include <WiFi.h>

#include "Config.h"
#include "SpotifyClient.h"

OAuthPortal::OAuthPortal(SpotifyClient& spotify) : spotify_(spotify) {}

void OAuthPortal::begin() {
  server_.on("/", HTTP_GET, [this] { handleRoot(); });
  server_.on("/oauth/callback", HTTP_GET, [this] { handleCallback(); });
  server_.on("/forget", HTTP_POST, [this] { handleForget(); });
  server_.on("/health", HTTP_GET, [this] { handleHealth(); });
  server_.onNotFound([this] { handleNotFound(); });
  server_.begin();
}

void OAuthPortal::handleClient() { server_.handleClient(); }

bool OAuthPortal::takeAuthorizationCompleted() {
  const bool value = authorization_completed_;
  authorization_completed_ = false;
  return value;
}

void OAuthPortal::handleRoot() {
  String page;
  page.reserve(3500);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Waveshare Spotify Remote</title><style>");
  page += F("body{font-family:system-ui,sans-serif;max-width:720px;margin:40px auto;padding:0 20px;background:#101010;color:#fff}");
  page += F("a.button,button{display:inline-block;background:#1ed760;color:#07140b;border:0;border-radius:999px;padding:14px 22px;font-weight:700;text-decoration:none;font-size:16px}");
  page += F("code{background:#272727;padding:3px 6px;border-radius:5px} .card{background:#1b1b1b;padding:24px;border-radius:18px;margin:18px 0} .muted{color:#b3b3b3}</style></head><body>");
  page += F("<h1>Waveshare Spotify Remote</h1><div class='card'>");
  page += F("<p>Device: <code>");
  page += htmlEscape(WiFi.localIP().toString());
  page += F("</code></p>");

  if (spotify_.isAuthorized()) {
    page += F("<h2>Spotify is connected</h2><p>The display can now read and control the active Spotify player.</p>");
    page += F("<form method='post' action='/forget'><button type='submit'>Disconnect Spotify</button></form>");
  } else {
    const String authorization_url = spotify_.createAuthorizationUrl();
    page += F("<h2>Connect Spotify</h2>");
    page += F("<p>Before clicking the button, run the included helper on this computer:</p>");
    page += F("<p><code>python3 tools/spotify_oauth_bridge.py</code></p>");
    page += F("<p class='muted'>Register <code>");
    page += Config::kRedirectUri;
    page += F("</code> exactly in the Spotify Developer Dashboard.</p>");
    if (authorization_url.isEmpty()) {
      page += F("<p>Could not prepare the PKCE authorization request.</p>");
    } else {
      page += F("<p><a class='button' href='");
      page += htmlEscape(authorization_url);
      page += F("'>Authorize with Spotify</a></p>");
    }
  }

  page += F("</div><p class='muted'>Tokens are exchanged and stored on the ESP32. The bridge only forwards Spotify's short-lived callback parameters.</p></body></html>");
  server_.send(200, "text/html; charset=utf-8", page);
}

void OAuthPortal::handleCallback() {
  const String oauth_error = server_.arg("error");
  if (!oauth_error.isEmpty()) {
    server_.send(400, "text/plain; charset=utf-8",
                 "Spotify authorization was denied: " + oauth_error);
    return;
  }

  String error;
  if (!spotify_.completeAuthorization(server_.arg("code"), server_.arg("state"),
                                      error)) {
    server_.send(400, "text/plain; charset=utf-8", error);
    return;
  }

  authorization_completed_ = true;
  server_.send(200, "text/html; charset=utf-8",
               "<!doctype html><meta name='viewport' content='width=device-width'>"
               "<body style='font-family:system-ui;background:#101010;color:white;padding:30px'>"
               "<h1>Spotify connected</h1><p>You can close this tab and return to the remote.</p></body>");
}

void OAuthPortal::handleForget() {
  spotify_.forgetAuthorization();
  server_.sendHeader("Location", "/", true);
  server_.send(303, "text/plain", "");
}

void OAuthPortal::handleHealth() {
  String json = "{\"ok\":true,\"authorized\":";
  json += spotify_.isAuthorized() ? "true" : "false";
  json += "}";
  server_.send(200, "application/json", json);
}

void OAuthPortal::handleNotFound() {
  server_.send(404, "text/plain", "Not found");
}

String OAuthPortal::htmlEscape(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}
