#include "storage.h"
#include <ArduinoJson.h>
#include "fsx.h"

AppConfig gConfig;

namespace Storage {

bool begin() { return true; }   // mounting is handled by fsx::begin()

bool load() {
  File f = fsx::fs().open(fsx::configPath(), "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[cfg] parse error: %s\n", err.c_str());
    return false;
  }

  gConfig.wifiSsid     = doc["wifiSsid"]     | "";
  gConfig.wifiPass     = doc["wifiPass"]     | "";
  gConfig.anthropicKey = doc["anthropicKey"] | "";
  gConfig.aiModel      = doc["aiModel"]      | "claude-opus-4-8";

  gConfig.envs.clear();
  for (JsonObject o : doc["envs"].as<JsonArray>()) {
    SshEnv e;
    e.name    = o["name"]    | "";
    e.host    = o["host"]    | "";
    e.user    = o["user"]    | "";
    e.pass    = o["pass"]    | "";
    e.port    = o["port"]    | 22;
    e.auth    = o["auth"]    | 0;
    e.keyName = o["keyName"] | "";
    e.keyPass = o["keyPass"] | "";
    gConfig.envs.push_back(e);
  }
  Serial.printf("[cfg] loaded %u env(s)\n", (unsigned)gConfig.envs.size());
  return true;
}

bool save() {
  JsonDocument doc;
  doc["wifiSsid"]     = gConfig.wifiSsid;
  doc["wifiPass"]     = gConfig.wifiPass;
  doc["anthropicKey"] = gConfig.anthropicKey;
  doc["aiModel"]      = gConfig.aiModel;

  JsonArray arr = doc["envs"].to<JsonArray>();
  for (auto& e : gConfig.envs) {
    JsonObject o = arr.add<JsonObject>();
    o["name"]    = e.name;
    o["host"]    = e.host;
    o["user"]    = e.user;
    o["pass"]    = e.pass;
    o["port"]    = e.port;
    o["auth"]    = e.auth;
    o["keyName"] = e.keyName;
    o["keyPass"] = e.keyPass;
  }

  File f = fsx::fs().open(fsx::configPath(), "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

void addEnv(const SshEnv& e)              { gConfig.envs.push_back(e); save(); }
void updateEnv(size_t i, const SshEnv& e) { if (i < gConfig.envs.size()) { gConfig.envs[i] = e; save(); } }
void removeEnv(size_t i)                  { if (i < gConfig.envs.size()) { gConfig.envs.erase(gConfig.envs.begin() + i); save(); } }

}  // namespace Storage
