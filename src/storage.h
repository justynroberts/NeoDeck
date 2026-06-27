#pragma once
#include "config.h"

// Persistence of AppConfig (WiFi creds, API key, AI model, SSH environments)
// to LittleFS as /config.json. Falls back to an empty config on first boot.
namespace Storage {
  bool begin();          // mounts LittleFS (formats on first use)
  bool load();           // reads CFG_PATH into gConfig
  bool save();           // writes gConfig to CFG_PATH

  // Convenience env CRUD (all persist immediately)
  void addEnv(const SshEnv& e);
  void updateEnv(size_t i, const SshEnv& e);
  void removeEnv(size_t i);
}
