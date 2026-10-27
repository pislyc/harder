// Keyboard / MIDI mapping (Control.hpp): defaults equal to the fixed 1.2 keys and notes, action ids, JSON round trip,
// relative encoder decoding, binding replacement and lookup order.
#include "../plugins/Harder/Control.hpp"
#include <cstdio>
using namespace hd;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// the 1.2 keyboard handler, as a table: scan code / DGL key -> what it did
static Action oldKey(int k) {
  if (k >= 0x02 && k <= 0x09) return act(AC_STEP, k - 0x02);
  if (k >= 0x10 && k <= 0x17) return act(AC_STEP, 8 + k - 0x10);
  if (k >= 0x2C && k <= 0x33) return act(AC_PAD, k - 0x2C);
  switch (k) { case 0x39: return act(AC_PLAYSTOP); case 0x21: return act(AC_FILL); case 0x26: return act(AC_LAB); case 0x1E: return act(AC_ALL); case 0x01: return act(AC_RELEASE);
    case KEY_LEFT: return act(AC_PAGEPREV); case KEY_RIGHT: return act(AC_PAGENEXT); case KEY_UP: return act(AC_TRACKPREV); case KEY_DOWN: return act(AC_TRACKNEXT); }
  return Action();
}

int main() {
  ControlMap m; m.defaults();
  int same = 0;
  for (int k = 1; k < 0xE100; k++) { const Action a = m.actionForKey(k), o = oldKey(k); CHECK(a == o, "key %X: %s vs %s", k, actId(a).c_str(), actId(o).c_str()); if (o.k != AC_NONE && a == o) same++; }
  CHECK(same == 33, "%d default keys", same);   // 16 steps, 8 pads, 9 others
  for (int n = 0; n < 128; n++) for (int ch = 0; ch < 16; ch++) {
    const MidiBind* b = m.find(MT_NOTE, ch, n);
    if (n >= 36 && n < 44) CHECK(b && b->a == act(AC_PAD, n - 36), "note %d ch %d", n, ch);
    else if (n >= 24 && n < 32) CHECK(b && b->a == act(AC_PAT, n - 24), "note %d ch %d", n, ch);
    else CHECK(!b, "note %d bound", n);
    CHECK(!m.find(MT_CC, ch, n), "cc bound by default");
  }
  CHECK(noteName(36) == "C1" && noteName(24) == "C0" && noteName(60) == "C3", "note names");
  // every action id parses back to itself
  int n = 0;
  for (const auto& g : actGroups()) for (const Action& a : g.acts) { Action b; CHECK(parseAct(actId(a), b) && b == a, "id %s", actId(a).c_str()); n++; }
  { Action b; CHECK(parseAct("param:9:16", b) && b.k == AC_PARAM && b.tr == 8 && b.i == 16 && actId(b) == "param:9:16", "param id"); }
  for (const char* bad : { "", "step", "step:0", "step:17", "pad:9", "track:10", "param:0:1", "param:10:1", "param:3:56", "playstop:1", "nonsense" }) { Action b; CHECK(!parseAct(bad, b), "accepted '%s'", bad); }
  CHECK(n == 9 + 16 + 8 + 13 + 8 + 13 + 6 + 12, "%d actions in groups", n);
  // relative encoders
  CHECK(relDelta(CM_REL2C, 1) == 1 && relDelta(CM_REL2C, 127) == -1 && relDelta(CM_REL2C, 65) == -63 && relDelta(CM_REL2C, 0) == 0, "2c");
  CHECK(relDelta(CM_RELSIGN, 1) == 1 && relDelta(CM_RELSIGN, 65) == -1 && relDelta(CM_RELSIGN, 127) == -63 && relDelta(CM_RELSIGN, 64) == 0, "sign");
  CHECK(relDelta(CM_REL64, 65) == 1 && relDelta(CM_REL64, 63) == -1 && relDelta(CM_REL64, 64) == 0, "64");
  // bindings: same source replaces, exact channel before any channel
  { ControlMap c; c.defaults();
    MidiBind b; b.type = MT_NOTE; b.ch = 9; b.num = 36; b.a = act(AC_PLAY); c.bind(b);
    CHECK(c.find(MT_NOTE, 9, 36) && c.find(MT_NOTE, 9, 36)->a == act(AC_PLAY), "exact binding");
    CHECK(!c.find(MT_NOTE, 0, 36), "the any-channel binding of that note is replaced");
    MidiBind k; k.type = MT_CC; k.ch = 0; k.num = 21; k.a = act(AC_ENC, 2); k.mode = CM_REL64; c.bind(k);
    MidiBind k2 = k; k2.a = act(AC_PARAM, 8, 3); c.bind(k2);
    int cnt = 0; for (auto& x : c.midi) if (x.type == MT_CC && x.num == 21) cnt++; CHECK(cnt == 1 && c.find(MT_CC, 0, 21)->a == act(AC_PARAM, 8, 3), "rebinding a CC");
    // keys: one key per action and per action one key
    const Action lost = c.setKey(act(AC_UNDO), 0x39); CHECK(lost == act(AC_PLAYSTOP) && c.actionForKey(0x39) == act(AC_UNDO) && c.keyFor(act(AC_PLAYSTOP)) == 0, "key steal");
    c.setKey(act(AC_UNDO), 0x2F); CHECK(c.keyFor(act(AC_UNDO)) == 0x2F && c.actionForKey(0x39).k == AC_NONE, "key move");
    // JSON
    std::string js; writeJson(js, c.toJson()); JVal j; CHECK(parseJson(js, j), "json");
    ControlMap r; r.fromJson(j); std::string js2; writeJson(js2, r.toJson());
    CHECK(js == js2 && r.keys == c.keys && r.midi.size() == c.midi.size(), "json round trip");
    const MidiBind* rk = r.find(MT_CC, 0, 21); CHECK(rk && rk->mode == CM_REL64 && rk->a == act(AC_PARAM, 8, 3), "mode kept");
  }
  { ControlMap r; JVal j; parseJson("{\"keys\":{\"step:3\":30,\"enc:1\":31,\"bogus\":5,\"play\":42},\"midi\":[{\"t\":\"note\",\"ch\":0,\"n\":60,\"a\":\"enc:1\"},{\"t\":\"cc\",\"ch\":17,\"n\":1,\"a\":\"play\"},{\"t\":\"cc\",\"ch\":2,\"n\":7,\"a\":\"out\",\"m\":\"rel2c\"}]}", j);
    r.fromJson(j);
    CHECK(r.keyFor(act(AC_STEP, 2)) == 30 && r.keys.size() == 1, "junk keys skipped (knob, unknown, Shift)");
    CHECK(r.midi.size() == 1 && r.midi[0].a == act(AC_OUT) && r.midi[0].ch == 1 && r.midi[0].mode == CM_REL2C, "junk MIDI skipped");
    ControlMap e; JVal z; parseJson("{}", z); e.fromJson(z); CHECK(e.keys.size() == 33 && e.midi.size() == 16, "empty file = defaults"); }
  std::printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
  return fails ? 1 : 0;
}
