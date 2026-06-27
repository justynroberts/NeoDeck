#pragma once
#include <Arduino.h>
#include <vector>

// =============================================================================
//  Board: LilyGo T-Deck Plus (ESP32-S3)
//  IMPORTANT: verify these pins against the official LilyGo T-Deck repo before
//  flashing — a wrong power/keyboard pin is the #1 cause of a "dead" board:
//  https://github.com/Xinyuan-LilyGO/T-Deck
// =============================================================================

#define BOARD_POWERON   10     // MUST be driven HIGH to power the peripherals

// I2C bus (keyboard MCU + touch + others live here)
#define BOARD_I2C_SDA   18
#define BOARD_I2C_SCL   8
#define KEYBOARD_ADDR   0x55   // T-Deck keyboard (ESP32-C3) I2C slave address

// Capacitive touch (GT911) — shares the I2C bus above. Address is 0x5D on the
// T-Deck Plus (some panels answer on 0x14; the driver probes both). INT pin is
// the documented T-Deck value — UNVERIFIED on hardware, flag if touch is dead.
#define BOARD_TOUCH_INT   16
#define BOARD_TOUCH_ADDR  0x5D
#define BOARD_TOUCH_ADDR2 0x14
// If taps land mirrored/rotated, flip these (set to 1) — needs on-hardware tune.
#define TOUCH_SWAP_XY 0
#define TOUCH_INV_X   0
#define TOUCH_INV_Y   0

// Trackball (active-low quadrature-style pulse pins + click)
#define TB_UP     3
#define TB_DOWN   15
#define TB_LEFT   1
#define TB_RIGHT  2
#define TB_CLICK  0            // also the BOOT button

// SD card + shared SPI bus (same bus as the TFT)
#define BOARD_SD_CS    39
#define BOARD_SPI_SCK  40
#define BOARD_SPI_MISO 38
#define BOARD_SPI_MOSI 41

// Display pins are passed to TFT_eSPI through build_flags in platformio.ini.

// =============================================================================
//  Application
// =============================================================================
#define APP_NAME        "NeoDeck"
#define APP_VERSION     "0.7.0"
#define CFG_PATH        "/config.json"
#define MAX_SCROLLBACK  200     // terminal lines kept in PSRAM
#define TERM_COLS       52      // chars per terminal row (6px font on 320px)
#define TERM_ROWS       24      // visible terminal rows under the header

// A saved SSH connection profile ("environment").
struct SshEnv {
  String   name;
  String   host;
  String   user;
  String   pass;          // used when auth == 0; stored locally only
  uint16_t port = 22;
  uint8_t  auth = 0;      // 0 = password, 1 = public key
  String   keyName;       // file under /keys (used when auth == 1)
  String   keyPass;       // optional passphrase for an encrypted key
};

// Everything persisted to LittleFS as /config.json.
struct AppConfig {
  String wifiSsid;
  String wifiPass;
  String anthropicKey;                 // Claude API key for the AI assistant
  String aiModel = "claude-opus-4-8";  // or "claude-haiku-4-5" for speed
  std::vector<SshEnv> envs;
};

extern AppConfig gConfig;
