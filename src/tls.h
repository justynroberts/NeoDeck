#pragma once
#include <WiFiClientSecure.h>

// Configures certificate validation for outbound HTTPS (the Claude API).
//
// If /ca/anthropic.pem exists in LittleFS it is pinned as the trusted CA
// (mbedTLS accepts one or several concatenated PEM roots). Otherwise the client
// falls back to setInsecure() and logs a warning, so the AI feature still works
// out-of-the-box while a hardened build supplies a real root.
namespace Tls {
  void configure(WiFiClientSecure& client);
}
