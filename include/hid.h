#pragma once
#include <Arduino.h>

// Modifier bits (USB/BLE HID modifier byte)
enum : uint8_t {
  MOD_NONE = 0, MOD_CTRL = 0x01, MOD_SHIFT = 0x02, MOD_ALT = 0x04, MOD_GUI = 0x08,  // GUI = Cmd on Mac
};

enum class Media : uint8_t { PlayPause, Next, Prev, VolUp, VolDown, Mute };

enum class LinkEvent : int8_t { None, Connected, Disconnected };

// BLE HID keyboard + consumer control ("media keys").
class HidOutput {
 public:
  void begin();
  LinkEvent poll();                             // call every loop: restarts advertising, reports link changes
  bool connected() const;
  void tapKey(uint8_t modifiers, uint8_t key);  // key = ASCII ('c', '4', '\t', '`', ...)
  void tapMedia(Media m);
  void typeText(const char* text);              // '\n' = Enter
  void printInfo();                             // BT address, bonded hosts, link state
  void forgetAll();                             // clear bonds + drop current link
};

extern HidOutput hid;
