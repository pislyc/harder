// Standalone controls: computer-keyboard and MIDI-controller mappings onto the panel's buttons and knobs.
// Saved in Documents/HARDER/control.json. Pure data and logic (main thread); the UI executes the actions and
// the audio thread plays the pads (Core::padMap, built from the note bindings to PAD actions).
#pragma once
#include "Json.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace hd {

// ---------- actions ----------
enum ActKind : int16_t {
  AC_NONE = 0,
  AC_PLAYSTOP, AC_PLAY, AC_STOP, AC_REC, AC_FILL, AC_FUNC, AC_ALL, AC_PERF, AC_UNDO,
  AC_STEP,        // i = 0..15 (on the visible step page)
  AC_PAD,         // i = 0..7: play the track (velocity), FUNC: mute
  AC_TRACK,       // i = 0..8: like clicking the track key (select + play, FUNC: mute)
  AC_TRACKPREV, AC_TRACKNEXT, AC_MUTE, AC_SOLO,
  AC_PAT,         // i = 0..7
  AC_PAGE,        // i = 0..6 (TRIG .. LFO; on the FX track the four master pages)
  AC_PAGEPREV, AC_PAGENEXT,
  AC_SPAGE,       // i = 0..3: steps 1-16 .. 49-64
  AC_COPY, AC_PASTE, AC_CLEAR, AC_CLEARLOCKS, AC_RELEASE, AC_LAB,
  // continuous
  AC_ENC,         // i = 0..7: the panel's encoder i (follows track, page and held steps)
  AC_BPM, AC_SWING, AC_OUT, AC_TVOL,
  AC_PARAM,       // tr = 0..8 (8 = FX track), i = parameter index: always that parameter
};
struct Action {
  int16_t k = AC_NONE; int8_t tr = 0; int16_t i = 0;
  bool knob() const { return k >= AC_ENC; }
  bool operator==(const Action& o) const { return k == o.k && tr == o.tr && i == o.i; }
  bool operator<(const Action& o) const { return k != o.k ? k < o.k : tr != o.tr ? tr < o.tr : i < o.i; }
};

struct ActInfo { int16_t k; const char* id; const char* label; int count; };   // count > 1: indexed (id:N, label N)
static const ActInfo ACTS[] = {
  { AC_PLAYSTOP, "playstop", "PLAY / STOP", 1 }, { AC_PLAY, "play", "PLAY", 1 }, { AC_STOP, "stop", "STOP", 1 },
  { AC_REC, "rec", "REC", 1 }, { AC_FILL, "fill", "FILL (HOLD)", 1 }, { AC_FUNC, "func", "FUNC (HOLD)", 1 },
  { AC_ALL, "all", "ALL (HOLD)", 1 }, { AC_PERF, "perf", "PERF", 1 }, { AC_UNDO, "undo", "UNDO", 1 },
  { AC_STEP, "step", "STEP", 16 }, { AC_PAD, "pad", "PAD", 8 }, { AC_TRACK, "track", "TRACK", 9 },
  { AC_TRACKPREV, "trackprev", "TRACK \xe2\x86\x91", 1 }, { AC_TRACKNEXT, "tracknext", "TRACK \xe2\x86\x93", 1 },
  { AC_MUTE, "mute", "MUTE TRACK", 1 }, { AC_SOLO, "solo", "SOLO TRACK", 1 },
  { AC_PAT, "pat", "PATTERN", 8 }, { AC_PAGE, "page", "PAGE", 7 }, { AC_PAGEPREV, "pageprev", "PAGE \xe2\x86\x90", 1 }, { AC_PAGENEXT, "pagenext", "PAGE \xe2\x86\x92", 1 },
  { AC_SPAGE, "spage", "STEPS", 4 }, { AC_COPY, "copy", "COPY", 1 }, { AC_PASTE, "paste", "PASTE", 1 }, { AC_CLEAR, "clear", "CLEAR", 1 },
  { AC_CLEARLOCKS, "clearlocks", "CLEAR LOCKS", 1 }, { AC_RELEASE, "release", "RELEASE STEPS", 1 }, { AC_LAB, "lab", "LAB DRAWER", 1 },
  { AC_ENC, "enc", "ENCODER", 8 }, { AC_BPM, "bpm", "BPM", 1 }, { AC_SWING, "swing", "SWING", 1 }, { AC_OUT, "out", "OUT LEVEL", 1 }, { AC_TVOL, "tvol", "TRACK VOLUME", 1 },
};
static const char* const PAGE_LABELS[7] = { "TRIG", "STEP", "SRC", "FLTR", "AMP", "FX", "LFO" };

inline const ActInfo* actInfo(int16_t k) { for (const ActInfo& a : ACTS) if (a.k == k) return &a; return nullptr; }
inline std::string actId(const Action& a) {
  if (a.k == AC_PARAM) { char b[32]; std::snprintf(b, sizeof b, "param:%d:%d", a.tr + 1, a.i); return b; }
  const ActInfo* inf = actInfo(a.k); if (!inf) return "";
  return inf->count > 1 ? std::string(inf->id) + ":" + std::to_string(a.i + 1) : std::string(inf->id);
}
inline bool parseAct(const std::string& s, Action& a) {
  a = Action();
  if (!s.compare(0, 6, "param:")) {
    int t = 0, i = 0; if (std::sscanf(s.c_str() + 6, "%d:%d", &t, &i) != 2 || t < 1 || t > 9 || i < 0 || i > 55) return false;
    a.k = AC_PARAM; a.tr = (int8_t)(t - 1); a.i = (int16_t)i; return true;
  }
  const size_t c = s.find(':'); const std::string id = s.substr(0, c);
  for (const ActInfo& inf : ACTS) {
    if (id != inf.id) continue;
    if (inf.count > 1) { if (c == std::string::npos) return false; const int n = std::atoi(s.c_str() + c + 1); if (n < 1 || n > inf.count) return false; a.i = (int16_t)(n - 1); }
    else if (c != std::string::npos) return false;
    a.k = inf.k; return true;
  }
  return false;
}
// labels: paramName(tr, i) gives the name for fixed parameters (machine dependent, so the UI supplies it)
inline std::string actLabel(const Action& a) {
  if (a.k == AC_TRACK) return a.i == 8 ? "TRACK FX" : "TRACK " + std::to_string(a.i + 1);
  if (a.k == AC_PAGE) return std::string("PAGE ") + PAGE_LABELS[a.i % 7];
  if (a.k == AC_SPAGE) return "STEPS " + std::to_string(a.i * 16 + 1) + "\xe2\x80\x93" + std::to_string(a.i * 16 + 16);
  if (a.k == AC_PAT) return "PATTERN P" + std::to_string(a.i + 1);
  const ActInfo* inf = actInfo(a.k); if (!inf) return "?";
  return inf->count > 1 ? std::string(inf->label) + " " + std::to_string(a.i + 1) : std::string(inf->label);
}
inline Action act(int16_t k, int i = 0, int tr = 0) { Action a; a.k = k; a.i = (int16_t)i; a.tr = (int8_t)tr; return a; }

// groups shown in LAB > CONTROL
struct ActGroup { const char* name; std::vector<Action> acts; };
inline const std::vector<ActGroup>& actGroups() {
  static std::vector<ActGroup> G;
  if (G.empty()) {
    G.push_back({ "TRANSPORT", { act(AC_PLAYSTOP), act(AC_PLAY), act(AC_STOP), act(AC_REC), act(AC_FILL), act(AC_FUNC), act(AC_ALL), act(AC_PERF), act(AC_UNDO) } });
    { ActGroup g{ "STEPS", {} }; for (int i = 0; i < 16; i++) g.acts.push_back(act(AC_STEP, i)); G.push_back(g); }
    { ActGroup g{ "PADS", {} }; for (int i = 0; i < 8; i++) g.acts.push_back(act(AC_PAD, i)); G.push_back(g); }
    { ActGroup g{ "TRACKS", {} }; for (int i = 0; i < 9; i++) g.acts.push_back(act(AC_TRACK, i)); for (int k : { AC_TRACKPREV, AC_TRACKNEXT, AC_MUTE, AC_SOLO }) g.acts.push_back(act((int16_t)k)); G.push_back(g); }
    { ActGroup g{ "PATTERNS", {} }; for (int i = 0; i < 8; i++) g.acts.push_back(act(AC_PAT, i)); G.push_back(g); }
    { ActGroup g{ "PAGES", {} }; for (int i = 0; i < 7; i++) g.acts.push_back(act(AC_PAGE, i)); g.acts.push_back(act(AC_PAGEPREV)); g.acts.push_back(act(AC_PAGENEXT)); for (int i = 0; i < 4; i++) g.acts.push_back(act(AC_SPAGE, i)); G.push_back(g); }
    G.push_back({ "EDIT", { act(AC_COPY), act(AC_PASTE), act(AC_CLEAR), act(AC_CLEARLOCKS), act(AC_RELEASE), act(AC_LAB) } });
    { ActGroup g{ "KNOBS", {} }; for (int i = 0; i < 8; i++) g.acts.push_back(act(AC_ENC, i)); for (int k : { AC_BPM, AC_SWING, AC_OUT, AC_TVOL }) g.acts.push_back(act((int16_t)k)); G.push_back(g); }
  }
  return G;
}

// ---------- keys ----------
// A key is its physical position (the set-1 scan code, which is also the Linux evdev code for these keys), or for
// keys without a fixed position meaning (arrows, F-keys, keypad, Home/End...) the DGL key code (>= 0xE000).
constexpr int KEY_LEFT = 0xE035, KEY_UP = 0xE036, KEY_RIGHT = 0xE037, KEY_DOWN = 0xE038;
inline bool keyAssignable(int key) { return key > 0 && !(key >= 0xE051 && key <= 0xE058) && key != 0x2A && key != 0x36 && key != 0x1D && key != 0x38; }   // not Shift / Ctrl / Alt / Super
// US-layout names by scan code (Linux and the fallback; Windows asks the active layout, see keyNameForScancode)
inline const char* usKeyName(int sc) {
  static const char* const N[0x59] = { "", "ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "BKSP", "TAB",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "ENTER", "CTRL", "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "`",
    "SHIFT", "\\", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "SHIFT", "KP*", "ALT", "SPACE", "CAPS",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "NUM", "SCROLL", "KP7", "KP8", "KP9", "KP-", "KP4", "KP5", "KP6", "KP+",
    "KP1", "KP2", "KP3", "KP0", "KP.", "", "", "<>", "F11", "F12" };
  return sc >= 0 && sc < 0x59 && N[sc][0] ? N[sc] : "";
}
inline std::string specialKeyName(int key) {
  switch (key) {
    case KEY_LEFT: return "\xe2\x86\x90"; case KEY_RIGHT: return "\xe2\x86\x92"; case KEY_UP: return "\xe2\x86\x91"; case KEY_DOWN: return "\xe2\x86\x93";
    case 0xE031: return "PGUP"; case 0xE032: return "PGDN"; case 0xE033: return "END"; case 0xE034: return "HOME";
    case 0xE041: return "PRINT"; case 0xE042: return "INS"; case 0xE043: return "PAUSE"; case 0xE044: return "MENU";
  }
  if (key >= 0xE000 && key <= 0xE00B) return "F" + std::to_string(key - 0xE000 + 1);
  if (key >= 0xE060 && key <= 0xE069) return "KP" + std::to_string(key - 0xE060);
  char b[16]; std::snprintf(b, sizeof b, "K%X", key); return b;
}

// ---------- MIDI ----------
enum MidiType : int8_t { MT_NOTE = 0, MT_CC = 1 };
enum CcMode : int8_t { CM_ABS = 0, CM_REL2C, CM_RELSIGN, CM_REL64, CM_COUNT };
static const char* const CC_MODE_NAMES[CM_COUNT] = { "ABS", "REL 2C", "REL SIGN", "REL 64" };
static const char* const CC_MODE_IDS[CM_COUNT] = { "abs", "rel2c", "relsign", "rel64" };
// relative encoders: the value as a step count (0 = none)
inline int relDelta(int mode, int v) {
  switch (mode) {
    case CM_REL2C: return v == 0 ? 0 : v < 64 ? v : v - 128;          // 1..63 up, 127 = -1 .. 64 = -64
    case CM_RELSIGN: return v == 0 || v == 64 ? 0 : v < 64 ? v : -(v - 64);   // 1..63 up, 65 = -1 ..
    case CM_REL64: return v - 64;                                      // 65 = +1, 63 = -1
  }
  return 0;
}
struct MidiBind { int8_t type = MT_CC; int8_t ch = -1; uint8_t num = 0; Action a; int8_t mode = CM_ABS; };   // ch -1 = any channel
inline int midiSrcId(int type, int ch, int num) { return 0x1000000 | type << 16 | (ch & 15) << 8 | (num & 127); }   // held-key source ids
inline std::string noteName(int n) { static const char* const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return std::string(N[n % 12]) + std::to_string(n / 12 - 2); }   // 36 = C1, as in the MIDI export
inline std::string midiSrcLabel(const MidiBind& b) {
  std::string s = b.type == MT_NOTE ? "NOTE " + noteName(b.num) : "CC " + std::to_string(b.num);
  return s + (b.ch < 0 ? " \xc2\xb7 ANY" : " \xc2\xb7 CH " + std::to_string(b.ch + 1));
}

struct ControlMap {
  std::map<Action, int> keys;          // action -> key (one key per action, one action per key)
  std::vector<MidiBind> midi;
  void defaults() { defaultKeys(); defaultMidi(); }
  void defaultKeys() {
    keys.clear();
    keys[act(AC_PLAYSTOP)] = 0x39;
    for (int i = 0; i < 8; i++) { keys[act(AC_STEP, i)] = 0x02 + i; keys[act(AC_STEP, 8 + i)] = 0x10 + i; keys[act(AC_PAD, i)] = 0x2C + i; }
    keys[act(AC_ALL)] = 0x1E; keys[act(AC_FILL)] = 0x21; keys[act(AC_LAB)] = 0x26; keys[act(AC_RELEASE)] = 0x01;
    keys[act(AC_PAGEPREV)] = KEY_LEFT; keys[act(AC_PAGENEXT)] = KEY_RIGHT; keys[act(AC_TRACKPREV)] = KEY_UP; keys[act(AC_TRACKNEXT)] = KEY_DOWN;
  }
  void defaultMidi() {   // the fixed mapping of 1.2 and the plugin: C1-G1 play tracks 1-8, C0-G0 cue patterns 1-8
    midi.clear();
    for (int i = 0; i < 8; i++) { MidiBind b; b.type = MT_NOTE; b.ch = -1; b.num = (uint8_t)(36 + i); b.a = act(AC_PAD, i); midi.push_back(b); }
    for (int i = 0; i < 8; i++) { MidiBind b; b.type = MT_NOTE; b.ch = -1; b.num = (uint8_t)(24 + i); b.a = act(AC_PAT, i); midi.push_back(b); }
  }
  Action actionForKey(int key) const { for (auto& kv : keys) if (kv.second == key) return kv.first; return Action(); }
  int keyFor(const Action& a) const { auto it = keys.find(a); return it == keys.end() ? 0 : it->second; }
  // assign; returns the action that lost the key (AC_NONE if none)
  Action setKey(const Action& a, int key) {
    Action lost; for (auto it = keys.begin(); it != keys.end(); ++it) if (it->second == key && !(it->first == a)) { lost = it->first; keys.erase(it); break; }
    if (key) keys[a] = key; else keys.erase(a); return lost;
  }
  // exact channel first, then "any channel"
  const MidiBind* find(int type, int ch, int num) const {
    for (const MidiBind& b : midi) if (b.type == type && b.ch == ch && b.num == num) return &b;
    for (const MidiBind& b : midi) if (b.type == type && b.ch < 0 && b.num == num) return &b;
    return nullptr;
  }
  // a new binding replaces any binding of the same source (and an any-channel binding of that number)
  void bind(const MidiBind& nb) {
    midi.erase(std::remove_if(midi.begin(), midi.end(), [&](const MidiBind& b) { return b.type == nb.type && b.num == nb.num && (b.ch == nb.ch || b.ch < 0 || nb.ch < 0); }), midi.end());
    midi.push_back(nb);
  }
  std::vector<int> midiFor(const Action& a) const { std::vector<int> r; for (size_t i = 0; i < midi.size(); i++) if (midi[i].a == a) r.push_back((int)i); return r; }

  JVal toJson() const {
    JVal o = JVal::Obj(); o.set("version", JVal::Num(1));
    JVal k = JVal::Obj(); for (auto& kv : keys) k.set(actId(kv.first), JVal::Num(kv.second)); o.set("keys", std::move(k));
    JVal m = JVal::Arr();
    for (const MidiBind& b : midi) {
      JVal e = JVal::Obj(); e.set("t", JVal::Str(b.type == MT_NOTE ? "note" : "cc")); e.set("ch", JVal::Num(b.ch < 0 ? 0 : b.ch + 1)); e.set("n", JVal::Num(b.num));
      e.set("a", JVal::Str(actId(b.a))); if (b.type == MT_CC && b.a.knob()) e.set("m", JVal::Str(CC_MODE_IDS[b.mode])); m.push(std::move(e));
    }
    o.set("midi", std::move(m)); return o;
  }
  // unknown entries are skipped; a missing section keeps its defaults
  void fromJson(const JVal& o) {
    defaults(); if (!o.isObj()) return;
    if (o["keys"].isObj()) {
      keys.clear();
      for (auto& kv : o["keys"].o) { Action a; const int key = (int)kv.second.num(0); if (parseAct(kv.first, a) && keyAssignable(key) && !a.knob()) setKey(a, key); }
    }
    if (o["midi"].isArr()) {
      midi.clear();
      for (size_t i = 0; i < o["midi"].size(); i++) {
        const JVal& e = o["midi"][i]; MidiBind b; const std::string t = e["t"].str();
        if (t != "note" && t != "cc") continue; b.type = t == "note" ? MT_NOTE : MT_CC;
        const int ch = (int)e["ch"].num(0), n = (int)e["n"].num(-1); if (ch < 0 || ch > 16 || n < 0 || n > 127) continue;
        b.ch = (int8_t)(ch - 1); b.num = (uint8_t)n; if (!parseAct(e["a"].str(), b.a)) continue;
        if (b.a.knob() && b.type != MT_CC) continue;
        b.mode = CM_ABS; for (int m = 0; m < CM_COUNT; m++) if (e["m"].str() == CC_MODE_IDS[m]) b.mode = (int8_t)m;
        midi.push_back(b);
      }
    }
  }
};

} // namespace hd
