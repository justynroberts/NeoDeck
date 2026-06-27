#include "touch.h"
#include <Wire.h>
#include "config.h"

// Minimal GT911 driver. The chip uses 16-bit register addresses; we only need
// the status + first touch-point registers, so this stays tiny instead of
// pulling a whole touch library. Reset/address-select needs the RST line, which
// the T-Deck doesn't expose to a GPIO, so we just probe the default addresses.
namespace Touch {

static uint8_t  addr_   = 0;          // 0 == not found
static bool     wasDown = false;      // edge detect (tap = up->down)
static uint32_t lastMs  = 0;
static String   busRep_;              // I2C scan result captured at begin()

// GT911 register map (subset)
static const uint16_t REG_STATUS = 0x814E;   // bit7=ready, low nibble=#points
static const uint16_t REG_POINT1 = 0x8150;   // [id,xl,xh,yl,yh,sl,sh,res]

static bool rd(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;   // repeated start
  if (Wire.requestFrom((int)addr_, (int)len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void wr(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  Wire.endTransmission();
}

static bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

bool begin() {
  // Wire is already started by Input::begin(); just look for the controller.
  pinMode(BOARD_TOUCH_INT, INPUT);

  // Full bus scan — recorded so the About screen can show exactly what's on the
  // I2C bus (we're blind to serial on this hardware). The keyboard (0x55) should
  // always appear, which proves the scan itself works.
  busRep_ = "";
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (probe(a)) {
      if (busRep_.length()) busRep_ += ' ';
      busRep_ += String(a, HEX);
    }
    delay(1);
  }

  if      (probe(BOARD_TOUCH_ADDR))  addr_ = BOARD_TOUCH_ADDR;
  else if (probe(BOARD_TOUCH_ADDR2)) addr_ = BOARD_TOUCH_ADDR2;
  else                               addr_ = 0;
  Serial.printf("touch: I2C devices = [%s], GT911 addr = 0x%02X\n",
                busRep_.c_str(), addr_);
  return addr_ != 0;
}

bool present() { return addr_ != 0; }
String report() { return busRep_; }

// Map a raw GT911 coordinate to the active rotation (landscape 320x240) and
// apply the optional flips from config.h for on-hardware calibration.
static void mapXY(uint16_t rx, uint16_t ry, int16_t& sx, int16_t& sy) {
#if TOUCH_SWAP_XY
  int x = ry, y = rx;
#else
  int x = rx, y = ry;
#endif
#if TOUCH_INV_X
  x = 319 - x;
#endif
#if TOUCH_INV_Y
  y = 239 - y;
#endif
  sx = (int16_t)constrain(x, 0, 319);
  sy = (int16_t)constrain(y, 0, 239);
}

bool poll(int16_t& x, int16_t& y) {
  if (!addr_) return false;
  if (millis() - lastMs < 20) return false;     // throttle bus traffic
  lastMs = millis();

  uint8_t st;
  if (!rd(REG_STATUS, &st, 1)) return false;
  if (!(st & 0x80)) return false;               // no fresh data buffered

  uint8_t points = st & 0x0F;
  bool emit = false;
  if (points > 0) {
    uint8_t p[8];
    if (rd(REG_POINT1, p, 8)) {
      uint16_t rx = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
      uint16_t ry = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
      if (!wasDown) { mapXY(rx, ry, x, y); emit = true; }   // tap on press edge
      wasDown = true;
    }
  } else {
    wasDown = false;
  }

  wr(REG_STATUS, 0);   // must clear the ready flag every read
  return emit;
}

}  // namespace Touch
