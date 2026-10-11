#include "wifi_mgr.h"
#include <WiFi.h>
#include "config.h"
#include "storage.h"

namespace WifiMgr {
static String err;
static int trialIndex = -1;
static uint32_t trialStarted = 0;
static uint32_t nextRetry = 0;
static bool autoconnect = false;
static constexpr uint32_t kTryMs = 11000;

static void cleanStart() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(80);
}
static void startProfile(int idx) {
  if (idx < 0 || idx >= (int)gConfig.wifiNetworks.size()) return;
  trialIndex = idx;
  const auto& p = gConfig.wifiNetworks[idx];
  cleanStart();
  WiFi.begin(p.ssid.c_str(), p.pass.c_str());
  trialStarted = millis();
  Serial.printf("[wifi] trying network %d: %s\n", idx, p.ssid.c_str());
}
void beginConnect() {
  autoconnect = true;
  if (!gConfig.wifiNetworks.empty()) startProfile(0);
  else if (!gConfig.wifiSsid.isEmpty()) {
    gConfig.wifiNetworks.push_back({gConfig.wifiSsid, gConfig.wifiPass});
    startProfile(0);
  }
}
void tick() {
  if (!autoconnect) return;
  if (WiFi.status() == WL_CONNECTED) {
    trialIndex = -1;
    return;
  }
  if (trialIndex >= 0) {
    if (millis() - trialStarted < kTryMs) return;
    const int next = trialIndex + 1;
    if (next < (int)gConfig.wifiNetworks.size()) { startProfile(next); return; }
    trialIndex = -1;
    nextRetry = millis() + 30000;
    err = "No saved network in range";
    return;
  }
  if (!gConfig.wifiNetworks.empty() && (int32_t)(millis() - nextRetry) >= 0)
    startProfile(0);
}
bool connect(uint32_t timeoutMs) {
  err = "";
  autoconnect = false;
  if (gConfig.wifiNetworks.empty() && !gConfig.wifiSsid.isEmpty())
    gConfig.wifiNetworks.push_back({gConfig.wifiSsid, gConfig.wifiPass});
  if (gConfig.wifiNetworks.empty()) { err = "no saved networks"; return false; }
  const uint32_t started = millis();
  for (int i = 0; i < (int)gConfig.wifiNetworks.size(); ++i) {
    startProfile(i);
    uint32_t allowance = min((uint32_t)kTryMs, timeoutMs);
    while (WiFi.status() != WL_CONNECTED && millis() - trialStarted < allowance) delay(120);
    if (WiFi.status() == WL_CONNECTED) {
      trialIndex = -1;
      gConfig.wifiSsid = gConfig.wifiNetworks[i].ssid;
      gConfig.wifiPass = gConfig.wifiNetworks[i].pass;
      autoconnect = true;
      return true;
    }
    if (millis() - started >= timeoutMs) break;
  }
  trialIndex = -1;
  err = "No saved WiFi connected";
  autoconnect = true;
  nextRetry = millis() + 30000;
  return false;
}
bool saved(const String& ssid) {
  for (const auto& p : gConfig.wifiNetworks) if (p.ssid == ssid) return true;
  return false;
}
bool saveNetwork(const String& ssid, const String& pass) {
  if (ssid.isEmpty()) return false;
  for (auto& p : gConfig.wifiNetworks) if (p.ssid == ssid) {
    p.pass = pass;
    gConfig.wifiSsid = ssid; gConfig.wifiPass = pass;
    return Storage::save();
  }
  if (gConfig.wifiNetworks.size() >= 10) return false;
  gConfig.wifiNetworks.push_back({ssid, pass});
  gConfig.wifiSsid = ssid; gConfig.wifiPass = pass;
  return Storage::save();
}
bool removeNetwork(const String& ssid) {
  for (auto it = gConfig.wifiNetworks.begin(); it != gConfig.wifiNetworks.end(); ++it) {
    if (it->ssid == ssid) {
      gConfig.wifiNetworks.erase(it);
      if (gConfig.wifiSsid == ssid) {
        gConfig.wifiSsid = gConfig.wifiNetworks.empty() ? "" : gConfig.wifiNetworks.front().ssid;
        gConfig.wifiPass = gConfig.wifiNetworks.empty() ? "" : gConfig.wifiNetworks.front().pass;
      }
      return Storage::save();
    }
  }
  return false;
}
bool prioritiseNetwork(const String& ssid) {
  for (size_t i = 0; i < gConfig.wifiNetworks.size(); ++i) {
    if (gConfig.wifiNetworks[i].ssid == ssid) {
      auto p = gConfig.wifiNetworks[i];
      gConfig.wifiNetworks.erase(gConfig.wifiNetworks.begin() + i);
      gConfig.wifiNetworks.insert(gConfig.wifiNetworks.begin(), p);
      return Storage::save();
    }
  }
  return false;
}
bool connectSaved(const String& ssid, uint32_t timeoutMs) {
  for (size_t i = 0; i < gConfig.wifiNetworks.size(); ++i) {
    if (gConfig.wifiNetworks[i].ssid != ssid) continue;
    autoconnect = false;
    startProfile((int)i);
    while (WiFi.status() != WL_CONNECTED && millis() - trialStarted < timeoutMs) delay(120);
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) { gConfig.wifiSsid = ssid; gConfig.wifiPass = gConfig.wifiNetworks[i].pass; }
    else err = "Failed to join " + ssid;
    trialIndex = -1;
    autoconnect = true;
    nextRetry = millis() + 30000;
    return ok;
  }
  return false;
}
void disconnect() { autoconnect = false; trialIndex = -1; WiFi.disconnect(true); }
bool isConnected() { return WiFi.status() == WL_CONNECTED; }
String ip() { return WiFi.localIP().toString(); }
String ssid() { return WiFi.SSID(); }
int rssi() { return WiFi.RSSI(); }
String lastError() { return err; }
std::vector<Net> scan() {
  std::vector<Net> out;
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Net x{WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
    if (x.ssid.length()) out.push_back(x);
  }
  WiFi.scanDelete();
  return out;
}
} // namespace WifiMgr
