// DeckSSH — full-featured SSH client for the LilyGo T-Deck Plus.
//
// Boot order matters: peripheral power rail first, then I2C/keyboard, display,
// filesystem, network, SSH engine, UI.

#include <Arduino.h>
#include "config.h"
#include "fsx.h"
#include "storage.h"
#include "input.h"
#include "wifi_mgr.h"
#include "ssh_session.h"
#include "touch.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);

  // 1. Power the peripheral rail (display, keyboard, SD all hang off this).
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  delay(200);

  Serial.println("\n" APP_NAME " v" APP_VERSION " booting…");

  // 2. Input (I2C keyboard + trackball) — starts the shared I2C bus.
  gInput.begin();

  // 2b. Capacitive touch (GT911) lives on the same I2C bus.
  if (Touch::begin()) Serial.println("touch: GT911 found");
  else                Serial.println("touch: GT911 NOT found");

  // 3. Display + UI (shows splash immediately)
  UI::begin();

  // 4. Filesystem (SD preferred, LittleFS fallback) — after TFT so they share
  //    the SPI bus cleanly — then load config.
  fsx::begin();
  Storage::load();

  // 5. SSH engine init (libssh global init)
  gSsh.begin();

  // 6. Network — kick off association without blocking the UI.
  WifiMgr::beginConnect();
}

void loop() {
  // Drain one input event per loop and dispatch it.
  InputEvent ev = gInput.poll();
  if (ev.type != EvType::NONE) UI::handle(ev);

  // A fresh touch tap maps to the current screen's controls.
  int16_t tx, ty;
  if (Touch::poll(tx, ty)) UI::touch(tx, ty);

  UI::tick();
  UI::render();

  delay(4);  // keep the UI responsive without starving the SSH task
}
