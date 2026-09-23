#pragma once
// SpaceDeck UI state: which screen is up, layer, popups, timeouts.
// Written from loop() (keys, knob, Bluetooth), read by the display task.
#include <Arduino.h>

namespace ui {

enum class Screen : uint8_t { Boot, Awaiting, Linked, Lost, Layer, Ambient };
enum class Popup : uint8_t { None, Turn, Push };

struct Snapshot {
  Screen screen;
  uint8_t layer;
  bool connected;
  int8_t pressedKey;      // -1 = none; cell drawn inverted while set
  Popup popup;
  int16_t popupDelta;     // clicks turned (+ = CW)
  uint32_t screenSince;   // millis() when this screen started
  uint8_t stuckKeys;      // keys reading pressed at boot (boot log)
  bool menuOpen;          // snippet menu over the layer view
  uint8_t menuIndex, menuCount;
  uint32_t rev;           // bumps on every change -> display redraws
};

void begin(uint8_t stuckKeys);
Snapshot snapshot();

// Input hooks. Each returns true if the key/knob action should actually fire
// (false when the press only wakes the screen from ambient mode).
bool onKey(uint8_t index);
bool onKnobTurn(int32_t detents);
bool onKnobPush();

void onLink(bool connected);
void setLayer(uint8_t layer);
uint8_t layer();
void showAmbient();       // e.g. long-press
// Snippet menu: knob scrolls, push picks, any key cancels.
void openMenu(uint8_t count, uint8_t start);
int takeMenuSelection();  // picked index once, else -1
void bootDone();          // display task: boot animation finished
void tick();              // loop(): timeouts (popup, pressed cell, idle -> ambient)

}  // namespace ui
