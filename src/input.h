#pragma once
#include <Arduino.h>

// Unified input: the I2C keyboard emits CHAR events; the trackball emits
// directional NAV events plus SELECT (click). The UI layer decides what a
// CHAR means in context (e.g. '\r' = confirm in a menu, raw byte in a shell).

enum class EvType { NONE, CHAR, UP, DOWN, LEFT, RIGHT, SELECT };

// Plain aggregate (no default member initializers) so brace-init like
// {EvType::UP, 0} works under -std=gnu++11. Always fully constructed at use.
struct InputEvent {
  EvType type;
  char   ch;   // valid when type == CHAR
};

class Input {
 public:
  void begin();
  InputEvent poll();   // returns one queued event, or {NONE}

 private:
  // Trackball edge tracking
  int      lastUp_ = HIGH, lastDn_ = HIGH, lastLt_ = HIGH, lastRt_ = HIGH;
  uint32_t lastClickMs_ = 0;
  uint32_t lastKbMs_ = 0;
};

extern Input gInput;
