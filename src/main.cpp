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
    k.swallowed = !ui::onKey(i);                 // press only woke the screen
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
  actionsBegin();
  ui::begin(countStuckKeys());
  display::begin();
  hid.begin();
}

static void serialCommands() {
  if (!Serial.available()) return;
  switch (tolower(Serial.read())) {
    case 'i': hid.printInfo(); break;
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

  if (encButton.update() == BtnEvent::Pressed && ui::onKnobPush())
    runAction(LAYERS[currentLayer()].encPress);

  const int pick = ui::takeMenuSelection();
  if (pick >= 0) pickSnippet(pick);

  ui::tick();
  delay(1);
}
