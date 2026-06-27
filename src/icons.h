#pragma once
#include <Arduino.h>

// Tiny 16x16 vector icons drawn with TFT primitives (web-style glyphs).
// (x, y) is the top-left of the icon box.
namespace Icons {
  void wifi(int x, int y, int bars, uint16_t col);  // bars 0..4
  void lock(int x, int y, uint16_t col);
  void key(int x, int y, uint16_t col);
  void terminal(int x, int y, uint16_t col);
  void gear(int x, int y, uint16_t col);
  void robot(int x, int y, uint16_t col);           // AI
  void plus(int x, int y, uint16_t col);
  void server(int x, int y, uint16_t col);
  void chevron(int x, int y, uint16_t col);          // right-pointing
  void info(int x, int y, uint16_t col);
  void dot(int x, int y, uint16_t col);
}
