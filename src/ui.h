#pragma once
#include "input.h"

// Screen-driven UI. main.cpp pumps input events in and calls tick()/render().
namespace UI {
  void begin();                  // init TFT, show splash
  void handle(const InputEvent& ev);
  void touch(int x, int y);      // a tap at screen coords (from the GT911)
  void tick();                   // drain SSH output, update terminal, animations
  void render();                 // redraw the current screen if dirty
}
