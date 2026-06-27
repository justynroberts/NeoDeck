#include "icons.h"
#include "display.h"
#include "theme.h"

namespace Icons {

void wifi(int x, int y, int bars, uint16_t col) {
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    tft.fillRect(x + i * 4, y + 14 - h, 3, h, i < bars ? col : C_PANEL2);
  }
}

void lock(int x, int y, uint16_t col) {
  tft.drawRoundRect(x + 4, y + 1, 8, 9, 3, col);   // shackle
  tft.fillRect(x + 3, y + 6, 10, 9, col);          // body
  tft.fillRect(x + 7, y + 9, 2, 3, C_BG);          // keyhole
}

void key(int x, int y, uint16_t col) {
  tft.drawCircle(x + 4, y + 5, 3, col);
  tft.fillCircle(x + 4, y + 5, 1, col);
  tft.drawLine(x + 6, y + 7, x + 13, y + 14, col);
  tft.drawFastVLine(x + 11, y + 12, 3, col);
}

void terminal(int x, int y, uint16_t col) {
  tft.drawRoundRect(x + 1, y + 2, 14, 12, 2, col);
  tft.drawLine(x + 4, y + 6, x + 6, y + 8, col);
  tft.drawLine(x + 6, y + 8, x + 4, y + 10, col);  // ">"
  tft.drawFastHLine(x + 7, y + 11, 4, col);        // "_"
}

void gear(int x, int y, uint16_t col) {
  int cx = x + 8, cy = y + 8;
  tft.fillCircle(cx, cy, 5, col);
  tft.fillCircle(cx, cy, 2, C_BG);
  tft.fillRect(cx - 1, y + 1, 2, 3, col);          // teeth N/S/E/W
  tft.fillRect(cx - 1, y + 12, 2, 3, col);
  tft.fillRect(x + 1, cy - 1, 3, 2, col);
  tft.fillRect(x + 12, cy - 1, 3, 2, col);
}

void robot(int x, int y, uint16_t col) {
  tft.drawRoundRect(x + 2, y + 4, 12, 10, 2, col);
  tft.fillRect(x + 5, y + 7, 2, 2, col);           // eyes
  tft.fillRect(x + 9, y + 7, 2, 2, col);
  tft.drawFastVLine(x + 8, y + 1, 3, col);         // antenna
  tft.fillCircle(x + 8, y + 1, 1, col);
}

void plus(int x, int y, uint16_t col) {
  tft.fillRect(x + 7, y + 3, 2, 10, col);
  tft.fillRect(x + 3, y + 7, 10, 2, col);
}

void server(int x, int y, uint16_t col) {
  tft.drawRoundRect(x + 2, y + 3, 12, 5, 1, col);
  tft.drawRoundRect(x + 2, y + 9, 12, 5, 1, col);
  tft.fillRect(x + 4, y + 5, 2, 1, col);
  tft.fillRect(x + 4, y + 11, 2, 1, col);
}

void chevron(int x, int y, uint16_t col) {
  tft.drawLine(x + 5, y + 4, x + 10, y + 8, col);
  tft.drawLine(x + 10, y + 8, x + 5, y + 12, col);
  tft.drawLine(x + 6, y + 4, x + 11, y + 8, col);
  tft.drawLine(x + 11, y + 8, x + 6, y + 12, col);
}

void info(int x, int y, uint16_t col) {
  tft.drawCircle(x + 8, y + 8, 6, col);
  tft.fillRect(x + 7, y + 7, 2, 5, col);
  tft.fillRect(x + 7, y + 4, 2, 2, col);
}

void dot(int x, int y, uint16_t col) { tft.fillCircle(x + 8, y + 8, 3, col); }

}  // namespace Icons
