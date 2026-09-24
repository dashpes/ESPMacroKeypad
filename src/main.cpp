// SpaceDeck — main firmware
// 8 keys + EC11 knob -> BLE keyboard actions (keymap in actions.cpp),
// with the retro-terminal e-paper UI (display.cpp) running in its own task.
#include <Arduino.h>
#include "config.h"
#include "input.h"
#include "actions.h"
#include "hid.h"
#include "display.h"
#include "ui.h"

static Keys keys;
static Encoder encoder;
static Debounced encButton;

// ---- tap / long-press per key ----
// Keys with no hold action fire on press (no added latency).
// Keys with a hold action fire their tap on release, or the hold after KEY_HOLD_MS.
struct KeyTrack { bool down, holdFired, swallowed; uint32_t since; };
static KeyTrack track[NUM_KEYS];

static void onKey(uint8_t i, bool pressed) {
  const Layer& L = LAYERS[currentLayer()];
  KeyTrack& k = track[i];
  if (pressed) {
    k = {true, false, false, millis()};
    // Only keys that fire right away (a shortcut/media key) flash their cell.
    // Layer switch and hold keys skip it: one refresh instead of three.
    const bool flash = !hasHold(L.holds[i]) && L.keys[i].type != ActionType::NextLayer;
    k.swallowed = !ui::onKey(i, flash);          // false = press only woke the screen / closed a menu
    if (!k.swallowed && !hasHold(L.holds[i])) runAction(L.keys[i]);
    return;
  }
  const bool tapPending = k.down && !k.swallowed && !k.holdFired && hasHold(L.holds[i]);
  k.down = false;
  if (tapPending) runAction(L.keys[i]);
}

static void checkHolds() {
  const Layer& L = LAYERS[currentLayer()];
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    KeyTrack& k = track[i];
    if (k.down && !k.swallowed && !k.holdFired && hasHold(L.holds[i]) && millis() - k.since >= KEY_HOLD_MS) {
      k.holdFired = true;
      runAction(L.holds[i]);
    }
  }
}

// ---- knob push: tap = layer's push action, hold = HOSTS menu ----
struct { bool down, swallowed, holdFired; uint32_t since; } knob;

static void updateKnobButton() {
  const BtnEvent b = encButton.update();
  if (b == BtnEvent::Pressed) {
    knob = {true, false, false, millis()};
    if (ui::menuOpenOrAmbient()) { ui::onKnobPush(); knob.swallowed = true; }   // select / wake only
  } else if (b == BtnEvent::Released) {
    if (knob.down && !knob.swallowed && !knob.holdFired && ui::onKnobPush())
      runAction(LAYERS[currentLayer()].encPress);
    knob.down = false;
  }
  if (knob.down && !knob.swallowed && !knob.holdFired && millis() - knob.since >= KNOB_HOLD_MS) {
    knob.holdFired = true;
    ui::openMenu(ui::Menu::Hosts, NUM_HOSTS, hid.hostSlot());
  }
}

// Survives ESP.restart() (not power loss): lets a host switch skip the boot animation.
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint8_t rtcLayer;
constexpr uint32_t FAST_BOOT_MAGIC = 0x5DEC0001;

static uint8_t countStuckKeys() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_KEYS; i++) n += keys.key(i).isDown();
  return n;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s ===\n", BLE_DEVICE_NAME);

  keys.begin();
  encoder.begin();
  encButton.begin(ENC_PIN_SW);
  const bool fastBoot = rtcMagic == FAST_BOOT_MAGIC;
  rtcMagic = 0;
  actionsBegin();
  hid.begin();   // reads the host slot first: it sets the Bluetooth address
  ui::begin(countStuckKeys(), hid.hostSlot(), fastBoot, fastBoot && rtcLayer < NUM_LAYERS ? rtcLayer : 0);
  display::begin();
}

static void serialCommands() {
  if (!Serial.available()) return;
  switch (tolower(Serial.read())) {
    case 'i': hid.printInfo(); Serial.printf("[epd] panel refreshes: %lu (rated ~1,000,000)\n", (unsigned long)display::refreshCount()); break;
    case 'f': hid.forgetAll(); break;
    case 'a': ui::showAmbient(); break;
    case 'h': Serial.println("Commands: i = Bluetooth info   f = forget all paired hosts   a = ambient screen"); break;
  }
}

void loop() {
  serialCommands();
  switch (hid.poll()) {
    case LinkEvent::Connected:    ui::onLink(true); break;
    case LinkEvent::Disconnected: ui::onLink(false); break;
    case LinkEvent::None:         break;
  }

  keys.update(onKey);
  checkHolds();

  const int32_t d = encoder.readDetents();
  if (d != 0 && ui::onKnobTurn(d)) {
    const Layer& L = LAYERS[currentLayer()];
    for (int32_t n = abs(d); n > 0; n--) runAction(d > 0 ? L.encCW : L.encCCW);
  }

  updateKnobButton();

  const ui::Pick pick = ui::takeMenuSelection();
  if (pick.kind == ui::Menu::Snippets) pickSnippet(pick.index);
  if (pick.kind == ui::Menu::Hosts && pick.index != hid.hostSlot()) {
    ui::showSwitching(pick.index);
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500) delay(10);   // let the e-paper show "SWITCHING"
    rtcMagic = FAST_BOOT_MAGIC;
    rtcLayer = currentLayer();
    hid.switchHost(pick.index);               // restarts
  }

  ui::tick();
  delay(1);
}
