#include "input.h"
#include "soc/gpio_struct.h"

bool pinHasInternalPullup(uint8_t pin) { return pin < 34; }

// ---------------- Debounced ----------------
void Debounced::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
  if (!pinHasInternalPullup(pin_))
    Serial.printf("NOTE: GPIO%u has no internal pull-up - needs an external resistor (encoder boards have one)\n", pin_);
  delayMicroseconds(50);
  stable_ = lastRaw_ = (digitalRead(pin_) == LOW);
  changedAt_ = millis();
  rawEdges = 0;
}

BtnEvent Debounced::update() {
  const uint32_t now = millis();
  const bool raw = (digitalRead(pin_) == LOW);
  if (raw != lastRaw_) {
    lastRaw_ = raw;
    changedAt_ = now;
    rawEdges++;
  } else if (raw != stable_ && now - changedAt_ >= DEBOUNCE_MS) {
    stable_ = raw;
    return stable_ ? BtnEvent::Pressed : BtnEvent::Released;
  }
  return BtnEvent::None;
}

// ---------------- Keys ----------------
void Keys::begin() {
  for (uint8_t i = 0; i < NUM_KEYS; i++) keys_[i].begin(KEY_PINS[i]);
}

void Keys::update(Callback cb) {
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    BtnEvent e = keys_[i].update();
    if (e != BtnEvent::None && cb) cb(i, e == BtnEvent::Pressed);
  }
}

// ---------------- Encoder ----------------
namespace {
volatile int32_t g_pending = 0;   // steps not yet turned into detents
volatile int32_t g_raw = 0;       // total steps (diagnostics)
volatile uint32_t g_invalid = 0;
volatile uint8_t g_prev = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
bool g_attached = false;

// index = (prev << 2) | cur, state = (A << 1) | B
const int8_t kQuad[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};

inline uint8_t IRAM_ATTR fastRead(uint8_t pin) {
  return pin < 32 ? (GPIO.in >> pin) & 1 : (GPIO.in1.data >> (pin - 32)) & 1;
}

void IRAM_ATTR encIsr() {
  const uint8_t cur = (fastRead(ENC_PIN_A) << 1) | fastRead(ENC_PIN_B);
  portENTER_CRITICAL_ISR(&g_mux);
  if ((g_prev ^ cur) == 3) {
    g_invalid++;
  } else {
    int8_t d = kQuad[(g_prev << 2) | cur];
    if (ENC_REVERSE) d = -d;
    g_pending += d;
    g_raw += d;
  }
  g_prev = cur;
  portEXIT_CRITICAL_ISR(&g_mux);
}
}  // namespace

void Encoder::begin() {
  pinMode(ENC_PIN_A, INPUT_PULLUP);
  pinMode(ENC_PIN_B, INPUT_PULLUP);
  delayMicroseconds(50);
  g_prev = (digitalRead(ENC_PIN_A) << 1) | digitalRead(ENC_PIN_B);
  resetStats();
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_B), encIsr, CHANGE);
  g_attached = true;
}

void Encoder::end() {
  if (!g_attached) return;
  g_attached = false;
  detachInterrupt(digitalPinToInterrupt(ENC_PIN_A));
  detachInterrupt(digitalPinToInterrupt(ENC_PIN_B));
}

int32_t Encoder::readDetents() {
  portENTER_CRITICAL(&g_mux);
  const int32_t d = g_pending / ENC_STEPS_PER_DETENT;
  g_pending -= d * ENC_STEPS_PER_DETENT;
  portEXIT_CRITICAL(&g_mux);
  return d;
}

int32_t Encoder::rawSteps() const { return g_raw; }
uint32_t Encoder::invalidTransitions() const { return g_invalid; }

void Encoder::resetStats() {
  portENTER_CRITICAL(&g_mux);
  g_pending = 0;
  g_raw = 0;
  g_invalid = 0;
  portEXIT_CRITICAL(&g_mux);
}
