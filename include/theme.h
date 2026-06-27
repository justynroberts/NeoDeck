#pragma once
#include <Arduino.h>

// "Matrix" theme — phosphor green on black, all the way through, in RGB565.
// Everything is a shade of green; only warnings/errors break the monochrome so
// failures stay legible.

#define C_BG       0x0000   // #000000 black
#define C_PANEL    0x0140   // #002800 very dark green panel
#define C_PANEL2   0x0280   // #005000 selected row (brighter green tint)
#define C_TEXT     0x07E0   // #00FF00 bright phosphor green (primary text)
#define C_MUTED    0x0480   // #009000 dim green (secondary text)
#define C_ACCENT   0x07E0   // #00FF00 signature green
#define C_ACCENT_D 0x0400   // #008000 dim green accent
#define C_WARN     0xFD20   // #FFA500 amber (kept for warnings)
#define C_ERR      0xF904   // #FA2020 red (kept for errors)
#define C_TERMTXT  0x07E0   // green terminal text
#define C_HDR      0x0140   // header bar tint (dark green)

// "Accent" palette — under the Matrix theme these are just green shades so the
// per-item coloring reads as varying brightness rather than hue.
#define C_CYAN     0x05E0   // #00C000 mid-bright green
#define C_BLUE     0x0400   // #008000 dark green
#define C_PURPLE   0x0560   // #00AC00 green
#define C_PINK     0x07E0   // green
#define C_HDR2     0x0280   // darker header edge

// Layout
#define HDR_H      22       // header bar height
#define ROW_H      22       // menu/list row height
