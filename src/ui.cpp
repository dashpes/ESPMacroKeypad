#include "ui.h"
#include "config.h"

namespace ui {
namespace {

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
Snapshot g{Screen::Boot, 0, false, -1, Popup::None, 0, 0, 0, 1, false, 0, 0};
uint32_t g_menuAt = 0;
int g_menuPick = -1;
uint32_t g_pressedAt = 0, g_popupAt = 0, g_lastInput = 0;
bool g_bootDone = false;

// caller holds g_mux
void setScreen(Screen s) {
  g.screen = s;
  g.screenSince = millis();
  g.popup = Popup::None;
  g.pressedKey = -1;
  g.menuOpen = false;
  g.rev++;
}

// caller holds g_mux
void closeMenu() { g.menuOpen = false; g.rev++; }

// caller holds g_mux; returns true if this input only woke the screen
bool wakeIfAmbient() {
  g_lastInput = millis();
  if (g.screen != Screen::Ambient) return false;
  setScreen(Screen::Layer);
  return true;
}

}  // namespace

void begin(uint8_t stuckKeys) {
  portENTER_CRITICAL(&g_mux);
  g.stuckKeys = stuckKeys;
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

bool onKey(uint8_t i) {
  portENTER_CRITICAL(&g_mux);
  if (g.menuOpen) { closeMenu(); g_lastInput = millis(); portEXIT_CRITICAL(&g_mux); return false; }  // cancel
  bool fire = !wakeIfAmbient();
  if (fire && g.screen == Screen::Layer) {
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
  if (g.menuOpen) {
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
  if (g.menuOpen) {
    g_menuPick = g.menuIndex;
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

void openMenu(uint8_t count, uint8_t start) {
  if (!count) return;
  portENTER_CRITICAL(&g_mux);
  if (g.screen == Screen::Layer) {
    g.menuOpen = true;
    g.menuCount = count;
    g.menuIndex = start < count ? start : 0;
    g.popup = Popup::None;
    g.pressedKey = -1;
    g_menuAt = millis();
    g.rev++;
  }
  portEXIT_CRITICAL(&g_mux);
}

int takeMenuSelection() {
  portENTER_CRITICAL(&g_mux);
  const int p = g_menuPick;
  g_menuPick = -1;
  portEXIT_CRITICAL(&g_mux);
  return p;
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
  if (g.menuOpen && now - g_menuAt > UI_MENU_MS) closeMenu();
  if (g.screen == Screen::Layer && !g.menuOpen && g.connected && now - g_lastInput > UI_AMBIENT_AFTER_MS) setScreen(Screen::Ambient);
  portEXIT_CRITICAL(&g_mux);
}

}  // namespace ui
