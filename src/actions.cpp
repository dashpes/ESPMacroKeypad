#include "actions.h"
#include "ui.h"
#include <Preferences.h>

// ===================== SNIPPETS — edit me =====================
// Hold SIG to open the list, turn the knob, push to type one.
// Plain ASCII only (the pad types it as US keystrokes). '\n' presses Enter.
const Snippet SNIPPETS[] = {
    // name: keep it short (the first 9 characters show under the SIG key)
    {"Thanks",  "Thanks,\nDaniel"},
    {"Best",    "Best,\nDaniel"},
    {"LGTM",    "LGTM, approving."},
    {"On it",   "On it, will follow up shortly."},
    {"Standup", "Yesterday: \nToday: \nBlockers: none"},
};
const uint8_t NUM_SNIPPETS = sizeof(SNIPPETS) / sizeof(SNIPPETS[0]);

// ===================== KEYMAP — edit me =====================
// Labels: up to 5 characters show on the screen. The small line under each
// label (CMD+C, TRACK, ...) is generated from the action itself.
const Layer LAYERS[] = {
    {"Media", "VOL", "VOLUME",
     {MEDIA("Play", Media::PlayPause), MEDIA("Prev", Media::Prev), MEDIA("Next", Media::Next), KEY("Spot", MOD_GUI, ' '),
      KEY("Copy", MOD_GUI, 'c'), KEY("Paste", MOD_GUI, 'v'), KEY("Shot", MOD_GUI | MOD_SHIFT, '4'), NEXT_LAYER()},
     MEDIA("Vol+", Media::VolUp), MEDIA("Vol-", Media::VolDown), MEDIA("Mute", Media::Mute),
     // holds:  1       2       3       4       5       6       7       8
     {NONE(), NONE(), NONE(), NONE(), NONE(), NONE(), NONE(), AMBIENT()}},

    {"Dev", "TABS", "TABS",
     {KEY("Save", MOD_GUI, 's'), KEY("Find", MOD_GUI, 'f'), KEY("Term", MOD_CTRL, '`'), KEY("Cmt", MOD_GUI, '/'),
      KEY("Undo", MOD_GUI, 'z'), KEY("Redo", MOD_GUI | MOD_SHIFT, 'z'), SNIPPET("Sig"), NEXT_LAYER()},
     KEY("Tab>", MOD_CTRL, '\t'), KEY("<Tab", MOD_CTRL | MOD_SHIFT, '\t'), NONE(),
     // holds:  1       2       3       4       5       6       7               8
     {NONE(), NONE(), NONE(), NONE(), NONE(), NONE(), SNIPPET_MENU(), AMBIENT()}},
};
const uint8_t NUM_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);
// ============================================================

uint8_t currentLayer() { return ui::layer(); }

// ---------------- snippets ----------------
static uint8_t g_snip = 0;
static Preferences g_prefs;

void actionsBegin() {
  g_prefs.begin("spacedeck", false);
  g_snip = g_prefs.getUChar("snip", 0);
  if (g_snip >= NUM_SNIPPETS) g_snip = 0;
}
uint8_t currentSnippet() { return g_snip; }

void pickSnippet(uint8_t i) {
  if (i >= NUM_SNIPPETS) return;
  if (i != g_snip) { g_snip = i; g_prefs.putUChar("snip", i); }
  hid.typeText(SNIPPETS[i].text);
}
bool hasHold(const Action& a) { return a.type != ActionType::None; }

void runAction(const Action& a) {
  switch (a.type) {
    case ActionType::None:      break;
    case ActionType::Key:       hid.tapKey(a.mods, a.key); break;
    case ActionType::Media:     hid.tapMedia(a.media); break;
    case ActionType::Text:      hid.typeText(a.text); break;
    case ActionType::NextLayer: ui::setLayer((ui::layer() + 1) % NUM_LAYERS); break;
    case ActionType::Ambient:   ui::showAmbient(); break;
    case ActionType::Snippet:   if (NUM_SNIPPETS) hid.typeText(SNIPPETS[g_snip].text); break;
    case ActionType::SnippetMenu: ui::openMenu(ui::Menu::Snippets, NUM_SNIPPETS, g_snip); break;
  }
}

// ---------------- labels for the e-paper ----------------
static void copyTrunc(char* out, size_t n, const char* s, size_t maxChars) {
  size_t lim = min(n - 1, maxChars), i = 0;
  for (; s && s[i] && i < lim; i++) out[i] = toupper((unsigned char)s[i]);
  out[i] = 0;
}

void actionMainLabel(const Action& a, uint8_t layer, char* out, size_t n) {
  if (a.type == ActionType::NextLayer) {
    char buf[8];
    copyTrunc(buf, sizeof buf, LAYERS[(layer + 1) % NUM_LAYERS].name, 3);
    snprintf(out, n, "%s>", buf);   // "DEV>", "MED>"
    return;
  }
  copyTrunc(out, n, a.label, 5);
}

static const char* keyName(uint8_t k, char* one) {
  switch (k) {
    case '\t': return "TAB";
    case '\n': return "ENTER";
    case ' ':  return "SPACE";
    case 0x1B: return "ESC";
    case '\b': return "BKSP";
  }
  one[0] = toupper(k); one[1] = 0;
  return one;
}

void actionSubLabel(const Action& a, char* out, size_t n) {
  char buf[24] = "", one[2];
  switch (a.type) {
    case ActionType::None:      break;
    case ActionType::NextLayer: strcpy(buf, "LAYER"); break;
    case ActionType::Ambient:   strcpy(buf, "AMBIENT"); break;
    case ActionType::Text:      strcpy(buf, "TYPE TEXT"); break;
    case ActionType::Snippet:   if (NUM_SNIPPETS) strncpy(buf, SNIPPETS[g_snip].name, sizeof buf - 1); break;
    case ActionType::SnippetMenu: strcpy(buf, "SNIPPETS"); break;
    case ActionType::Media:
      switch (a.media) {
        case Media::PlayPause: strcpy(buf, "PAUSE"); break;
        case Media::Next:
        case Media::Prev:      strcpy(buf, "TRACK"); break;
        case Media::VolUp:
        case Media::VolDown:   strcpy(buf, "VOLUME"); break;
        case Media::Mute:      strcpy(buf, "AUDIO"); break;
      }
      break;
    case ActionType::Key:
      if (a.mods & MOD_CTRL)  strcat(buf, "CTRL+");
      if (a.mods & MOD_ALT)   strcat(buf, "OPT+");
      if (a.mods & MOD_GUI)   strcat(buf, "CMD+");
      if (a.mods & MOD_SHIFT) strcat(buf, "SH+");
      strcat(buf, keyName(a.key, one));
      break;
  }
  copyTrunc(out, n, buf, 9);   // 9 x 6 px fits inside a 61 px cell
}
