#pragma once

#include <Arduino.h>
#include <WebServer.h>

class SpotifyClient;

class OAuthPortal {
 public:
  explicit OAuthPortal(SpotifyClient& spotify);

  void begin();
  void handleClient();
  bool takeAuthorizationCompleted();

 private:
  void handleRoot();
  void handleCallback();
  void handleForget();
  void handleHealth();
  void handleNotFound();

  static String htmlEscape(const String& value);

  SpotifyClient& spotify_;
  WebServer server_{80};
  bool authorization_completed_ = false;
};
