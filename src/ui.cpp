#include "ui.h"
#include "config.h"

namespace ui {
namespace {

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g = [] { Snapshot s{}; s.screen = Screen::Boot; s.pressedKey = -1; s.popup = Popup::None; s.menu = Menu::None; s.rev = 1; return s; }();
uint32_t g_menuAt = 0;
Pick g_pick{Menu::None, -1};
uint32_t g_pressedAt = 0, g_popupAt = 0, g_lastInput = 0;
bool g_bootDone = false;

// caller holds g_mux
void setScreen(Screen s) {
  g.screen = s;
  g.screenSince = millis();
  g.popup = Popup::None;
  g.pressedKey = -1;
  g.menu = Menu::None;
  g.rev++;
}

// caller holds g_mux
void closeMenu() { g.menu = Menu::None; g.rev++; }

// caller holds g_mux; returns true if this input only woke the screen
bool wakeIfAmbient() {
  g_lastInput = millis();
  if (g.screen != Screen::Ambient) return false;
  setScreen(Screen::Layer);
  return true;
}

}  // namespace

void begin(uint8_t stuckKeys, uint8_t host, bool fastBoot, uint8_t layer) {
  portENTER_CRITICAL(&g_mux);
  g.stuckKeys = stuckKeys;
  g.host = host;
  g.fastBoot = fastBoot;
  g.layer = layer;
  g_lastInput = millis();
  setScreen(Screen::Boot);
  portEXIT_CRITICAL(&g_mux);
}

Snapshot snapshot() {
  portENTER_CRITICAL(&g_mux);
  Snapshot s = g;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

bool onKey(uint8_t i, bool flash) {
  portENTER_CRITICAL(&g_mux);
  if (g.menu != Menu::None) { closeMenu(); g_lastInput = millis(); portEXIT_CRITICAL(&g_mux); return false; }  // cancel
  bool fire = !wakeIfAmbient();
  // Each flash costs two e-paper refreshes (on + off), so it's skipped when not wanted.
  if (fire && flash && UI_PRESS_FLASH && g.screen == Screen::Layer) {
    g.pressedKey = i;
    g.popup = Popup::None;
    g_pressedAt = millis();
    g.rev++;
  }
  portEXIT_CRITICAL(&g_mux);
  return fire;
}

bool onKnobTurn(int32_t d) {
  portENTER_CRITICAL(&g_mux);
  if (g.menu != Menu::None) {
    const int n = g.menuCount;
    g.menuIndex = (uint8_t)(((g.menuIndex + d) % n + n) % n);
    g_menuAt = g_lastInput = millis();
    g.rev++;
    portEXIT_CRITICAL(&g_mux);
    return false;
  }
  bool fire = !wakeIfAmbient();
  if (fire && g.screen == Screen::Layer) {
    // keep counting while turning the same way; reverse starts over
    bool sameDir = g.popup == Popup::Turn && ((g.popupDelta > 0) == (d > 0));
    int32_t v = (sameDir ? g.popupDelta : 0) + d;
    g.popupDelta = (int16_t)constrain(v, -99, 99);
    g.popup = Popup::Turn;
    g_popupAt = millis();
    g.rev++;
  }
  portEXIT_CRITICAL(&g_mux);
  return fire;
}

bool onKnobPush() {
  portENTER_CRITICAL(&g_mux);
  if (g.menu != Menu::None) {
    g_pick = {g.menu, g.menuIndex};
    g_lastInput = millis();
    closeMenu();
    portEXIT_CRITICAL(&g_mux);
    return false;
  }
  bool fire = !wakeIfAmbient();
  if (fire && g.screen == Screen::Layer) {
    g.popup = Popup::Push;
    g.popupDelta = 0;
    g_popupAt = millis();
    g.rev++;
  }
  portEXIT_CRITICAL(&g_mux);
  return fire;
}

void onLink(bool connected) {
  portENTER_CRITICAL(&g_mux);
  g.connected = connected;
  g.rev++;
  if (g_bootDone) setScreen(connected ? Screen::Linked : Screen::Lost);
  portEXIT_CRITICAL(&g_mux);
}

void setLayer(uint8_t layer) {
  portENTER_CRITICAL(&g_mux);
  g.layer = layer;
  if (g.screen == Screen::Layer) setScreen(Screen::Layer);
  else g.rev++;
  portEXIT_CRITICAL(&g_mux);
}

uint8_t layer() {
  portENTER_CRITICAL(&g_mux);
  uint8_t l = g.layer;
  portEXIT_CRITICAL(&g_mux);
  return l;
}

void showAmbient() {
  portENTER_CRITICAL(&g_mux);
  if (g_bootDone) setScreen(Screen::Ambient);
  portEXIT_CRITICAL(&g_mux);
}

void openMenu(Menu kind, uint8_t count, uint8_t start) {
  if (!count) return;
  portENTER_CRITICAL(&g_mux);
  if (g.screen == Screen::Layer) {
    g.menu = kind;
    g.menuCount = count;
    g.menuIndex = start < count ? start : 0;
    g.popup = Popup::None;
    g.pressedKey = -1;
    g_menuAt = millis();
    g.rev++;
  }
  portEXIT_CRITICAL(&g_mux);
}

Pick takeMenuSelection() {
  portENTER_CRITICAL(&g_mux);
  const Pick p = g_pick;
  g_pick = {Menu::None, -1};
  portEXIT_CRITICAL(&g_mux);
  return p;
}

bool menuOpenOrAmbient() {
  portENTER_CRITICAL(&g_mux);
  const bool r = g.menu != Menu::None || g.screen == Screen::Ambient;
  portEXIT_CRITICAL(&g_mux);
  return r;
}

void showSwitching(uint8_t slot) {
  portENTER_CRITICAL(&g_mux);
  g.switchTo = slot;
  setScreen(Screen::Switching);
  portEXIT_CRITICAL(&g_mux);
}

void bootDone() {
  portENTER_CRITICAL(&g_mux);
  g_bootDone = true;
  setScreen(g.connected ? Screen::Linked : Screen::Awaiting);
  portEXIT_CRITICAL(&g_mux);
}

void tick() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&g_mux);
  if (g.screen == Screen::Linked && now - g.screenSince > UI_LINKED_MS) setScreen(Screen::Layer);
  if (g.pressedKey >= 0 && now - g_pressedAt > UI_PRESS_FLASH_MS) { g.pressedKey = -1; g.rev++; }
  if (g.popup != Popup::None && now - g_popupAt > UI_POPUP_MS) { g.popup = Popup::None; g.rev++; }
  if (g.menu != Menu::None && now - g_menuAt > UI_MENU_MS) closeMenu();
  if (g.screen == Screen::Layer && g.menu == Menu::None && g.connected && now - g_lastInput > UI_AMBIENT_AFTER_MS) setScreen(Screen::Ambient);
  portEXIT_CRITICAL(&g_mux);
}

}  // namespace ui
