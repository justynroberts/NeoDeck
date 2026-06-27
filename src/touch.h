#pragma once
#include <Arduino.h>

// GT911 capacitive touch on the shared I2C bus. The UI polls poll(); a fresh
// finger-down returns true once with screen coordinates already mapped to the
// active display rotation (landscape 320x240).
namespace Touch {
  bool   begin();                     // probe the controller; false if absent
  bool   present();                   // was a controller found at begin()?
  bool   poll(int16_t& x, int16_t& y);// true once per new press (tap)
  String report();                    // "55 5D" — every I2C address that ACKed
}
