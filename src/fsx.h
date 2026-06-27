#pragma once
#include <FS.h>

// Filesystem abstraction. Prefers the SD card (so config/keys/known_hosts
// persist even when the app is booted from SD by a launcher like Bruce, where
// the app's own LittleFS partition may not exist), and falls back to LittleFS.
//
// Both SD and LittleFS derive from fs::FS, so callers use one uniform handle.
namespace fsx {
  bool    begin();              // mount SD (preferred) or LittleFS
  fs::FS& fs();                 // active filesystem
  bool    usingSD();

  String  configPath();         // config.json
  String  keysDir();            // directory holding PEM private keys
  String  caPath();             // anthropic.pem
  String  knownHostsVfs();      // VFS path libssh uses for known_hosts
}
