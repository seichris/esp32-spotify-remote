#include "SpotifyClient.h"

#include <ArduinoJson.h>
#include <climits>
#include <memory>
#include <new>
#include <utility>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "Config.h"
#include "TlsClient.h"

namespace {
constexpr char kTokenUrl[] = "https://accounts.spotify.com/api/token";
constexpr char kApiBase[] = "https://api.spotify.com/v1";
constexpr uint32_t kExpirySafetyMs = 60000;
constexpr size_t kMaximumJsonBody = 64 * 1024;

bool isRedirect(int status) {
  return status == HTTP_CODE_MOVED_PERMANENTLY || status == HTTP_CODE_FOUND ||
         status == HTTP_CODE_TEMPORARY_REDIRECT || status == 308;
}
}  // namespace

bool SpotifyClient::begin(const char* client_id) {
  if (client_id == nullptr || strlen(client_id) < 8 ||
      String(client_id).startsWith("YOUR_")) {
    return false;
  }
  client_id_ = client_id;
  if (!preferences_.begin("spotify", false)) {
    return false;
  }
  refresh_token_ = preferences_.getString("refresh", "");
  return true;
}

bool SpotifyClient::hasRefreshToken() const { return !refresh_token_.isEmpty(); }

bool SpotifyClient::isAuthorized() const {
  return !refresh_token_.isEmpty() ||
         (!access_token_.isEmpty() && deadlinePending(access_token_deadline_ms_));
}

bool SpotifyClient::ensureAccessToken() {
  if (!access_token_.isEmpty() && deadlinePending(access_token_deadline_ms_)) {
    return true;
  }
  String ignored;
  return refreshAccessToken(ignored);
}

void SpotifyClient::forgetAuthorization() {
  refresh_token_.clear();
  access_token_.clear();
  access_token_deadline_ms_ = 0;
  oauth_state_.clear();
  code_verifier_.clear();
  oauth_request_ready_ = false;
  preferences_.remove("refresh");
}

String SpotifyClient::createAuthorizationUrl() {
  code_verifier_ = randomUrlSafeString(80);
  oauth_state_ = randomUrlSafeString(32);
  oauth_request_ready_ = !code_verifier_.isEmpty() && !oauth_state_.isEmpty();
  if (!oauth_request_ready_) {
    return String();
  }

  const String challenge = sha256Base64Url(code_verifier_);
  String url = "https://accounts.spotify.com/authorize?response_type=code";
  url += "&client_id=" + urlEncode(client_id_);
  url += "&redirect_uri=" + urlEncode(Config::kRedirectUri);
  url += "&scope=" + urlEncode(Config::kSpotifyScopes);
  url += "&state=" + urlEncode(oauth_state_);
  url += "&code_challenge_method=S256";
  url += "&code_challenge=" + urlEncode(challenge);
  url += "&show_dialog=true";
  return url;
}

bool SpotifyClient::completeAuthorization(const String& code, const String& state,
                                          String& error) {
  if (!oauth_request_ready_) {
    error = "No active authorization request. Open the device setup page again.";
    return false;
  }
  if (state.isEmpty() || state != oauth_state_) {
    error = "OAuth state mismatch. Authorization was rejected.";
    return false;
  }
  if (code.isEmpty()) {
    error = "Spotify did not return an authorization code.";
    return false;
  }

  const bool success = exchangeAuthorizationCode(code, error);
  code_verifier_.clear();
  oauth_state_.clear();
  oauth_request_ready_ = false;
  return success;
}

bool SpotifyClient::fetchCurrentlyPlaying(TrackInfo& track, String& error) {
  const ApiResponse response = apiRequest(
      "GET", "/me/player/currently-playing?additional_types=track,episode");
  if (response.status == HTTP_CODE_NO_CONTENT) {
    track = TrackInfo{};
    return true;
  }
  if (response.status != HTTP_CODE_OK) {
    error = "Spotify currently-playing request failed (HTTP " +
            String(response.status) + ").";
    return false;
  }

  if (response.body.length() > kMaximumJsonBody) {
    error = "Spotify response was unexpectedly large.";
    return false;
  }

  JsonDocument document;
  const DeserializationError json_error = deserializeJson(document, response.body);
  if (json_error) {
    error = String("Could not parse Spotify response: ") + json_error.c_str();
    return false;
  }

  JsonVariantConst item = document["item"];
  if (item.isNull()) {
    track = TrackInfo{};
    return true;
  }

  TrackInfo next;
  next.available = true;
  next.is_playing = document["is_playing"] | false;
  next.progress_ms = document["progress_ms"] | 0U;
  next.duration_ms = item["duration_ms"] | 0U;
  next.sampled_at_ms = millis();
  next.uri = String(item["uri"] | "");
  next.title = String(item["name"] | "Unknown title");

  JsonArrayConst artists = item["artists"].as<JsonArrayConst>();
  for (JsonObjectConst artist : artists) {
    const char* name = artist["name"] | "";
    if (name[0] == '\0') {
      continue;
    }
    if (!next.artists.isEmpty()) {
      next.artists += ", ";
    }
    next.artists += name;
  }
  if (next.artists.isEmpty()) {
    next.artists = "Unknown artist";
  }

  JsonObjectConst album = item["album"].as<JsonObjectConst>();
  next.album = String(album["name"] | "");
  JsonArrayConst images = album["images"].as<JsonArrayConst>();
  int best_distance = INT_MAX;
  for (JsonObjectConst image : images) {
    const int width = image["width"] | 0;
    const char* url = image["url"] | "";
    if (url[0] == '\0') {
      continue;
    }
    const int distance = abs(width - 300);
    if (distance < best_distance) {
      best_distance = distance;
      next.artwork_url = url;
    }
  }

  track = std::move(next);
  return true;
}

bool SpotifyClient::sendPlaybackCommand(PlaybackCommand command,
                                        bool currently_playing, String& error) {
  const char* method = "POST";
  String endpoint;
  switch (command) {
    case PlaybackCommand::kPrevious:
      endpoint = "/me/player/previous";
      break;
    case PlaybackCommand::kNext:
      endpoint = "/me/player/next";
      break;
    case PlaybackCommand::kTogglePlayPause:
      method = "PUT";
      endpoint = currently_playing ? "/me/player/pause" : "/me/player/play";
      break;
  }

  const ApiResponse response = apiRequest(method, endpoint);
  if (response.status == HTTP_CODE_NO_CONTENT || response.status == HTTP_CODE_OK ||
      response.status == HTTP_CODE_ACCEPTED) {
    return true;
  }
  error = "Spotify playback command failed (HTTP " + String(response.status) + ").";
  return false;
}

bool SpotifyClient::downloadArtwork(const String& original_url, fs::FS& fs,
                                    const char* final_path,
                                    const char* temporary_path, String& error) {
  if (original_url.isEmpty()) {
    error = "No artwork URL was supplied.";
    return false;
  }

  String url = original_url;
  for (int redirect_count = 0; redirect_count < 5; ++redirect_count) {
    WiFiClientSecure client;
    configureVerifiedTls(client);
    HTTPClient http;
    const char* header_keys[] = {"Location", "Content-Type", "Content-Length"};
    http.collectHeaders(header_keys, 3);
    http.setConnectTimeout(15000);
    http.setTimeout(30000);
    if (!http.begin(client, url)) {
      error = "Could not start the artwork HTTPS request.";
      return false;
    }

    const int status = http.GET();
    if (isRedirect(status)) {
      const String next_url = http.header("Location");
      http.end();
      if (next_url.isEmpty()) {
        error = "Artwork redirect omitted its destination.";
        return false;
      }
      url = next_url;
      continue;
    }

    if (status != HTTP_CODE_OK) {
      http.end();
      error = "Artwork download failed (HTTP " + String(status) + ").";
      return false;
    }

    const String content_type = http.header("Content-Type");
    if (!content_type.startsWith("image/jpeg") &&
        !content_type.startsWith("image/jpg") && !content_type.isEmpty()) {
      http.end();
      error = "Spotify returned non-JPEG artwork.";
      return false;
    }

    fs.remove(temporary_path);
    File output = fs.open(temporary_path, FILE_WRITE);
    if (!output) {
      http.end();
      error = "Could not open the temporary artwork file.";
      return false;
    }

    const int written = http.writeToStream(&output);
    output.flush();
    output.close();
    http.end();
    if (written <= 0) {
      fs.remove(temporary_path);
      error = "Artwork response could not be written to flash.";
      return false;
    }

    fs.remove(final_path);
    if (!fs.rename(temporary_path, final_path)) {
      fs.remove(temporary_path);
      error = "Could not activate the downloaded artwork.";
      return false;
    }
    return true;
  }

  error = "Artwork download exceeded the redirect limit.";
  return false;
}

bool SpotifyClient::refreshAccessToken(String& error) {
  if (refresh_token_.isEmpty()) {
    error = "No Spotify refresh token is stored.";
    return false;
  }

  String form = "grant_type=refresh_token";
  form += "&refresh_token=" + urlEncode(refresh_token_);
  form += "&client_id=" + urlEncode(client_id_);
  const ApiResponse response = requestForm(form);
  return parseTokenResponse(response.status, response.body, true, error);
}

bool SpotifyClient::exchangeAuthorizationCode(const String& code, String& error) {
  String form = "grant_type=authorization_code";
  form += "&code=" + urlEncode(code);
  form += "&redirect_uri=" + urlEncode(Config::kRedirectUri);
  form += "&client_id=" + urlEncode(client_id_);
  form += "&code_verifier=" + urlEncode(code_verifier_);
  const ApiResponse response = requestForm(form);
  return parseTokenResponse(response.status, response.body, false, error);
}

bool SpotifyClient::parseTokenResponse(int status, const String& body,
                                       bool keep_old_refresh_token, String& error) {
  JsonDocument document;
  const DeserializationError json_error = deserializeJson(document, body);
  if (status != HTTP_CODE_OK) {
    const char* description = document["error_description"] | "";
    const char* short_error = document["error"] | "";
    error = "Spotify token request failed (HTTP " + String(status) + ")";
    if (description[0] != '\0') {
      error += ": ";
      error += description;
    } else if (short_error[0] != '\0') {
      error += ": ";
      error += short_error;
    }
    return false;
  }
  if (json_error) {
    error = String("Could not parse the Spotify token response: ") +
            json_error.c_str();
    return false;
  }

  const char* access_token = document["access_token"] | "";
  if (access_token[0] == '\0') {
    error = "Spotify token response did not contain an access token.";
    return false;
  }

  access_token_ = access_token;
  const uint32_t expires_seconds = document["expires_in"] | 3600U;
  const uint32_t lifetime_ms = expires_seconds * 1000U;
  access_token_deadline_ms_ = millis() +
      (lifetime_ms > kExpirySafetyMs ? lifetime_ms - kExpirySafetyMs : lifetime_ms / 2);

  const char* refresh_token = document["refresh_token"] | "";
  if (refresh_token[0] != '\0') {
    refresh_token_ = refresh_token;
    preferences_.putString("refresh", refresh_token_);
  } else if (!keep_old_refresh_token || refresh_token_.isEmpty()) {
    error = "Spotify did not return a refresh token.";
    access_token_.clear();
    access_token_deadline_ms_ = 0;
    return false;
  }

  return true;
}

SpotifyClient::ApiResponse SpotifyClient::apiRequest(const char* method,
                                                     const String& endpoint,
                                                     const String& body) {
  ApiResponse response;
  String token_error;
  if ((!access_token_.isEmpty() && deadlinePending(access_token_deadline_ms_)) ||
      refreshAccessToken(token_error)) {
    const String url = String(kApiBase) + endpoint;
    response = requestOnce(method, url, "application/json", body, true);
    if (response.status == HTTP_CODE_UNAUTHORIZED && refreshAccessToken(token_error)) {
      response = requestOnce(method, url, "application/json", body, true);
    }
  }
  return response;
}

SpotifyClient::ApiResponse SpotifyClient::requestForm(const String& form_body) {
  return requestOnce("POST", kTokenUrl, "application/x-www-form-urlencoded",
                     form_body, false);
}

SpotifyClient::ApiResponse SpotifyClient::requestOnce(
    const char* method, const String& url, const String& content_type,
    const String& body, bool include_bearer_token) {
  ApiResponse response;
  WiFiClientSecure client;
  configureVerifiedTls(client);
  HTTPClient http;
  const char* header_keys[] = {"Retry-After"};
  http.collectHeaders(header_keys, 1);
  http.setConnectTimeout(15000);
  http.setTimeout(30000);
  if (!http.begin(client, url)) {
    return response;
  }

  http.addHeader("Accept", "application/json");
  if (!content_type.isEmpty()) {
    http.addHeader("Content-Type", content_type);
  }
  if (include_bearer_token) {
    http.addHeader("Authorization", "Bearer " + access_token_);
  }

  if (strcmp(method, "GET") == 0) {
    response.status = http.GET();
  } else if (body.isEmpty()) {
    response.status = http.sendRequest(method);
  } else {
    response.status = http.sendRequest(method,
                                       reinterpret_cast<const uint8_t*>(body.c_str()),
                                       body.length());
  }

  response.retry_after = http.header("Retry-After");
  if (response.status > 0 && http.getSize() != 0) {
    response.body = http.getString();
  }
  http.end();
  return response;
}

String SpotifyClient::randomUrlSafeString(size_t length) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  String output;
  if (!output.reserve(length + 1)) {
    return String();
  }
  for (size_t i = 0; i < length; ++i) {
    output += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }
  return output;
}

String SpotifyClient::sha256Base64Url(const String& input) {
  uint8_t digest[32] = {};
  if (mbedtls_sha256(
          reinterpret_cast<const unsigned char*>(input.c_str()), input.length(),
          digest, 0) != 0) {
    return String();
  }
  return base64Url(digest, sizeof(digest));
}

String SpotifyClient::base64Url(const uint8_t* input, size_t length) {
  size_t output_length = 0;
  mbedtls_base64_encode(nullptr, 0, &output_length, input, length);
  std::unique_ptr<uint8_t[]> output(new (std::nothrow) uint8_t[output_length + 1]);
  if (!output) {
    return String();
  }
  if (mbedtls_base64_encode(output.get(), output_length + 1, &output_length,
                            input, length) != 0) {
    return String();
  }
  output[output_length] = '\0';
  String encoded(reinterpret_cast<char*>(output.get()));
  encoded.replace("+", "-");
  encoded.replace("/", "_");
  while (encoded.endsWith("=")) {
    encoded.remove(encoded.length() - 1);
  }
  return encoded;
}

String SpotifyClient::urlEncode(const String& input) {
  static constexpr char hex[] = "0123456789ABCDEF";
  String output;
  output.reserve(input.length() * 3 + 1);
  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t value = static_cast<uint8_t>(input[i]);
    const bool unreserved =
        (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == '-' || value == '_' ||
        value == '.' || value == '~';
    if (unreserved) {
      output += static_cast<char>(value);
    } else {
      output += '%';
      output += hex[value >> 4];
      output += hex[value & 0x0F];
    }
  }
  return output;
}

bool SpotifyClient::deadlinePending(uint32_t deadline) {
  return static_cast<int32_t>(deadline - millis()) > 0;
}
