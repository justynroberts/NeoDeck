#pragma once
#include <Arduino.h>
#include <vector>

// Private-key files live in LittleFS under /keys (PEM text). Drop them in via
// `pio run -t uploadfs` (data/keys/<name>) or write them at runtime.
namespace Keys {
  std::vector<String> list();             // filenames in /keys
  String              read(const String& name);  // PEM contents, "" if missing
}
