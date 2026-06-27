#pragma once
#include <TFT_eSPI.h>

// Shared OFF-SCREEN framebuffer (a full-screen sprite in PSRAM, defined in
// ui.cpp). Everything draws into this buffer and ui.cpp pushes the finished
// frame to the panel in one shot — so the screen never shows a half-drawn frame
// (no flicker). Other modules (e.g. the icon renderer) draw to the same buffer.
extern TFT_eSprite tft;
