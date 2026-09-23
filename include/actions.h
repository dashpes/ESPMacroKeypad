#pragma once
#include "config.h"
#include "hid.h"

enum class ActionType : uint8_t { None, Key, Media, Text, NextLayer, Ambient, Snippet, SnippetMenu };

struct Action {
  ActionType type;
  const char* label;   // big label on the e-paper (max 5 characters shown)
  uint8_t mods;
  uint8_t key;
  Media media;
  const char* text;
};

// Helpers so the keymap reads cleanly
constexpr Action NONE()                                   { return {ActionType::None, "", 0, 0, Media::Mute, nullptr}; }
constexpr Action KEY(const char* l, uint8_t m, char k)    { return {ActionType::Key, l, m, (uint8_t)k, Media::Mute, nullptr}; }
constexpr Action MEDIA(const char* l, Media md)           { return {ActionType::Media, l, 0, 0, md, nullptr}; }
constexpr Action TEXT(const char* l, const char* t)       { return {ActionType::Text, l, 0, 0, Media::Mute, t}; }
constexpr Action NEXT_LAYER()                             { return {ActionType::NextLayer, "", 0, 0, Media::Mute, nullptr}; }
constexpr Action SNIPPET(const char* l)                   { return {ActionType::Snippet, l, 0, 0, Media::Mute, nullptr}; }  // types the chosen snippet
constexpr Action SNIPPET_MENU()                           { return {ActionType::SnippetMenu, "Snips", 0, 0, Media::Mute, nullptr}; }
constexpr Action AMBIENT()                                { return {ActionType::Ambient, "Wave", 0, 0, Media::Mute, nullptr}; }

struct Layer {
  const char* name;       // header, e.g. ">MEDIA"
  const char* knobShort;  // header, e.g. "KNOB:VOL"
  const char* knobLong;   // knob popup title, e.g. "VOLUME"
  Action keys[NUM_KEYS];
  Action encCW, encCCW, encPress;
  Action holds[NUM_KEYS]; // long-press actions; leave out / NONE() for none
};

// Text snippets for the SIG key: hold it to pick one with the knob.
struct Snippet {
  const char* name;   // shown in the menu (up to 18 characters)
  const char* text;   // what gets typed ('\n' = Enter)
};
extern const Snippet SNIPPETS[];
extern const uint8_t NUM_SNIPPETS;
void actionsBegin();                 // loads the last-picked snippet
void pickSnippet(uint8_t index);     // from the menu: remember it and type it
uint8_t currentSnippet();

extern const Layer LAYERS[];
extern const uint8_t NUM_LAYERS;

void runAction(const Action& a);
uint8_t currentLayer();
bool hasHold(const Action& a);

// Text for the e-paper (uppercase-able, already truncated to fit)
void actionMainLabel(const Action& a, uint8_t layer, char* out, size_t n);   // <= 5 chars
void actionSubLabel(const Action& a, char* out, size_t n);                   // <= 9 chars
