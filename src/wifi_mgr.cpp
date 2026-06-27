#include "wifi_mgr.h"
#include <WiFi.h>
#include "config.h"

namespace WifiMgr {

static String err = "";

static void cleanStart() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);       // more reliable association
  WiFi.disconnect(true);      // clear any prior/launcher state
  delay(100);
}

void beginConnect() {
  if (gConfig.wifiSsid.isEmpty()) return;
  cleanStart();
  WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());
  Serial.printf("[wifi] begin '%s' (non-blocking)\n", gConfig.wifiSsid.c_str());
}

bool connect(uint32_t timeoutMs) {
  err = "";
  if (gConfig.wifiSsid.isEmpty()) { err = "no SSID set"; return false; }
  if (WiFi.status() == WL_CONNECTED) return true;

  cleanStart();
  WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(150);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL: err = "network not found"; break;
    case WL_CONNECT_FAILED: err = "wrong password?"; break;
    case WL_CONNECTION_LOST: err = "connection lost"; break;
    default: err = "timed out (5GHz? wrong pass?)"; break;
  }
  Serial.printf("[wifi] FAILED: %s (status=%d)\n", err.c_str(), WiFi.status());
  return false;
}

void disconnect() { WiFi.disconnect(true); }

bool   isConnected() { return WiFi.status() == WL_CONNECTED; }
String ip()          { return WiFi.localIP().toString(); }
String ssid()        { return WiFi.SSID(); }
int    rssi()        { return WiFi.RSSI(); }
String lastError()   { return err; }

std::vector<Net> scan() {
  std::vector<Net> out;
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Net x;
    x.ssid   = WiFi.SSID(i);
    x.rssi   = WiFi.RSSI(i);
    x.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    if (x.ssid.length()) out.push_back(x);
  }
  WiFi.scanDelete();
  Serial.printf("[wifi] scan found %u networks\n", (unsigned)out.size());
  return out;
}

}  // namespace WifiMgr
