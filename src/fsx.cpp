#include "fsx.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include "config.h"

namespace fsx {

static fs::FS* gFs  = &LittleFS;
static bool    sd_  = false;
static String  base = "";          // "" on LittleFS root, "/deckssh" on SD

static void ensureDirs() {
  if (!sd_) return;
  if (!gFs->exists("/deckssh"))      gFs->mkdir("/deckssh");
  if (!gFs->exists("/deckssh/keys")) gFs->mkdir("/deckssh/keys");
  if (!gFs->exists("/deckssh/ca"))   gFs->mkdir("/deckssh/ca");
}

bool begin() {
  // Bring up the shared SPI bus and try the SD card first. (TFT_eSPI is already
  // initialised by this point; SD just arbitrates via its own CS line.)
  SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI, BOARD_SD_CS);
  if (SD.begin(BOARD_SD_CS, SPI, 8000000)) {
    gFs = &SD; sd_ = true; base = "/deckssh";
    Serial.println("[fs] using SD card");
  } else {
    LittleFS.begin(true);            // format on first use
    gFs = &LittleFS; sd_ = false; base = "";
    Serial.println("[fs] SD not found — using LittleFS");
  }
  ensureDirs();
  return true;
}

fs::FS& fs()         { return *gFs; }
bool    usingSD()    { return sd_; }
String  configPath() { return base + "/config.json"; }
String  keysDir()    { return base + "/keys"; }
String  caPath()     { return base + "/ca/anthropic.pem"; }
String  knownHostsVfs() { return sd_ ? "/sd/deckssh/known_hosts" : "/littlefs/known_hosts"; }

}  // namespace fsx
