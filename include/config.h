#pragma once
#include <Arduino.h>

// =====================================================================
//  Board: DOIT ESP32 DevKit V1 (classic ESP32, USB-C via USB-UART chip)
//  Classic ESP32 has no native USB, so the keyboard will talk over BLE.
// =====================================================================
//
//  PIN MAP — run the `hwtest` env; its guided test watches every GPIO,
//  maps each key/encoder pin itself, and prints a ready-to-paste block.
//  Status: confirmed from your wiring. Key 5 = GPIO19 was inferred from the
//          first test run, and the guided test decides which of 32/33 is A.
//
//  Switches are wired pin -> switch -> GND (internal pull-ups, active LOW).
//  Avoid GPIO 6-11 (flash), 1/3 (USB serial). GPIO 34-39 have NO internal
//  pull-ups (need external resistors). GPIO 18 is not used by the keypad.

constexpr uint8_t NUM_KEYS = 8;

// Physical order: KEY 1 .. KEY 8 (top-left to bottom-right)
constexpr uint8_t KEY_PINS[NUM_KEYS] = {13, 14, 26, 27, 19, 21, 22, 23};

// EC11 rotary encoder breakout (has its own pull-ups on A/B/SW)
constexpr uint8_t ENC_PIN_A  = 32;
constexpr uint8_t ENC_PIN_B  = 33;
constexpr uint8_t ENC_PIN_SW = 25;
// Quadrature steps per detent "click". Most EC11s = 4; some = 2.
// hwtest tells you which one yours is.
constexpr int32_t ENC_STEPS_PER_DETENT = 4;
// Flip if clockwise counts the wrong way (or swap A/B above).
constexpr bool ENC_REVERSE = false;

constexpr uint32_t DEBOUNCE_MS = 5;

// Waveshare 2.13" e-Paper V4. GPIO23 (default SPI MOSI) is
// KEY 8, so SPI is remapped through the GPIO matrix to these free pins:
//   CLK 18, DIN 17, CS 5, DC 16, RST 15, BUSY 4, VCC 3V3, GND GND
// (Keep BUSY off GPIO12: if it's HIGH at power-up the ESP32 won't boot.)
constexpr uint8_t EPD_PIN_CLK  = 18;
constexpr uint8_t EPD_PIN_DIN  = 17;
constexpr uint8_t EPD_PIN_CS   = 5;
constexpr uint8_t EPD_PIN_DC   = 16;
constexpr uint8_t EPD_PIN_RST  = 15;
constexpr uint8_t EPD_PIN_BUSY = 4;
// 1 or 3 = landscape. If the picture is upside down in the case, switch to the other.
constexpr uint8_t EPD_ROTATION = 1;
// ---- Refresh policy (wear + ghosting) ----
// Panel is rated ~1,000,000 refreshes; the boot log shows the running total.
// Full (flashing) refreshes clear ghosting. They happen after big screen
// changes (boot, link screens, host switch, leaving the wave), once when the
// pad has sat untouched for a while, and as a rare safety net during use.
constexpr uint32_t EPD_FULL_REFRESH_EVERY = 100;     // safety net: partials in a row
constexpr uint32_t EPD_IDLE_CLEAN_AFTER   = 5;       // ...and at least this many partials built up
constexpr uint32_t EPD_IDLE_CLEAN_MS      = 120000;  // untouched this long -> one full clean
constexpr bool     EPD_FULL_ON_LAYER_SWITCH = false; // true = no layer ghosting, but flash + ~2 s per switch
constexpr uint32_t EPD_POWER_OFF_MS       = 1000;    // cut panel high voltage this long after a refresh

// ---------------- UI timing ----------------
constexpr uint32_t KEY_HOLD_MS         = 600;             // long-press threshold
constexpr uint32_t UI_POPUP_MS         = 1500;            // knob popup stays this long after the last click
// Mark a key while pressed (hollow number tag + thicker border; shortcut/media
// keys only). Costs two refreshes per press; false = snappiest + half the wear.
constexpr bool     UI_PRESS_FLASH      = true;
constexpr uint32_t UI_PRESS_FLASH_MS   = 300;             // how long the press marker shows
constexpr uint32_t UI_MENU_MS          = 10000;           // snippet menu closes after this long untouched
constexpr uint32_t UI_LINKED_MS        = 1600;            // "LINK ESTABLISHED" before the layer view
constexpr uint32_t UI_AMBIENT_AFTER_MS = 5UL * 60 * 1000; // idle time before the ambient wave
constexpr uint32_t AMBIENT_FRAME_MS    = 5000;            // slow drift: one frame every 5 s
constexpr uint32_t AMBIENT_RUN_MS      = 15UL * 60 * 1000;// then rest on a still frame (screen idle)
constexpr uint32_t AMBIENT_FULL_EVERY  = 24;              // full clean every 24 frames (~2 min) while drifting
constexpr uint32_t UI_LINK_ANIM_MS     = 60000;           // link screens animate 1 min, then hold still
constexpr uint8_t  AMBIENT_STYLE       = 0;               // 0 = line, 1 = bars, 2 = scope

// ---------------- Bluetooth ----------------
constexpr const char* BLE_DEVICE_NAME  = "SpaceDeck";

// Host slots: one computer per slot. Each slot is its own Bluetooth device
// ("SpaceDeck HOST2"), so computers never fight over the pad.
// Hold the knob to open the HOSTS menu. Names: 5 characters max.
constexpr uint8_t NUM_HOSTS = 3;
constexpr const char* HOST_NAMES[NUM_HOSTS] = {"HOME", "WORK", "MINI"};  // Personal Mac, Work Mac, Mac Mini
constexpr uint32_t KNOB_HOLD_MS = 800;   // hold the knob this long for the HOSTS menu
constexpr const char* BLE_MANUFACTURER = "Daniel";
// Gap between HID reports. Too small and macOS can drop keystrokes.
constexpr uint32_t BLE_SEND_DELAY_MS = 8;
