#pragma once

#include <WiFiClientSecure.h>
#include <esp32_cert_bundle.h>

inline void configureVerifiedTls(WiFiClientSecure& client) {
  // Reapply this for every fresh client. This avoids depending on TLS state
  // surviving HTTPClient::end() or a redirect reconnect.
  client.setCACertBundle(x509_crt_bundle, x509_crt_bundle_len);
  client.setHandshakeTimeout(30);
  client.setTimeout(20);
}
