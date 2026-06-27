#include "tls.h"
#include "fsx.h"

namespace Tls {

// setCACert() keeps the pointer, so the PEM must outlive the client — cache it.
static String  caCache;
static bool    loaded = false;

static const char* loadCa() {
  if (!loaded) {
    loaded = true;
    File f = fsx::fs().open(fsx::caPath(), "r");
    if (f) { caCache = f.readString(); f.close(); }
  }
  return caCache.length() ? caCache.c_str() : nullptr;
}

void configure(WiFiClientSecure& client) {
#ifdef TLS_INSECURE
  client.setInsecure();
  return;
#else
  const char* ca = loadCa();
  if (ca) {
    client.setCACert(ca);                // validate the server certificate chain
  } else {
    Serial.println("[tls] /ca/anthropic.pem not found — using INSECURE TLS");
    client.setInsecure();
  }
#endif
}

}  // namespace Tls
