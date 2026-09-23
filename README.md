# ESP32 Macro Keypad

8 MX keys + EC11 encoder + (soon) Waveshare 2.13" e-Paper V4, on a DOIT ESP32 DevKit V1.

## Layout

```
platformio.ini        two envs: firmware (default) and hwtest
include/config.h      ALL pin assignments + encoder/debounce settings  <- edit first
include/input.h       Debounced switch, Keys, Encoder (interrupt quadrature decoder)
src/input.cpp
include/actions.h     Action / Layer types
src/actions.cpp       THE KEYMAP (layers, what each key + encoder does)
include/hid.h         HID output — stub that logs to Serial (BLE comes next)
include/display.h     e-paper — stub that logs to Serial
src/main.cpp          firmware loop
src/hwtest/hwtest.cpp hardware test
```

## 1. Run the hardware test first

```
pio run -e hwtest -t upload -t monitor
```
(VS Code: PlatformIO sidebar -> hwtest -> Upload and Monitor.)

1. **Wiring checks** run at boot. Don't touch anything. Flags pins stuck LOW,
   pins shorted together, and pins with external pull-ups (normal for the
   encoder breakout board, suspicious on a plain key).
2. **Guided test** watches *every* usable GPIO, not just `config.h`. Press
   KEY 1..8 in order, turn the knob 3 clicks CW, 3 clicks CCW, press it. It
   records which GPIO each one is on, works out encoder A/B from which pin
   leads on a clockwise turn, and measures steps per click.
3. **Summary** prints a ready-to-paste `config.h` block. If config already
   matches, it drops straight into monitor mode so you can confirm the real
   drivers: each key prints once, one click = one `ENCODER +1/-1`.

Serial commands (type in the monitor):

| key | does |
|---|---|
| `g` | restart the guided test |
| `d` | **discover mode** — watches every usable GPIO and prints whichever one changes. Use it when a key does nothing, or to map pins from scratch |
| `m` | monitor mode — real firmware drivers using `config.h` pins; prints every key/encoder event |
| `s` | skip the current guided step |
| `h` | help |

Current wiring: keys 13, 14, 26, 27, 19, 21, 22, 23; encoder A/B 32/33, switch 25.

Encoder notes the test will give you:
- counts backwards -> `ENC_REVERSE = true` (or swap A/B)
- 3 clicks shows as 1-2 counts or 6 -> change `ENC_STEPS_PER_DETENT` (4 or 2)
- lots of "invalid transitions" -> noisy contacts; add 10k pull-ups + 10 nF caps on A/B

## 2. Firmware (SpaceDeck over Bluetooth)

```
pio run -e firmware -t upload -t monitor
```

**Pair it:** Mac -> System Settings -> Bluetooth -> "SpaceDeck" -> Connect.
The monitor prints `[hid] connected`. It bonds, so after this it reconnects on
its own (give it a few seconds after the Mac wakes).

**Test it** with TextEdit focused:

| Layer | Key | Expect |
|---|---|---|
| Media | 1-4 | play/pause, previous, next, Spotlight (Cmd+Space) |
| Media | 5-6 | Cmd+C / Cmd+V |
| Media | 7 | Cmd+Shift+4 (screenshot crosshair, Esc to cancel) |
| Media | 8 | switch to Dev layer (hold = ambient screen) |
| Media | knob | volume up/down, one step per click; press = mute |
| Dev | 1-6 | Cmd+S, Cmd+F, Ctrl+`, Cmd+/, Cmd+Z, Cmd+Shift+Z |
| Dev | 7 | types the current snippet (hold = snippet menu) |
| Dev | 8 | back to Media |
| Dev | knob | Ctrl+Tab / Ctrl+Shift+Tab (next/previous tab) |

Presses while not connected are dropped (monitor says so), not queued.

**If it misbehaves:**
- Won't reconnect after reflashing -> on the Mac, "Forget This Device" on
  SpaceDeck and pair again.
- Missed or doubled keystrokes -> raise `BLE_SEND_DELAY_MS` in `config.h`.
- Rename it -> `BLE_DEVICE_NAME` in `config.h`, then forget + re-pair on the Mac
  (macOS caches the old name).

Edit what the keys do in `src/actions.cpp` (keymap, long-press actions, snippets).

## 3. Screen, long presses, snippets

- **E-paper** (`src/display.cpp`, GxEPD2): draws the screens from
  `design/spacedeck-screens.html` with the same 5x7 font (`include/font5x7.h`
  is generated from the preview). It runs in its own task, so refreshes never
  block the keys. Works fine with no screen attached. If the picture is upside
  down in the case, set `EPD_ROTATION = 3` in `config.h`.
- **Long presses:** each layer has a `holds` list in the keymap. Keys with a
  hold action show a small dot on screen and fire their tap on release; other
  keys fire on press. Hold threshold: `KEY_HOLD_MS`.
  - Hold **8** -> ambient wave screen (any key wakes it; that press does nothing else).
- **Snippets** (Dev layer, **SIG** key 7): tap types the current snippet; hold
  opens the list; turn the knob, push to type it and make it the default (kept
  across power cycles). Any key or 10 s idle closes the menu. Edit the
  `SNIPPETS` list in `actions.cpp`.
- Serial command `a` jumps to the ambient screen for testing.
- Timing knobs (popup length, idle time before ambient, refresh cadence) are in
  `config.h` under UI timing.

Flash layout: the firmware env uses `huge_app.csv` (3 MB app, no OTA) because
Bluetooth alone is ~1.1 MB.
