#pragma once
// SpaceDeck e-paper (Waveshare 2.13" V4, 250x122, GxEPD2).
// Draws the screens from design/spacedeck-screens.html in its own FreeRTOS
// task, so a slow refresh never blocks key scanning or Bluetooth.
#include <Arduino.h>

namespace display {
void begin();              // starts the render task; safe to call with no screen attached
uint32_t refreshCount();   // lifetime panel refreshes (rated ~1,000,000)
}
