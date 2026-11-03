#pragma once
#include <Arduino.h>

namespace Display {
  bool begin();          // returns false if no OLED found (controller keeps working)
  void render();         // call every DISPLAY_MS; reads app state (takes the lock)
}
