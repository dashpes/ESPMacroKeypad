#include "display.h"
#include "config.h"
#include "ui.h"
#include "actions.h"
#include "font5x7.h"
#include "hid.h"
#include <Preferences.h>

#include <SPI.h>
#include <GxEPD2_BW.h>

namespace display {
namespace {

// Waveshare 2.13" V4 = GDEY0213B74 panel (SSD1680). Full-height buffer: one page.
GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT>
    epd(GxEPD2_213_GDEY0213B74(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY));

constexpr int W = 250, H = 122;
// Hybrid retro terminal: light background + dark text (what e-paper does best:
// least ghosting, no fading blacks), with an inverted header bar and popups.
constexpr uint16_t BG = GxEPD_WHITE, FG = GxEPD_BLACK;

// ---------------- primitives (same as the preview) ----------------
inline void px(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) epd.drawPixel(x, y, c); }
inline void rect(int x, int y, int w, int h, uint16_t c) { if (w > 0 && h > 0) epd.fillRect(x, y, w, h, c); }
inline void frame(int x, int y, int w, int h, uint16_t c) { epd.drawRect(x, y, w, h, c); }
void hdots(int y, int x0, int x1, uint16_t c, int step = 2) { for (int x = x0; x <= x1; x += step) px(x, y, c); }

// adv = pixels per character (default 6 * scale); 5-letter key labels use one less so they clear the cell borders
int tw(const char* s, int sc, int adv = 0) { int n = strlen(s); if (!adv) adv = FONT_ADVANCE * sc; return n ? (n - 1) * adv + FONT_W * sc : 0; }

void text(const char* s, int x, int y, int sc, uint16_t c, int adv = 0) {
  if (!adv) adv = FONT_ADVANCE * sc;
  for (; *s; s++) {
    int ch = toupper((unsigned char)*s);
    if (ch < FONT_FIRST || ch > FONT_LAST) ch = '?';
    const uint8_t* g = FONT5X7[ch - FONT_FIRST];
    for (int r = 0; r < FONT_H; r++)
      for (int col = 0; col < FONT_W; col++)
        if (g[r] & (0x10 >> col)) rect(x + col * sc, y + r * sc, sc, sc, c);
    x += adv;
  }
}
void ctext(const char* s, float cx, int y, int sc, uint16_t c, int adv = 0) { text(s, (int)lroundf(cx - tw(s, sc, adv) / 2.0f), y, sc, c, adv); }

void icon(const char* const rows[], int n, int x, int y, uint16_t c) {
  for (int j = 0; j < n; j++)
    for (int i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#') px(x + i, y + j, c);
}
void arc(int cx, int cy, float r, float a0, float a1, uint16_t c, bool dash = false) {
  int n = 0;
  for (float a = a0; a <= a1; a += 0.6f / r) {
    n++;
    if (dash && n % 4 < 2) continue;
    px(lroundf(cx + r * cosf(a)), lroundf(cy + r * sinf(a)), c);
  }
}
void ellipse(int cx, int cy, float rx, float ry, uint16_t c, bool dash) {
  int n = 0;
  for (float a = 0; a < 2 * PI; a += 0.8f / max(rx, ry)) {
    n++;
    if (dash && n % 5 < 2) continue;
    px(lroundf(cx + rx * cosf(a)), lroundf(cy + ry * sinf(a)), c);
  }
}

const char* const BT[] = {"..#..", "..##.", "#.#.#", ".###.", "..#..", ".###.", "#.#.#", "..##.", "..#.."};

enum class Bt : uint8_t { None, On, Blink, Off };

// ---------------- screens ----------------
uint32_t g_frame = 0;   // animation clock for this render (millis)
uint32_t g_refreshes = 0;   // lifetime panel refreshes (saved to flash every 50)

bool blinkOn() { return (g_frame / 1000) % 2 == 0; }

void header(const char* title, const char* right, Bt bt, bool cursorBlink) {
  char t[24];
  snprintf(t, sizeof t, ">%s", title);
  rect(0, 0, W, 13, FG);   // header bar
  text(t, 3, 3, 1, BG);
  if (!cursorBlink || blinkOn()) rect(3 + tw(t, 1) + 3, 3, 5, 7, BG);
  if (right && *right) text(right, 236 - tw(right, 1) - 4, 3, 1, BG);
  if (bt == Bt::On || (bt == Bt::Blink && blinkOn())) icon(BT, 9, 241, 2, BG);
  if (bt == Bt::Off) { icon(BT, 9, 241, 2, BG); for (int i = 0; i < 9; i++) px(239 + i, 2 + i, BG); }
}

constexpr uint32_t BOOT_SPLASH_MS = 2600, BOOT_LINE_MS = 450, BOOT_READY_MS = 1200;
constexpr int BOOT_LINES = 5;

void drawBoot(const ui::Snapshot& s) {
  const uint32_t t = g_frame - s.screenSince;
  if (t < BOOT_SPLASH_MS) {
    const float a = t / 1000.0f * 2.2f;
    ellipse(125, 50, 104, 22, FG, true);
    ctext("SPACEDECK", 125, 40, 3, FG);
    const int sx = lroundf(125 + 104 * cosf(a)), sy = lroundf(50 + 22 * sinf(a));
    if (!(sinf(a) < 0 && abs(sx - 125) < 82)) { rect(sx - 1, sy - 1, 3, 3, FG); px(sx - 3, sy, FG); px(sx + 3, sy, FG); }
    ctext("MACROPAD // BUILD 0.1", 125, 92, 1, FG);
    if ((t / 500) % 2 == 0) ctext("SYSTEMS CHECK", 125, 106, 1, FG);
    return;
  }
  header("BOOT", "", Bt::None, false);
  char panel[24];
  if (g_refreshes < 10000) snprintf(panel, sizeof panel, "%lu REFRESH", (unsigned long)g_refreshes);
  else snprintf(panel, sizeof panel, "%lu.%luK REFRESH", (unsigned long)(g_refreshes / 1000), (unsigned long)(g_refreshes / 100 % 10));
  char keys[12];
  if (s.stuckKeys) snprintf(keys, sizeof keys, "%u STUCK", s.stuckKeys);
  else snprintf(keys, sizeof keys, "%u/%u OK", NUM_KEYS, NUM_KEYS);
  const char* lines[BOOT_LINES][2] = {
      {"SPACEDECK OS", "0.1"}, {"KEYS", keys}, {"ENCODER", "OK"}, {"DISPLAY", panel}, {"BLUETOOTH", "ADV"}};
  const int shown = min<int>(BOOT_LINES, (t - BOOT_SPLASH_MS) / BOOT_LINE_MS + 1);
  for (int i = 0; i < shown; i++) {
    const int y = 20 + i * 13;
    text(lines[i][0], 6, y, 1, FG);
    const int rx = 244 - tw(lines[i][1], 1);
    hdots(y + 6, 6 + tw(lines[i][0], 1) + 4, rx - 5, FG, 3);
    text(lines[i][1], rx, y, 1, FG);
  }
  if (shown == BOOT_LINES) {
    const int y = 20 + BOOT_LINES * 13;
    text("READY", 6, y, 1, FG);
    rect(6 + tw("READY", 1) + 3, y, 5, 7, FG);
  }
}
bool bootFinished(const ui::Snapshot& s) {
  return g_frame - s.screenSince > BOOT_SPLASH_MS + BOOT_LINES * BOOT_LINE_MS + BOOT_READY_MS;
}

bool linkStill(const ui::Snapshot& s) { return g_frame - s.screenSince >= UI_LINK_ANIM_MS; }

void drawAwaiting(const ui::Snapshot& s) {
  const bool still = linkStill(s);
  header("LINK", "UNPAIRED", still ? Bt::Off : Bt::Blink, !still);
  const int phase = still ? 3 : ((g_frame - s.screenSince) * 12 / 10000) % 4;
  rect(28, 64, 4, 4, FG);
  for (int i = 1; i <= 3; i++) if (phase >= i) arc(30, 66, 8 + i * 9, -0.9f, 0.9f, FG);
  text("AWAITING LINK", 76, 30, 2, FG);
  text("PAIR FROM YOUR COMPUTER:", 76, 56, 1, FG);
  text("SETTINGS > BLUETOOTH", 76, 70, 1, FG);
  char pair[24];
  snprintf(pair, sizeof pair, "> %s", hid.deviceName());
  rect(74, 82, tw(pair, 1) + 4, 11, FG);
  text(pair, 76, 84, 1, BG);
  text("KEYS INACTIVE UNTIL PAIRED", 76, 104, 1, FG);
}

void drawLinked(const ui::Snapshot&) {
  header("LINK", "", Bt::On, false);
  ctext("LINK", 125, 30, 3, FG);
  ctext("ESTABLISHED", 125, 60, 2, FG);
  hdots(84, 60, 190, FG, 3);
  ctext("HOST CONNECTED // KEYS LIVE", 125, 94, 1, FG);
}

void drawLost(const ui::Snapshot& s) {
  const bool still = linkStill(s);
  header("LINK", "NO HOST", Bt::Off, !still);
  rect(28, 64, 4, 4, FG);
  for (int i = 1; i <= 3; i++) arc(30, 66, 8 + i * 9, -0.9f, 0.9f, FG, true);
  text("SIGNAL LOST", 76, 30, 2, FG);
  char r[16] = "RETRYING";
  const int dots = still ? 3 : ((g_frame - s.screenSince) * 15 / 10000) % 4;
  for (int i = 0; i < dots; i++) strcat(r, ".");
  text(r, 76, 56, 1, FG);
  text("KEYS PAUSED", 76, 70, 1, FG);
  text("PRESSES ARE DROPPED,", 76, 92, 1, FG);
  text("NOT QUEUED", 76, 104, 1, FG);
}

void drawLayerGrid(const ui::Snapshot& s) {
  const Layer& L = LAYERS[s.layer];
  char name[12], right[32];
  snprintf(name, sizeof name, "%s", L.name);
  for (char* p = name; *p; p++) *p = toupper(*p);
  snprintf(right, sizeof right, "%s KNOB:%s [%u/%u]", HOST_NAMES[s.host], L.knobShort, s.layer + 1, NUM_LAYERS);
  header(name, right, s.connected ? Bt::On : Bt::Off, false);  // steady cursor: no refresh every second
  for (int i = 0; i < NUM_KEYS; i++) {
    const int c = i % 4, r = i / 4;
    const int x = 1 + c * 62, y = 16 + r * 53, w = 61, h = 52;
    const bool down = s.pressedKey == i;
    frame(x, y, w, h, FG);
    if (down) {                       // pressed: hollow number tag + thicker border (small change = small ghost)
      frame(x + 1, y + 1, w - 2, h - 2, FG);
      rect(x, y, 10, 10, BG);
      frame(x, y, 10, 10, FG);
    } else {
      rect(x, y, 9, 9, FG);
    }
    char num[2] = {char('1' + i), 0};
    text(num, x + 2, y + 1, 1, down ? FG : BG);
    if (hasHold(L.holds[i])) rect(x + w - 5, y + 2, 3, 3, FG);   // has a long-press action
    char main[8], sub[12];
    actionMainLabel(L.keys[i], s.layer, main, sizeof main);
    actionSubLabel(L.keys[i], sub, sizeof sub);
    ctext(main, x + 30.5f, y + 17, 2, FG, strlen(main) >= 5 ? 11 : 0);
    ctext(sub, x + 30.5f, y + 38, 1, FG);
  }
}

void drawMenu(const ui::Snapshot& s) {
  const bool hosts = s.menu == ui::Menu::Hosts;
  header(hosts ? "HOSTS" : "SNIPPETS", hosts ? "KNOB:SCROLL PUSH:SWITCH" : "KNOB:SCROLL PUSH:SEND",
         s.connected ? Bt::On : Bt::Off, false);
  constexpr int ROWS = 4, ROW_H = 19, TOP = 17;
  const int n = s.menuCount, sel = s.menuIndex;
  int first = constrain(sel - 1, 0, max(0, n - ROWS));
  for (int r = 0; r < ROWS && first + r < n; r++) {
    const int i = first + r, y = TOP + r * ROW_H;
    const bool on = i == sel;
    if (on) rect(0, y, 242, ROW_H - 1, FG);
    char name[24];
    if (hosts) snprintf(name, sizeof name, "%u %s%s", i + 1, HOST_NAMES[i], i == s.host ? " *" : "");
    else snprintf(name, sizeof name, "%s", SNIPPETS[i].name);
    for (char* p = name; *p; p++) *p = toupper(*p);
    name[18] = 0;
    text(on ? ">" : " ", 4, y + 2, 2, on ? BG : FG);
    text(name, 18, y + 2, 2, on ? BG : FG);
  }
  // scrollbar
  for (int y = TOP; y < TOP + ROWS * ROW_H - 1; y += 2) px(246, y, FG);
  if (n > 0) {
    const int trackH = ROWS * ROW_H - 1, thumbH = max(6, trackH * min(n, ROWS) / n);
    const int thumbY = TOP + (n > 1 ? (trackH - thumbH) * sel / (n - 1) : 0);
    rect(245, thumbY, 3, thumbH, FG);
  }
  // preview of the selected snippet: up to two lines, Enter shown as a line break
  hdots(TOP + ROWS * ROW_H + 1, 0, W - 1, FG);
  char hostInfo[64];
  if (hosts) snprintf(hostInfo, sizeof hostInfo, sel == s.host ? "ACTIVE SLOT (*)\nPAIRS AS: %s %s"
                                                                : "PUSH TO SWITCH (ABOUT 2 S)\nPAIRS AS: %s %s",
                      BLE_DEVICE_NAME, HOST_NAMES[sel]);
  const char* t = hosts ? hostInfo : (n ? SNIPPETS[sel].text : "");
  char line[2][42] = {"", ""};
  int li = 0, ci = 0;
  for (; *t && li < 2; t++) {
    if (*t == '\n') { li++; ci = 0; continue; }
    if (ci < 40) { line[li][ci++] = *t; line[li][ci] = 0; }
  }
  text(line[0], 3, 99, 1, FG);
  text(line[1], 3, 111, 1, FG);
  if (*t) text("...", 247 - tw("...", 1), 111, 1, FG);
}

void drawSwitching(const ui::Snapshot& s) {
  header("HOST", "", Bt::Off, false);
  ctext("SWITCHING TO", 125, 30, 2, FG);
  ctext(HOST_NAMES[s.switchTo], 125, 54, 3, FG);
  hdots(84, 60, 190, FG, 3);
  ctext("RESTARTING BLUETOOTH...", 125, 94, 1, FG);
}

void drawPopup(const ui::Snapshot& s) {
  const Layer& L = LAYERS[s.layer];
  // right edge, centred on the screen: the knob sits just to the right
  const int w = 194, h = 72, x = W - w - 8, y = 61 - h / 2;
  rect(x - 2, y - 2, w + 4, h + 4, BG);
  rect(x, y, w, h, FG);
  frame(x + 2, y + 2, w - 4, h - 4, BG);
  for (int i = 0; i < 6; i++) rect(x + w + i, 61 - (5 - i), 1, (5 - i) * 2 + 1, FG);
  text(L.knobLong, x + 10, y + 9, 1, BG);
  if (s.popup == ui::Popup::Push) {
    char lbl[8];
    const bool none = L.encPress.type == ActionType::None;
    actionMainLabel(L.encPress, s.layer, lbl, sizeof lbl);
    ctext(none ? "---" : lbl, x + w / 2.0f, y + 26, 3, BG);
    ctext(none ? "NO ACTION ON THIS LAYER" : "TOGGLED", x + w / 2.0f, y + 54, 1, BG);
    return;
  }
  const int d = constrain(s.popupDelta, -8, 8);
  char num[8];
  snprintf(num, sizeof num, "%+d", s.popupDelta);
  text(num, x + w - 10 - tw(num, 3), y + 6, 3, BG);
  char dir[8];
  actionMainLabel(s.popupDelta > 0 ? L.encCW : L.encCCW, s.layer, dir, sizeof dir);
  text(dir, x + 10, y + 21, 1, BG);
  const int tx0 = x + 14, ty = y + 42;
  for (int k = -8; k <= 8; k++) {
    const int tx = tx0 + (k + 8) * 10;
    const bool filled = (d > 0 && k > 0 && k <= d) || (d < 0 && k < 0 && k >= d);
    if (k == 0) rect(tx + 2, ty - 3, 2, 18, BG);
    else if (filled) rect(tx, ty, 6, 12, BG);
    else frame(tx, ty, 6, 12, BG);
  }
}

void drawAmbient(const ui::Snapshot& s) {
  const uint32_t age = g_frame - s.screenSince;
  const bool resting = age >= AMBIENT_RUN_MS;
  header("AMBIENT", resting ? "RESTING - ANY KEY" : "ANY KEY: WAKE", s.connected ? Bt::On : Bt::Off, false);
  // slow drift: small step per 5 s frame; frozen once resting
  const float t = min(age, AMBIENT_RUN_MS) / 1000.0f * 0.25f;
  const int top = 17, bot = 108, mid = (top + bot) / 2;
  for (int gx = 5; gx < W; gx += 10) for (int gy = top + 2; gy <= bot; gy += 10) px(gx, gy, FG);
  auto f = [&](int x) {
    return mid + 30 * (0.55f * sinf(x * 0.045f + t * 0.9f) + 0.3f * sinf(x * 0.11f - t * 1.7f) + 0.25f * sinf(x * 0.021f + t * 0.35f));
  };
  if (AMBIENT_STYLE == 1) {          // bars
    for (int x = 2; x < W - 2; x += 4) { const float a = fabsf(f(x) - mid); rect(x, lroundf(mid - a), 2, max(1L, lroundf(a * 2)), FG); }
  } else if (AMBIENT_STYLE == 2) {   // scope
    for (int x = 0; x < W; x++) { const int y = lroundf(f(x)); px(x, y, FG); px(x, 2 * mid - y, FG); }
    hdots(mid, 0, W - 1, FG, 4);
  } else {                           // line
    int prev = lroundf(f(0));
    for (int x = 0; x < W; x++) {
      const int y = lroundf(f(x));
      rect(x, min(prev, y), 1, abs(prev - y) + 2, FG);
      prev = y;
    }
  }
  const uint32_t up = millis() / 1000;
  char tbuf[20], rbuf[24];
  snprintf(tbuf, sizeof tbuf, "T+%02lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60));
  text(tbuf, 3, 113, 1, FG);
  snprintf(rbuf, sizeof rbuf, "%s READY", LAYERS[s.layer].name);
  text(rbuf, 247 - tw(rbuf, 1), 113, 1, FG);
}

// How often each screen animates (0 = only when something changes)
// How often a screen animates (0 = only redraw when something changes).
// Everything that animates stops after a while so an idle pad stops refreshing.
uint32_t animInterval(const ui::Snapshot& s) {
  const uint32_t age = g_frame - s.screenSince;
  switch (s.screen) {
    case ui::Screen::Boot:     return 400;
    case ui::Screen::Awaiting: return age < UI_LINK_ANIM_MS ? 850 : 0;
    case ui::Screen::Lost:     return age < UI_LINK_ANIM_MS ? 700 : 0;
    case ui::Screen::Ambient:  return age < AMBIENT_RUN_MS ? AMBIENT_FRAME_MS : 0;
    default:                   return 0;
  }
}

void render(const ui::Snapshot& s) {
  epd.fillScreen(BG);
  switch (s.screen) {
    case ui::Screen::Boot:     drawBoot(s); break;
    case ui::Screen::Awaiting: drawAwaiting(s); break;
    case ui::Screen::Linked:   drawLinked(s); break;
    case ui::Screen::Lost:     drawLost(s); break;
    case ui::Screen::Layer:
      if (s.menu != ui::Menu::None) { drawMenu(s); break; }
      drawLayerGrid(s);
      if (s.popup != ui::Popup::None) drawPopup(s);
      break;
    case ui::Screen::Ambient:  drawAmbient(s); break;
    case ui::Screen::Switching: drawSwitching(s); break;
  }
}

void task(void*) {
  SPI.begin(EPD_PIN_CLK, -1, EPD_PIN_DIN, EPD_PIN_CS);
  epd.epd2.selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));   // 10 MHz: faster frame upload
  epd.init(0, true, 2, false);   // no serial diagnostics
  epd.setRotation(EPD_ROTATION);
  epd.setTextWrap(false);

  Preferences prefs;
  prefs.begin("spacedeck", false);
  g_refreshes = prefs.getULong("epdcnt", 0);
  uint32_t savedCount = g_refreshes;

  uint32_t lastRev = 0, lastDraw = 0, partials = 0;
  ui::Screen lastScreen = ui::Screen::Boot;
  uint8_t lastLayer = 0;
  bool first = true, wasAnimating = false, powered = false;

  // Screens with big bold graphics: the next screen after one gets a full refresh.
  auto heavy = [](ui::Screen sc) {
    return sc == ui::Screen::Boot || sc == ui::Screen::Ambient || sc == ui::Screen::Linked ||
           sc == ui::Screen::Switching;
  };

  for (;;) {
    const ui::Snapshot s = ui::snapshot();
    g_frame = millis();
    const uint32_t anim = animInterval(s);
    const bool changed = s.rev != lastRev;
    const bool due = anim && g_frame - lastDraw >= anim;
    const bool settle = wasAnimating && !anim && s.screen == lastScreen;   // draw the final still frame
    // Untouched for a while with ghosting built up: one full refresh to wipe it.
    const bool idleClean = !anim && partials >= EPD_IDLE_CLEAN_AFTER && g_frame - lastDraw >= EPD_IDLE_CLEAN_MS;

    if (s.screen == ui::Screen::Boot && (s.fastBoot || bootFinished(s))) { ui::bootDone(); vTaskDelay(1); continue; }

    if (first || changed || due || settle || idleClean) {
      const bool layerSwitch = s.screen == ui::Screen::Layer && lastScreen == ui::Screen::Layer && s.layer != lastLayer;
      const bool full = first || idleClean || partials >= EPD_FULL_REFRESH_EVERY ||
                        (s.screen != lastScreen && heavy(lastScreen)) ||
                        (settle && s.screen == ui::Screen::Ambient) ||
                        (s.screen == ui::Screen::Ambient && partials >= AMBIENT_FULL_EVERY) ||
                        (layerSwitch && EPD_FULL_ON_LAYER_SWITCH);
      render(s);
      epd.display(!full);
      powered = true;
      partials = full ? 0 : partials + 1;
      if (++g_refreshes - savedCount >= 50) { prefs.putULong("epdcnt", g_refreshes); savedCount = g_refreshes; }
      first = false;
      lastRev = s.rev;
      lastDraw = g_frame;
      lastScreen = s.screen;
      lastLayer = s.layer;
    }
    wasAnimating = anim != 0;

    // Waveshare: never leave the panel's high voltage on between refreshes.
    if (powered && millis() - lastDraw >= EPD_POWER_OFF_MS) { epd.powerOff(); powered = false; }

    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

}  // namespace

uint32_t refreshCount() { return g_refreshes; }

void begin() {
  // Core 0 (with the BT stack); loop() and key scanning keep core 1 to themselves.
  xTaskCreatePinnedToCore(task, "epd", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace display
