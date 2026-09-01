#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>

struct TrackInfo {
  bool available = false;
  bool is_playing = false;
  String uri;
  String title;
  String artists;
  String album;
  String artwork_url;
  uint32_t progress_ms = 0;
  uint32_t duration_ms = 0;
  uint32_t sampled_at_ms = 0;
};

enum class PlaybackCommand {
  kPrevious,
  kTogglePlayPause,
  kNext,
};

class SpotifyClient {
 public:
  bool begin(const char* client_id);

  bool hasRefreshToken() const;
  bool isAuthorized() const;
  bool ensureAccessToken();
  void forgetAuthorization();

  String createAuthorizationUrl();
  bool completeAuthorization(const String& code, const String& state, String& error);

  bool fetchCurrentlyPlaying(TrackInfo& track, String& error);
  bool sendPlaybackCommand(PlaybackCommand command, bool currently_playing, String& error);
  bool downloadArtwork(const String& url, fs::FS& fs, const char* final_path,
                       const char* temporary_path, String& error);

 private:
  struct ApiResponse {
    int status = -1;
    String body;
    String retry_after;
  };

  bool refreshAccessToken(String& error);
  bool exchangeAuthorizationCode(const String& code, String& error);
  bool parseTokenResponse(int status, const String& body, bool keep_old_refresh_token,
                          String& error);
  ApiResponse apiRequest(const char* method, const String& endpoint,
                         const String& body = String());
  ApiResponse requestForm(const String& form_body);
  ApiResponse requestOnce(const char* method, const String& url,
                          const String& content_type, const String& body,
                          bool include_bearer_token);

  static String randomUrlSafeString(size_t length);
  static String sha256Base64Url(const String& input);
  static String base64Url(const uint8_t* input, size_t length);
  static String urlEncode(const String& input);
  static bool deadlinePending(uint32_t deadline);

  Preferences preferences_;
  String client_id_;
  String refresh_token_;
  String access_token_;
  uint32_t access_token_deadline_ms_ = 0;

  String oauth_state_;
  String code_verifier_;
  bool oauth_request_ready_ = false;
};
