#pragma once
#include <Arduino.h>
#include <vector>

namespace WifiMgr {
  struct Net { String ssid; int rssi; bool secure; };

  void   beginConnect();                  // non-blocking start (used at boot)
  bool   connect(uint32_t timeoutMs = 15000);  // blocking connect using gConfig
  void   disconnect();
  void   tick(); // non-blocking automatic failover after failed boot connection
  bool   saveNetwork(const String& ssid, const String& pass);
  bool   removeNetwork(const String& ssid);
  bool   prioritiseNetwork(const String& ssid);
  bool   connectSaved(const String& ssid, uint32_t timeoutMs = 12000);
  bool   saved(const String& ssid);

  bool   isConnected();
  String ip();
  String ssid();
  int    rssi();

  std::vector<Net> scan();                // blocking scan (~2-4s)
  String lastError();                     // human-readable reason after a failure
}
