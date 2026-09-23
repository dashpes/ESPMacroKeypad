#pragma once
#include <Arduino.h>
#include "config.h"

enum class BtnEvent : uint8_t { None, Pressed, Released };

// One debounced active-LOW switch.
class Debounced {
 public:
  void begin(uint8_t pin);
  BtnEvent update();              // call every loop
  bool isDown() const { return stable_; }
  uint8_t pin() const { return pin_; }
  uint32_t rawEdges = 0;          // every raw transition (bounce diagnostics)

 private:
  uint8_t pin_ = 0;
  bool stable_ = false, lastRaw_ = false;
  uint32_t changedAt_ = 0;
};

class Keys {
 public:
  using Callback = void (*)(uint8_t keyIndex, bool pressed);
  void begin();
  void update(Callback cb);
  Debounced& key(uint8_t i) { return keys_[i]; }

 private:
  Debounced keys_[NUM_KEYS];
};

// Interrupt-driven quadrature decoder with invalid-transition counting.
class Encoder {
 public:
  void begin();
  void end();
  int32_t readDetents();          // whole clicks since last call (+ = CW)
  int32_t rawSteps() const;       // total quadrature steps since resetStats()
  uint32_t invalidTransitions() const;  // both bits changed at once = noise/bad wiring
  void resetStats();
};

bool pinHasInternalPullup(uint8_t pin);
