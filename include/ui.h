#pragma once
// SpaceDeck UI state: which screen is up, layer, menus, timeouts.
// Written from loop() (keys, knob, Bluetooth), read by the display task.
#include <Arduino.h>

namespace ui {

enum class Screen : uint8_t { Boot, Awaiting, Linked, Lost, Layer, Ambient, Switching };
enum class Menu : uint8_t { None, Snippets, Hosts };

struct Snapshot {
  Screen screen;
  uint8_t layer;
  bool connected;
  int8_t pressedKey;      // -1 = none; cell drawn inverted while set
  uint32_t screenSince;   // millis() when this screen started
  uint8_t stuckKeys;      // keys reading pressed at boot (boot log)
  Menu menu;              // menu over the layer view (None = closed)
  uint8_t menuIndex, menuCount;
  uint8_t host;           // active host slot
  uint8_t switchTo;       // target slot while Screen::Switching
  bool fastBoot;          // restarted to switch hosts: skip the boot animation
  uint32_t rev;           // bumps on every change -> display redraws
};

void begin(uint8_t stuckKeys, uint8_t host, bool fastBoot, uint8_t layer);
Snapshot snapshot();

// Input hooks. Each returns true if the key/knob action should actually fire
// (false when the press only wakes the screen from ambient mode).
bool onKey(uint8_t index, bool flash);   // flash = invert the key's cell briefly
bool onKnobTurn(int32_t detents);
bool onKnobPush();

void onLink(bool connected);
void setLayer(uint8_t layer);
uint8_t layer();
void showAmbient();       // e.g. long-press
// Menus: knob scrolls, push picks, any key cancels.
void openMenu(Menu kind, uint8_t count, uint8_t start);
bool menuOpenOrAmbient(); // a knob push now only selects / wakes
struct Pick { Menu kind; int index; };
Pick takeMenuSelection(); // picked item once, else {None, -1}
void showSwitching(uint8_t slot);
void bootDone();          // display task: boot animation finished
void tick();              // loop(): timeouts (menu, pressed cell, idle -> ambient)

}  // namespace ui
