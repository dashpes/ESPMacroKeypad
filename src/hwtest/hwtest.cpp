// =====================================================================
//  Hardware test — are we actually reading every key and the encoder?
//
//  pio run -e hwtest -t upload -t monitor
//
//  1. Wiring checks at boot (stuck pins, shorts, external pull-ups)
//  2. Guided test — watches EVERY usable GPIO (not just config.h), so it
//     finds keys wherever they're wired and prints a ready-to-paste
//     config block.
//  3. Once config.h matches, 'm' (monitor) exercises the real drivers
//     the firmware uses.
//
//  Commands: g = guided   m = monitor (real drivers)   d = discover (raw pins)
//            s = skip step   h = help
// =====================================================================
#include <Arduino.h>
#include "config.h"
#include "input.h"

// ---------------------------------------------------------------------
//  Pin watcher: every GPIO that could plausibly have a switch on it
// ---------------------------------------------------------------------
static const uint8_t CANDIDATES[] = {0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23,
                                     25, 26, 27, 32, 33, 34, 35, 36, 39};
static constexpr uint8_t NCAND = sizeof(CANDIDATES);

struct WPin {
  uint8_t gpio;
  bool usable;        // not floating
  bool extPullup;     // reads HIGH even with internal pull-down
  bool lowAtRest;
  bool raw, stable;   // true = LOW (pressed)
  uint32_t changedAt;
  uint32_t edges;     // raw edges since last reset
};
static WPin W[NCAND];

static bool isInputOnly(uint8_t g) { return g >= 34; }

static const char* configRole(uint8_t pin) {
  static char buf[20];
  for (uint8_t i = 0; i < NUM_KEYS; i++)
    if (KEY_PINS[i] == pin) { snprintf(buf, sizeof buf, "config KEY %u", i + 1); return buf; }
  if (pin == ENC_PIN_A)  return "config ENC A";
  if (pin == ENC_PIN_B)  return "config ENC B";
  if (pin == ENC_PIN_SW) return "config ENC SW";
  if (pin == EPD_PIN_CLK)  return "e-paper CLK";
  if (pin == EPD_PIN_DIN)  return "e-paper DIN";
  if (pin == EPD_PIN_CS)   return "e-paper CS";
  if (pin == EPD_PIN_DC)   return "e-paper DC";
  if (pin == EPD_PIN_RST)  return "e-paper RST";
  if (pin == EPD_PIN_BUSY) return "e-paper BUSY";
  return "not in config";
}

static void watchBegin(bool quiet) {
  for (uint8_t i = 0; i < NCAND; i++) {
    WPin& p = W[i];
    p.gpio = CANDIDATES[i];
    if (!isInputOnly(p.gpio)) {
      pinMode(p.gpio, INPUT_PULLDOWN);
      delayMicroseconds(100);
      p.extPullup = digitalRead(p.gpio) == HIGH;
      pinMode(p.gpio, INPUT_PULLUP);
    } else {
      pinMode(p.gpio, INPUT);  // 34-39: no internal pulls at all
      p.extPullup = false;
    }
  }
  delay(5);
  for (auto& p : W) { p.raw = p.stable = digitalRead(p.gpio) == LOW; p.usable = true; p.edges = 0; p.changedAt = millis(); }

  // Anything that wiggles on its own for 300 ms is floating -> ignore.
  uint32_t t0 = millis();
  while (millis() - t0 < 300)
    for (auto& p : W)
      if ((digitalRead(p.gpio) == LOW) != p.raw) p.usable = false;
  // Input-only pins are only usable if something external holds them HIGH.
  for (auto& p : W) {
    if (isInputOnly(p.gpio) && p.raw) p.usable = false;
    p.lowAtRest = p.usable && p.raw;
  }
  if (!quiet)
    for (auto& p : W)
      if (!p.usable && !isInputOnly(p.gpio)) Serial.printf("  (ignoring GPIO%u - floating/noisy)\n", p.gpio);
}

// onEdge(index, low) fires on every raw edge; onPress(index, pressed) on debounced changes
template <typename EdgeFn, typename PressFn>
static void watchUpdate(EdgeFn onEdge, PressFn onPress) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < NCAND; i++) {
    WPin& p = W[i];
    if (!p.usable) continue;
    const bool raw = digitalRead(p.gpio) == LOW;
    if (raw != p.raw) {
      p.raw = raw;
      p.changedAt = now;
      p.edges++;
      onEdge(i, raw);
    } else if (raw != p.stable && now - p.changedAt >= DEBOUNCE_MS) {
      p.stable = raw;
      onPress(i, raw);
    }
  }
}

static void watchResetEdges() { for (auto& p : W) p.edges = 0; }

// ---------------------------------------------------------------------
//  Wiring checks
// ---------------------------------------------------------------------
static int bootProblems = 0;

static void wiringChecks() {
  Serial.println(F("\n--- Wiring checks (don't touch anything) ---"));

  for (uint8_t i = 0; i < NUM_KEYS; i++)
    for (uint8_t j = i + 1; j < NUM_KEYS; j++)
      if (KEY_PINS[i] == KEY_PINS[j]) { Serial.printf("  FAIL  GPIO%u listed twice in KEY_PINS\n", KEY_PINS[i]); bootProblems++; }

  watchBegin(false);

  for (auto& p : W) {
    if (!p.usable) continue;
    // DOIT board parts, not your wiring: BOOT button pull-up, onboard LED, strap pull-up
    if ((p.gpio == 0 && p.extPullup && !p.lowAtRest) || (p.gpio == 2 && p.lowAtRest) || (p.gpio == 5 && p.extPullup && !p.lowAtRest))
      continue;
    if (p.lowAtRest)
      Serial.printf("  NOTE  GPIO%-2u LOW at rest (%s) -> stuck key / short to GND, or encoder parked between clicks\n",
                    p.gpio, configRole(p.gpio));
    else if (p.extPullup)
      Serial.printf("  NOTE  GPIO%-2u has an external pull-up (%s) -> normal for encoder-board pins; on a plain key it means touching 3V3\n",
                    p.gpio, configRole(p.gpio));
  }

  // Pairwise short test: drive one pin LOW, see if any other follows.
  // Skip pins with external pull-ups (could be 3V3) and input-only pins.
  for (uint8_t i = 0; i < NCAND; i++) {
    WPin& a = W[i];
    if (!a.usable || a.lowAtRest || a.extPullup || isInputOnly(a.gpio)) continue;
    pinMode(a.gpio, OUTPUT);
    digitalWrite(a.gpio, LOW);
    delayMicroseconds(100);
    for (uint8_t j = i + 1; j < NCAND; j++) {
      WPin& b = W[j];
      if (!b.usable || b.lowAtRest) continue;
      if (digitalRead(b.gpio) == LOW) {
        Serial.printf("  FAIL  GPIO%u is shorted to GPIO%u\n", a.gpio, b.gpio);
        bootProblems++;
      }
    }
    pinMode(a.gpio, INPUT_PULLUP);
  }
  delay(2);

  // E-paper header: with no screen plugged in, these should all be idle.
  const uint8_t epd[] = {EPD_PIN_CLK, EPD_PIN_DIN, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY};
  int epdBad = 0;
  for (uint8_t g : epd)
    for (auto& p : W)
      if (p.gpio == g && (!p.usable || p.lowAtRest || (p.extPullup && g != 5))) {
        Serial.printf("  CHECK GPIO%u (%s): %s\n", g, configRole(g),
                      p.lowAtRest ? "pulled LOW -> solder bridge to GND or a key wire?"
                      : p.extPullup ? "pulled HIGH -> bridged to 3V3?  (ignore if the screen is plugged in)"
                                    : "floating/noisy");
        epdBad++;
      }
  Serial.println(epdBad ? "  e-paper header: check the pins above"
                        : "  e-paper header (18, 17, 5, 16, 15, 4): free, no bridges");

  Serial.println(bootProblems ? "--- Wiring checks: problems found ---" : "--- Wiring checks: OK ---");
}

// ---------------------------------------------------------------------
//  Guided test (pin-level, config-independent)
// ---------------------------------------------------------------------
enum class Mode { Guided, Monitor, Discover };
static Mode mode = Mode::Guided;

enum : int { STEP_CW = NUM_KEYS, STEP_CCW, STEP_PRESS, STEP_DONE };
static constexpr int CLICKS = 3;
static constexpr uint32_t ENC_SETTLE_MS = 1200;  // quiet time that ends an encoder step

static int step = 0;
static int keyPin[NUM_KEYS];            // GPIO found for each physical key, -1 = none
static int pendingKey = -1;             // watcher index pressed, waiting for release
static int encA = -1, encB = -1, encSW = -1;
static int firstFallCW = -1, firstFallCCW = -1;
static uint32_t encEdgesCW = 0;
static uint32_t lastEncEdgeAt = 0;
static bool encSawEdge = false;
static bool encOnePinOnly = false;

static void help() {
  Serial.println(F("\nCommands: g=guided test  m=monitor (real drivers)  d=discover  s=skip step  h=help"));
}

static void prompt() {
  if (step < NUM_KEYS)
    Serial.printf("\n[%d/%d] Press and release KEY %d\n", step + 1, STEP_DONE, step + 1);
  else if (step == STEP_CW)
    Serial.printf("\n[%d/%d] Turn the encoder CLOCKWISE exactly %d clicks, then let go\n", step + 1, STEP_DONE, CLICKS);
  else if (step == STEP_CCW)
    Serial.printf("\n[%d/%d] Turn the encoder COUNTER-CLOCKWISE exactly %d clicks, then let go\n", step + 1, STEP_DONE, CLICKS);
  else if (step == STEP_PRESS)
    Serial.printf("\n[%d/%d] Press the encoder knob\n", step + 1, STEP_DONE);
}

static void summary();
static void startMonitorFromSummary();

static void advance() {
  step++;
  pendingKey = -1;
  encSawEdge = false;
  watchResetEdges();
  if (step >= STEP_DONE) summary();
  else prompt();
}

static void startGuided() {
  mode = Mode::Guided;
  step = 0;
  for (auto& k : keyPin) k = -1;
  encA = encB = encSW = firstFallCW = firstFallCCW = -1;
  encEdgesCW = 0;
  encOnePinOnly = false;
  watchBegin(true);
  Serial.println(F("\n===== GUIDED TEST (watching all GPIOs) ====="));
  prompt();
}

// the two watcher pins with the most edges this step
static void topTwo(int& a, int& b) {
  a = b = -1;
  for (int i = 0; i < NCAND; i++) {
    if (!W[i].edges) continue;
    if (a < 0 || W[i].edges > W[a].edges) { b = a; a = i; }
    else if (b < 0 || W[i].edges > W[b].edges) b = i;
  }
}

static void guidedEdge(uint8_t i, bool low) {
  if (step != STEP_CW && step != STEP_CCW) return;
  lastEncEdgeAt = millis();
  if (!encSawEdge && low) {  // first pin to fall tells us the direction
    encSawEdge = true;
    (step == STEP_CW ? firstFallCW : firstFallCCW) = i;
  }
}

static void finishEncoderStep() {
  int a, b;
  topTwo(a, b);
  if (step == STEP_CW) {
    if (b < 0) {
      Serial.printf("  FAIL  only GPIO%u changed - the other encoder wire isn't reaching a pin\n", W[a].gpio);
      encOnePinOnly = true;
      encA = a;
    } else {
      encEdgesCW = W[a].edges + W[b].edges;
      // By the decoder's convention, the pin that falls first on a CW turn is A.
      encA = (firstFallCW == a || firstFallCW == b) ? firstFallCW : a;
      encB = (encA == a) ? b : a;
      Serial.printf("  OK    encoder on GPIO%u + GPIO%u  (%lu edges for %d clicks)\n", W[encA].gpio,
                    W[encB].gpio, (unsigned long)encEdgesCW, CLICKS);
    }
  } else {
    if (encB >= 0 && firstFallCCW == encB)
      Serial.println(F("  OK    counter-clockwise leads with the other pin, direction is consistent"));
    else if (encB >= 0)
      Serial.println(F("  WARN  CCW led with the same pin as CW - contacts may be bouncy; rerun g to confirm"));
  }
  advance();
}

static void guidedPress(uint8_t i, bool pressed) {
  const uint8_t g = W[i].gpio;
  if (step < NUM_KEYS) {
    if (i == encA || i == encB) return;
    if (pressed) { if (pendingKey < 0) pendingKey = i; return; }
    if ((int)i != pendingKey) return;
    keyPin[step] = g;
    Serial.printf("  OK    KEY %d -> GPIO%u", step + 1, g);
    for (int k = 0; k < step; k++)
      if (keyPin[k] == g) Serial.printf("   !! same pin as KEY %d - pressed the wrong key, or two keys on one wire?", k + 1);
    if (KEY_PINS[step] == g) Serial.print("   (matches config)");
    else Serial.printf("   (config.h says GPIO%u)", KEY_PINS[step]);
    if (W[i].edges > 2) Serial.printf("   [%lu bounce edges]", (unsigned long)(W[i].edges - 2));
    Serial.println();
    advance();
  } else if (step == STEP_PRESS) {
    if (i == encA || i == encB) return;  // rotation, not the push switch
    if (pressed) { pendingKey = i; return; }
    if ((int)i != pendingKey) return;
    encSW = i;
    Serial.printf("  OK    encoder button -> GPIO%u%s\n", g, g == ENC_PIN_SW ? "   (matches config)" : "");
    advance();
  }
}

static void summary() {
  Serial.println(F("\n================ SUMMARY ================"));
  bool complete = true, matches = true;
  for (int k = 0; k < NUM_KEYS; k++) {
    if (keyPin[k] < 0) { complete = false; Serial.printf("  KEY %d: NOT FOUND\n", k + 1); }
    else if (keyPin[k] != KEY_PINS[k]) matches = false;
  }
  const bool encOk = encA >= 0 && encB >= 0 && !encOnePinOnly;
  if (!encOk) { complete = false; Serial.println(F("  ENCODER rotation: NOT FOUND")); }
  if (encSW < 0) { complete = false; Serial.println(F("  ENCODER button: NOT FOUND")); }

  int32_t perClick = encOk ? (encEdgesCW + CLICKS / 2) / CLICKS : ENC_STEPS_PER_DETENT;
  if (perClick != 2 && perClick != 4) perClick = perClick < 3 ? 2 : 4;  // snap to the two real EC11 types

  if (encOk) matches &= W[encA].gpio == ENC_PIN_A && W[encB].gpio == ENC_PIN_B;
  if (encSW >= 0) matches &= W[encSW].gpio == ENC_PIN_SW;
  matches &= perClick == ENC_STEPS_PER_DETENT && !ENC_REVERSE;

  Serial.println(F("\n  Detected wiring - paste into include/config.h:\n"));
  Serial.print(F("  constexpr uint8_t KEY_PINS[NUM_KEYS] = {"));
  for (int k = 0; k < NUM_KEYS; k++) {
    if (keyPin[k] >= 0) Serial.print(keyPin[k]); else Serial.print("??");
    Serial.print(k < NUM_KEYS - 1 ? ", " : "};\n");
  }
  if (encOk) {
    Serial.printf("  constexpr uint8_t ENC_PIN_A  = %u;\n", W[encA].gpio);
    Serial.printf("  constexpr uint8_t ENC_PIN_B  = %u;\n", W[encB].gpio);
  }
  if (encSW >= 0) Serial.printf("  constexpr uint8_t ENC_PIN_SW = %u;\n", W[encSW].gpio);
  if (encOk) Serial.printf("  constexpr int32_t ENC_STEPS_PER_DETENT = %ld;\n", (long)perClick);
  Serial.println(F("  constexpr bool ENC_REVERSE = false;"));

  if (!complete)
    Serial.println(F("\n  RESULT: INCOMPLETE - something above wasn't found (try 'd' to see raw pins), then g to rerun"));
  else if (!matches)
    Serial.println(F("\n  RESULT: WIRING OK, config.h is out of date -> paste the block above, reflash hwtest, run g again"));
  else
    Serial.println(F("\n  RESULT: ALL INPUTS FOUND AND config.h MATCHES\n"
                     "  Final check with the real drivers is next: every key should print once,\n"
                     "  one knob click = one ENCODER +1 (CW) / -1 (CCW)."));
  Serial.println(F("========================================="));

  if (complete && matches) startMonitorFromSummary();
}

// ---------------------------------------------------------------------
//  Monitor mode — the real drivers from input.cpp, driven by config.h
// ---------------------------------------------------------------------
static Keys keys;
static Encoder encoder;
static Debounced encButton;

static void startMonitor() {
  mode = Mode::Monitor;
  keys.begin();
  encoder.end();
  encoder.begin();
  encButton.begin(ENC_PIN_SW);
  Serial.println(F("\n===== MONITOR MODE (real drivers, config.h pins) ====="));
}
static void startMonitorFromSummary() { startMonitor(); }

static void onKeyMonitor(uint8_t idx, bool pressed) {
  Serial.printf("KEY %u (GPIO%u) %s   edges=%lu\n", idx + 1, KEY_PINS[idx], pressed ? "DOWN" : "UP",
                (unsigned long)keys.key(idx).rawEdges);
  if (!pressed) keys.key(idx).rawEdges = 0;
}

// ---------------------------------------------------------------------
//  Discover mode — print every raw pin change
// ---------------------------------------------------------------------
static void startDiscover() {
  mode = Mode::Discover;
  encoder.end();
  watchBegin(false);
  Serial.println(F("\n===== DISCOVER MODE - every GPIO change is printed (g/m to leave) ====="));
}

static void discoverPress(uint8_t i, bool pressed) {
  Serial.printf("  GPIO%-2u %s   [%s]\n", W[i].gpio, pressed ? "LOW  (pressed)" : "HIGH (released)",
                configRole(W[i].gpio));
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n\n######## ESP32 Macro Keypad - HARDWARE TEST ########"));
  wiringChecks();
  help();
  startGuided();
}

void loop() {
  if (Serial.available()) {
    switch (tolower(Serial.read())) {
      case 'g': encoder.end(); startGuided(); break;
      case 'd': startDiscover(); break;
      case 'm': startMonitor(); break;
      case 's':
        if (mode == Mode::Guided && step < STEP_DONE) { Serial.println(F("  skipped")); advance(); }
        break;
      case 'h': help(); break;
      default: break;
    }
  }

  switch (mode) {
    case Mode::Guided:
      watchUpdate(guidedEdge, guidedPress);
      if ((step == STEP_CW || step == STEP_CCW) && encSawEdge && millis() - lastEncEdgeAt > ENC_SETTLE_MS)
        finishEncoderStep();
      break;

    case Mode::Discover:
      watchUpdate([](uint8_t, bool) {}, discoverPress);
      break;

    case Mode::Monitor: {
      keys.update(onKeyMonitor);
      int32_t d = encoder.readDetents();
      if (d)
        Serial.printf("ENCODER %+ld %s   (raw %ld, invalid %lu)\n", (long)d, d > 0 ? "CW" : "CCW",
                      (long)encoder.rawSteps(), (unsigned long)encoder.invalidTransitions());
      BtnEvent b = encButton.update();
      if (b != BtnEvent::None) Serial.printf("ENC BUTTON %s\n", b == BtnEvent::Pressed ? "DOWN" : "UP");
      break;
    }
  }
  delay(1);
}
