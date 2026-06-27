#include "input.h"
#include <Wire.h>
#include "config.h"

Input gInput;

void Input::begin() {
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  pinMode(TB_UP, INPUT_PULLUP);
  pinMode(TB_DOWN, INPUT_PULLUP);
  pinMode(TB_LEFT, INPUT_PULLUP);
  pinMode(TB_RIGHT, INPUT_PULLUP);
  pinMode(TB_CLICK, INPUT_PULLUP);
}

InputEvent Input::poll() {
  // --- Keyboard: read one byte from the T-Deck keyboard MCU ---
  // The keyboard returns 0x00 when nothing is pressed. Throttle slightly so a
  // held key doesn't flood the UI.
  if (millis() - lastKbMs_ > 8) {
    lastKbMs_ = millis();
    if (Wire.requestFrom((int)KEYBOARD_ADDR, 1) == 1) {
      int c = Wire.read();
      if (c > 0) return {EvType::CHAR, (char)c};
    }
  }

  // --- Trackball: detect falling edges as discrete steps ---
  int up = digitalRead(TB_UP), dn = digitalRead(TB_DOWN);
  int lt = digitalRead(TB_LEFT), rt = digitalRead(TB_RIGHT);

  if (lastUp_ == HIGH && up == LOW) { lastUp_ = up; return {EvType::UP, 0}; }
  if (lastDn_ == HIGH && dn == LOW) { lastDn_ = dn; return {EvType::DOWN, 0}; }
  if (lastLt_ == HIGH && lt == LOW) { lastLt_ = lt; return {EvType::LEFT, 0}; }
  if (lastRt_ == HIGH && rt == LOW) { lastRt_ = rt; return {EvType::RIGHT, 0}; }
  lastUp_ = up; lastDn_ = dn; lastLt_ = lt; lastRt_ = rt;

  // --- Click (debounced) ---
  if (digitalRead(TB_CLICK) == LOW && millis() - lastClickMs_ > 250) {
    lastClickMs_ = millis();
    return {EvType::SELECT, 0};
  }

  return {EvType::NONE, 0};
}
