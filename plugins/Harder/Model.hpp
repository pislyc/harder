// HARDER project model (main thread): the same data the web app keeps in its `S` object, the groove generator,
// presets and kits. Ported from ui.js; the generator is checked against the JS code by tools/gen_test.
#pragma once
#include "Convert.hpp"
#include "Data.hpp"
#include <string>
#include <memory>

namespace hd {

struct Mulberry {   // ui.js mulberry()
  uint32_t a;
  explicit Mulberry(uint32_t seed) : a(seed) {}
  double operator()() {
    a += 0x6D2B79F5u;
    uint32_t t = (uint32_t)imul((int32_t)(a ^ (a >> 15)), (int32_t)(1 | a));
    t = (t + (uint32_t)imul((int32_t)(t ^ (t >> 7)), (int32_t)(61 | t))) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
  }
};
template <class R, class T, size_t N> inline T pick(R& r, const T (&a)[N]) { return a[(size_t)std::floor(r() * N)]; }
template <class R, class T> inline T pickv(R& r, const std::vector<T>& a) { return a[(size_t)std::floor(r() * a.size())]; }

struct TrackM { int m = M_SUB; double p[NP]; std::string pre; };

struct Project {
  int kit = 6;
  TrackM tracks[NT];
  int roles[NT] = { 0, 1, 2, 3, 4, 5, 6, 7 };
  double master[NM];
  Pattern patterns[NPAT];
  int cur = 0; double bpm = 125, swing = 50; uint8_t mutes[NT] = { 0 }; int perfA = 8, perfB = 24;
  SampleRef smp[NSLOT];   // SAMPLE machine slots (immutable data, shared with the engine, undo and the library)
  // sequencing beyond one pattern: seq 0 pattern, 1 chain, 2 song
  int seq = ARR_OFF; std::vector<int8_t> chain; Arrangement song;
  uint8_t outs[NT] = { 0 };   // plugin multi-out: 1 = the track plays on its own output pair (not in the main mix)
  Project() { reset(); }
  void reset() {   // in place (a Project is 2 MB: never create one on the stack)
    kit = 6; for (int i = 0; i < NT; i++) { tracks[i].m = M_SUB; defaultTrackParams(M_SUB, tracks[i].p); tracks[i].pre.clear(); roles[i] = i; mutes[i] = 0; }
    defaultMaster(master); for (auto& P : patterns) P.reset(); cur = 0; bpm = 125; swing = 50; perfA = 8; perfB = 24;
    for (auto& x : smp) x.reset();
    seq = ARR_OFF; chain.clear(); song = Arrangement(); song.mode = ARR_SONG; for (auto& o : outs) o = 0;
  }
  // what the engine plays: the chain or the song, or nothing (pattern mode)
  Arrangement arrangement() const {
    Arrangement a;
    if (seq == ARR_CHAIN && !chain.empty()) { a.mode = ARR_CHAIN; a.loop = true; a.n = std::min((int)chain.size(), NROW); for (int i = 0; i < a.n; i++) { a.rows[i].pat = chain[i]; a.rows[i].reps = 1; } }
    else if (seq == ARR_SONG && song.n > 0) { a = song; a.mode = ARR_SONG; }
    return a;
  }
  bool anySample() const { for (auto& x : smp) if (x) return true; return false; }
};

struct GenOpts { double den = 0.55, vari = 0.35; bool locks = true, fills = true, half = false; };

inline int CI(const char* name) { for (int i = 0; i < NCOND; i++) if (!std::strcmp(COND[i], name)) return i; return 0; }
inline int roleIndex(const char* r) { for (int i = 0; i < NROLES; i++) if (!std::strcmp(ROLES[i], r)) return i; return -1; }
inline int presetIndex(const std::string& n) { for (int i = 0; i < NPRESETS; i++) if (n == PRESETS[i].n) return i; return -1; }
inline int styleIndex(const char* n) { for (int i = 0; i < NSTYLES; i++) if (!std::strcmp(STYLES[i].name, n)) return i; return -1; }

inline void emptyPattern(Pattern& P, double bpm, double swing) {
  P.clearSteps(); P.mlen = 32; for (int t = 0; t < NTR; t++) { P.len[t] = 32; P.scl[t] = 4; }
  P.bpm = bpm ? bpm : 125; P.swing = swing ? swing : 50;
}
inline void presetParams(int pi, double* p) {
  const PresetDef& pr = PRESETS[pi]; defaultTrackParams(pr.m, p);
  for (int i = 0; i < 8; i++) p[i] = pr.s[i];
  for (int k = 0; k < pr.no; k++) p[pr.o[k].i] = pr.o[k].v;
}
inline void kitTracks(int k, Project& S) {
  const KitDef& K = KITS[k];
  for (int t = 0; t < NT; t++) {
    const KitTrack& kt = K.t[t]; const PresetDef& pr = PRESETS[kt.preset];
    S.tracks[t].m = pr.m; presetParams(kt.preset, S.tracks[t].p);
    for (int j = 0; j < kt.no; j++) S.tracks[t].p[kt.o[j].i] = kt.o[j].v;
    S.tracks[t].pre = pr.n;
  }
}
inline void kitMaster(int k, double* m, const double* keep) {
  const double keepVol = keep ? keep[19] : 0;
  defaultMaster(m); const KitDef& K = KITS[k];
  for (int j = 0; j < K.nm; j++) m[K.master[j].i] = K.master[j].v;
  if (keep) m[19] = keepVol;
}

// ---------- groove generation (ui.js) ----------
static const char LETTER[NROLES] = { 'K', 'S', 'C', 'H', 'O', 'P', 'T', 'X' };
static const double SCALE[] = { 0, 3, 5, 7, 10, 12, -5, -2, 15 };
inline int letterIndex(char L) { const char* s = "KSCHOPTX"; for (int i = 0; i < 8; i++) if (s[i] == L) return i; return -1; }
inline std::string stripBars(const char* s) { std::string o; for (const char* p = s; *p; p++) if (*p != '|') o += *p; return o; }

// returns false for '-' / unknown
inline bool symStep(char ch, Step& s) {
  s = Step(); s.on = 1;
  switch (ch) {
    case 'x': s.l.set(41, 122); return true; case 'o': return true; case 'g': s.l.set(41, 52); return true; case 'p': s.l.set(42, 50); return true;
    case 'c': s.l.set(43, CI("1:2")); return true; case 'd': s.l.set(43, CI("2:2")); return true;
    case 'f': s.l.set(43, CI("FILL")); s.l.set(41, 105); return true;
    case 'r': s.l.set(45, 3); s.l.set(47, 1); s.l.set(46, -30); return true;
    case 'R': s.l.set(45, 7); s.l.set(47, 0); s.l.set(46, -20); return true;
  }
  s.on = 0; return false;
}
template <class R> inline void soundLocks(R& r, Step& s, int tr, int role, const Project& S) {
  const int m = S.tracks[tr].m; const double* base = S.tracks[tr].p;
  const bool hat = role == 3 || role == 4, pt = role == 5 || role == 6;
  if (hat && m == M_MTL && r() < 0.2) s.l.set(1, clampd(jsround(base[1] + 12 + r() * 20), 0, 127));
  if (pt && r() < 0.5) s.l.set(40, pick(r, SCALE));
  if (m == M_GLT && role != 1 && role != 2 && r() < 0.35) s.l.set(2, jsround(r() * 127));
  if (m == M_MDL && r() < 0.2) s.l.set(1, clampd(jsround(base[1] + (r() * 2 - 1) * 26), 0, 127));
}
template <class R> inline void genRow(R& r, int style, const SeedDef& seed, int tr, const GenOpts& o, const Project& S, Step* row, int& len, int& scl) {
  const StyleDef& st = STYLES[style]; const int role = S.roles[tr]; const char L = LETTER[role]; const int li = letterIndex(L);
  const std::string line = stripBars(seed.line[li]); const std::string oline = role == 3 ? stripBars(seed.line[4]) : std::string();
  for (int i = 0; i < NSTEP; i++) row[i] = Step();
  len = line.empty() ? 32 : (int)line.size(); scl = st.scl[li] ? st.scl[li] : 4;
  const bool hps = role == 3 || role == 5 || role == 1;
  for (int i = 0; i < (int)line.size() && i < NSTEP; i++) {
    const char ch = line[i];
    if (!oline.empty() && i < (int)oline.size() && oline[i] != '-') continue;
    if (ch == 'f' && !o.fills) continue;
    Step s; const bool sym = symStep(ch, s);
    if (!sym) {
      if (o.den > 0.55 && hps && r() < (o.den - 0.55) * 0.5 * (o.vari + 0.3)) { s = Step(); s.on = 1; s.l.set(41, 40 + std::floor(r() * 25)); }
      else continue;
    } else {
      if ((ch == 'g' || ch == 'p') && r() > 0.25 + o.den * 1.1) continue;
      if (ch == 'x' && role != 0 && r() < o.vari * 0.05) continue;
      if (ch == 'o' && role != 0 && r() < o.vari * 0.18) { static const char* const C4[] = { "1:2", "2:2", "3:4", "!1ST" }; s.l.set(43, CI(pick(r, C4))); }
      if (role == 3 && !(s.l.has(45) && s.l.v[45] != 0) && r() < o.vari * 0.06) { s.l.set(45, 3); s.l.set(47, 0); s.l.set(46, -20); }
      if (st.micro[li] != 0 && r() < 0.7) s.l.set(44, jsround((r() * 2 - 1) * st.micro[li]));
    }
    if (o.locks) soundLocks(r, s, tr, role, S);
    row[i] = s;
  }
  if (o.fills && o.vari > 0.3 && (role == 1 || role == 2 || role == 5)) {
    for (int k = 0; k < 2; k++) {
      const int i = len - 1 - (int)std::floor(r() * 4);
      if (i >= 0 && i < NSTEP && !row[i].on) { Step s; s.on = 1; s.l.set(43, CI("FILL")); s.l.set(41, jsround(80 + r() * 40)); row[i] = s; }
    }
  }
}
template <class R> inline void generatePattern(R& r, int style, const GenOpts& o, const double* keepTempo, const Project& S, Pattern& P) {
  const StyleDef& st = STYLES[style]; const SeedDef& seed = st.seeds[(int)std::floor(r() * st.nseeds)];
  emptyPattern(P, keepTempo ? keepTempo[0] : st.bpm, keepTempo ? keepTempo[1] : st.swing);
  for (int tr = 0; tr < NT; tr++) genRow(r, style, seed, tr, o, S, P.steps[tr], P.len[tr], P.scl[tr]);
  if (o.half) { for (int tr = 0; tr < NT; tr++) if (S.roles[tr] != 3 && S.roles[tr] != 4 && P.scl[tr] == 4) P.scl[tr] = 2; P.mlen = 64; }
}
template <class R> inline void polyPattern(R& r, const GenOpts& o, double bpm, double swing, const Project& S, Pattern& P) {
  emptyPattern(P, bpm, swing); P.mlen = 64;
  static const int KL[] = { 16, 16, 12 }; static const int OL[] = { 16, 12, 14, 10, 7, 9, 11, 15, 13 };
  for (int tr = 0; tr < NT; tr++) {
    const int role = S.roles[tr]; const int len = role == 0 ? pick(r, KL) : pick(r, OL);
    const int k = std::max(1, (int)jsround(len * (0.12 + r() * 0.42) * (0.5 + o.den))); const int rot = (int)std::floor(r() * len);
    P.len[tr] = len;
    for (int i = 0; i < len; i++) {
      if ((((i + rot) * k) % len) >= k) continue;
      Step s; s.on = 1; const double v = jsround(60 + r() * 67); if (v != 100) s.l.set(41, v);
      if (r() < o.vari * 0.4) {
        if (r() < 0.5) { static const double PR[] = { 40, 55, 70, 85 }; s.l.set(42, pick(r, PR)); }
        else { static const char* const CS[] = { "1:2", "2:2", "1:3", "3:4", "!PRE", "PRE" }; s.l.set(43, CI(pick(r, CS))); }
      }
      if (r() < o.vari * 0.25) { static const double RT[] = { 3, 4, 6, 7 }; static const double RF[] = { -40, 0, 30 }; const double a = pick(r, RT); const double b = pick(r, RF); s.l.set(45, a); s.l.set(46, b); }
      if (r() < o.vari * 0.3) s.l.set(44, jsround((r() * 2 - 1) * 10));
      if (o.locks) { soundLocks(r, s, tr, role, S); if (role != 0 && r() < 0.3) s.l.set(40, pick(r, SCALE)); if (r() < 0.2) s.l.set(27, jsround(r() * 60)); }
      P.steps[tr][i] = s;
    }
  }
}
inline void euclid(int k, int n, int rot, bool* out) { for (int i = 0; i < n; i++) out[i] = (((i + rot) * k) % n) < k; }
template <class R> inline void mutatePattern(Pattern& P, R& r, double amt, const Project& S) {
  static const char* const C4[] = { "1:2", "2:2", "3:4", "!1ST" };
  for (int tr = 0; tr < NT; tr++) {
    const int len = P.len[tr]; Step* st = P.steps[tr]; std::vector<int> on;
    for (int i = 0; i < len; i++) if (st[i].on && !st[i].tl) on.push_back(i);
    if (on.empty() || r() > 0.35 + amt * 0.6) continue;
    const int role = S.roles[tr]; const double act = r();
    if (act < 0.35) {
      std::vector<int> c; for (int i : on) if (!(role == 0 && i % 4 == 0)) c.push_back(i);
      if (c.empty()) continue;
      const int i = pickv(r, c), j = (i + (r() < 0.5 ? -1 : 1) + len) % len;
      if (!st[j].on) { st[j] = st[i]; st[i] = Step(); }
    } else if (act < 0.6) {
      const int j = (int)std::floor(r() * len); if (!st[j].on) { Step s; s.on = 1; s.l.set(41, jsround(40 + r() * 30)); st[j] = s; }
    } else if (act < 0.8) {
      const int i = pickv(r, on); if (role == 0 && i % 4 == 0) continue; st[i].l.set(43, CI(pick(r, C4)));
    } else {
      const int i = pickv(r, on);
      if (role == 5 || role == 6) st[i].l.set(40, pick(r, SCALE)); else st[i].l.set(41, jsround(60 + r() * 67));
    }
  }
}
inline void showcase(Pattern& P) {
  for (int s = 0; s < NSTEP; s++) P.steps[5][s] = Step();
  const int SC[8][3] = { { 3, 0, 1 }, { 6, 7, 0 }, { 10, 3, 1 }, { 14, -5, 0 }, { 19, 0, 0 }, { 22, 10, 1 }, { 26, -2, 0 }, { 30, 12, 0 } };
  for (auto& e : SC) { Step s; s.on = 1; s.l.set(40, e[1]); if (e[2]) s.l.set(48, 1); P.steps[5][e[0]] = s; }
  for (int s = 0; s < NSTEP; s++) P.steps[6][s] = Step();
  { Step s; s.on = 1; s.l.set(40, 0); s.l.set(48, 1); P.steps[6][0] = s; }
  { Step s; s.on = 1; s.tl = 1; s.l.set(40, 12); P.steps[6][3] = s; }
  { Step s; s.on = 1; s.l.set(40, 7); s.l.set(48, 1); P.steps[6][16] = s; }
  { Step s; s.on = 1; s.tl = 1; s.l.set(40, -5); P.steps[6][19] = s; }
  for (int i : { 14, 30 }) { Step s; s.on = 1; s.l.set(1, 64); s.l.set(49, CI("1:2")); P.steps[3][i] = s; }
  { Step s; s.on = 1; P.steps[8][0] = s; }
  { Step s; s.on = 1; s.l.set(18, -20); P.steps[8][24] = s; }
  { Step s; s.on = 1; s.l.set(18, -20); s.l.set(1, 112); s.l.set(5, 127); P.steps[8][28] = s; }
}
inline void defaultProject(Project& p) {
  p.reset();
  p.kit = 6; kitTracks(6, p); for (int i = 0; i < NT; i++) p.roles[i] = i; kitMaster(6, p.master, nullptr);
  p.cur = 0; p.bpm = 132; p.swing = 52; for (auto& m : p.mutes) m = 0; p.perfA = 8; p.perfB = 24;
  GenOpts o; o.den = 0.55; o.vari = 0.35; o.locks = true; o.fills = true; o.half = false;
  const struct { uint32_t seed; const char* style; } D[8] = { { 11, "TECHNO" }, { 3, "ELECTRO" }, { 8, "JUNGLE" }, { 21, "UKG" }, { 5, "FOOTWORK" }, { 9, "TRAP" }, { 14, "STEPPERS" }, { 2, "HOUSE" } };
  for (int i = 0; i < NPAT; i++) { Mulberry r(D[i].seed); generatePattern(r, styleIndex(D[i].style), o, nullptr, p, p.patterns[i]); }
  showcase(p.patterns[0]);
  p.bpm = p.patterns[0].bpm; p.swing = p.patterns[0].swing;
}

// ---------- samples in project files ----------
// "samples": [{ "slot", "id", "name", "rate", "len", "fmt": "s16"|"s24"|"f32", "data": base64 little-endian mono }]
// Only written when a slot is used, so projects without samples stay byte-identical to the web format.
// Without "data" (undo snapshots) a sample is looked up by id in the session cache.
inline JVal sampleToJson(const SampleData& s, bool withData) {
  JVal o = JVal::Obj(); o.set("id", JVal::Str(s.id)); o.set("name", JVal::Str(s.name)); o.set("rate", JVal::Num(s.rate));
  o.set("len", JVal::Num(s.len)); o.set("fmt", JVal::Str(sampleFmtName(s.fmt)));
  if (s.ch == 2) o.set("ch", JVal::Num(2));
  if (!s.slices.empty()) { JVal a = JVal::Arr(); for (int x : s.slices) a.push(JVal::Num(x)); o.set("slices", std::move(a)); }
  if (withData) o.set("data", JVal::Str(s.base64()));
  return o;
}
inline SampleRef sampleFromJson(const JVal& j, SampleCache* cache, std::string* err = nullptr) {
  if (!j.isObj()) return nullptr;
  const std::string id = j["id"].str();
  if (j["data"].isStr()) {
    std::vector<uint8_t> b; if (!b64decode(j["data"].str(), b)) { if (err) *err = "bad sample data"; return nullptr; }
    std::vector<int> sl; for (size_t k = 0; k < j["slices"].size() && k < 64; k++) sl.push_back(jsonInt(j["slices"][k].num(-1), -1, 0x7fffffff, -1));
    SampleRef s = sampleFromBytes(j["name"].str(), j["rate"].num(0), sampleFmtFromName(j["fmt"].str()), b, err, j["ch"].num(1) == 2 ? 2 : 1, &sl);
    return cache ? cache->add(s) : s;
  }
  return cache && !id.empty() ? cache->find(id) : nullptr;
}

// ---------- JSON (web harder-project.json, v:2) ----------
inline JVal projectToJson(const Project& S, bool withData = true) {
  JVal o = JVal::Obj();
  o.set("v", JVal::Num(2)); o.set("kit", JVal::Num(S.kit));
  JVal tr = JVal::Arr();
  for (int t = 0; t < NT; t++) {
    JVal x = JVal::Obj(); x.set("m", JVal::Str(MACH_IDS[S.tracks[t].m]));
    JVal p = JVal::Arr(); for (int i = 0; i < NP; i++) p.push(JVal::Num(S.tracks[t].p[i])); x.set("p", std::move(p));
    x.set("pre", JVal::Str(S.tracks[t].pre)); tr.push(std::move(x));
  }
  o.set("tracks", std::move(tr));
  JVal roles = JVal::Arr(); for (int t = 0; t < NT; t++) roles.push(JVal::Str(ROLES[S.roles[t]])); o.set("roles", std::move(roles));
  JVal m = JVal::Arr(); for (int i = 0; i < NM; i++) m.push(JVal::Num(S.master[i])); o.set("master", std::move(m));
  JVal ps = JVal::Arr(); for (int i = 0; i < NPAT; i++) ps.push(patternToJson(S.patterns[i])); o.set("patterns", std::move(ps));
  o.set("cur", JVal::Num(S.cur)); o.set("bpm", JVal::Num(S.bpm)); o.set("swing", JVal::Num(S.swing));
  JVal mu = JVal::Arr(); for (int t = 0; t < NT; t++) mu.push(JVal::Num(S.mutes[t])); o.set("mutes", std::move(mu));
  JVal pf = JVal::Obj(); pf.set("a", JVal::Num(S.perfA)); pf.set("b", JVal::Num(S.perfB)); o.set("perf", std::move(pf));
  // keys the web app does not know are written only when used (a project without them stays web-identical)
  if (S.seq == ARR_CHAIN) o.set("seq", JVal::Str("chain")); else if (S.seq == ARR_SONG) o.set("seq", JVal::Str("song"));
  if (!S.chain.empty()) { JVal c = JVal::Arr(); for (int8_t x : S.chain) c.push(JVal::Num(x)); o.set("chain", std::move(c)); }
  if (S.song.n > 0) {
    JVal so = JVal::Obj(), rows = JVal::Arr();
    for (int r = 0; r < S.song.n; r++) {
      const ArrRow& w = S.song.rows[r]; JVal x = JVal::Obj(); x.set("p", JVal::Num(w.pat)); x.set("n", JVal::Num(w.reps));
      if (w.mute) { JVal m = JVal::Arr(); for (int t = 0; t < NT; t++) if ((w.mute >> t) & 1) m.push(JVal::Num(t + 1)); x.set("mute", std::move(m)); }
      if (w.bpm > 0) x.set("bpm", JVal::Num(w.bpm));
      rows.push(std::move(x));
    }
    so.set("rows", std::move(rows)); so.set("loop", JVal::Bool(S.song.loop)); if (S.song.start) so.set("start", JVal::Num(S.song.start)); o.set("song", std::move(so));
  }
  { bool any = false; for (uint8_t x : S.outs) if (x) any = true;
    if (any) { JVal a = JVal::Arr(); for (int t = 0; t < NT; t++) a.push(JVal::Num(S.outs[t] ? 1 : 0)); o.set("outs", std::move(a)); } }
  if (S.anySample()) {
    JVal sa = JVal::Arr();
    for (int i = 0; i < NSLOT; i++) if (S.smp[i]) { JVal e = sampleToJson(*S.smp[i], withData); e.o.insert(e.o.begin(), { "slot", JVal::Num(i) }); sa.push(std::move(e)); }
    o.set("samples", std::move(sa));
  }
  return o;
}
// validProject() from ui.js
inline bool projectFromJson(const JVal& o, Project& p, SampleCache* cache = nullptr) {
  if (!o.isObj() || o["v"].num(0) != 2 || !o["tracks"].isArr() || o["tracks"].size() != NT || !o["patterns"].isArr() || o["patterns"].size() != NPAT) return false;
  for (int t = 0; t < NT; t++) if (machineIndex(o["tracks"][t]["m"].str().c_str()) < 0) return false;
  std::unique_ptr<Project> q(new Project());
  q->kit = clampi(o["kit"].num(0), 0, 1000000, 0);
  for (int t = 0; t < NT; t++) {
    const JVal& x = o["tracks"][t]; const int mid = machineIndex(x["m"].str().c_str());
    q->tracks[t].m = mid; paramsFromJson(x["p"], mid, q->tracks[t].p); q->tracks[t].pre = x["pre"].isStr() ? x["pre"].str() : "";
  }
  for (int t = 0; t < NT; t++) { q->roles[t] = t; const int r = roleIndex(o["roles"][t].str().c_str()); if (o["roles"].isArr() && r >= 0) q->roles[t] = r; }
  defaultMaster(q->master); for (int i = 0; i < NM; i++) { const JVal& v = o["master"][i]; if (v.isNum() && std::isfinite(v.n)) q->master[i] = jsonParam(v.n); }
  for (int i = 0; i < NPAT; i++) patternFromJson(o["patterns"][i], q->patterns[i]);
  q->cur = clampi(o["cur"].num(0), 0, NPAT - 1, 0);
  for (int t = 0; t < NT; t++) q->mutes[t] = o["mutes"][t].truthy() ? 1 : 0;
  const JVal& pf = o["perf"]; if (pf["a"].isNum() && pf["b"].isNum()) { q->perfA = clampi(pf["a"].n, 0, 47, 8); q->perfB = clampi(pf["b"].n, 0, 47, 24); }
  q->bpm = q->patterns[q->cur].bpm; q->swing = q->patterns[q->cur].swing;
  const JVal& sa = o["samples"];
  for (size_t k = 0; k < sa.size(); k++) { const int slot = clampi(sa[k]["slot"].num(-1), -1, NSLOT - 1, -1); if (slot >= 0) q->smp[slot] = sampleFromJson(sa[k], cache); }
  { const std::string sq = o["seq"].str(); q->seq = sq == "chain" ? ARR_CHAIN : sq == "song" ? ARR_SONG : ARR_OFF; }
  for (size_t k = 0; k < o["chain"].size() && k < (size_t)NROW; k++) q->chain.push_back((int8_t)clampi(o["chain"][k].num(0), 0, NPAT - 1, 0));
  { const JVal& so = o["song"]; const JVal& rows = so["rows"];
    for (size_t k = 0; k < rows.size() && k < (size_t)NROW; k++) {
      ArrRow& w = q->song.rows[q->song.n++]; const JVal& x = rows[k];
      w.pat = (int8_t)clampi(x["p"].num(0), 0, NPAT - 1, 0); w.reps = (uint8_t)clampi(x["n"].num(1), 1, 99, 1);
      w.mute = 0; for (size_t m = 0; m < x["mute"].size(); m++) { const int t = clampi(x["mute"][m].num(0), 0, NT, 0); if (t >= 1) w.mute |= (uint8_t)(1 << (t - 1)); }
      const double b = x["bpm"].num(0); w.bpm = std::isfinite(b) && b >= 40 && b <= 300 ? (float)b : 0;
    }
    q->song.loop = !so.has("loop") || so["loop"].truthy(); q->song.start = clampi(so["start"].num(0), 0, std::max(0, q->song.n - 1), 0); q->song.mode = ARR_SONG; }
  for (int t = 0; t < NT; t++) q->outs[t] = o["outs"][t].truthy() ? 1 : 0;
  p = *q; return true;
}
inline std::string projectToString(const Project& S, bool withData = true) { std::string s; writeJson(s, projectToJson(S, withData)); return s; }
inline bool projectFromString(const std::string& s, Project& p, SampleCache* cache = nullptr) { JVal j; if (!parseJson(s, j)) return false; return projectFromJson(j, p, cache); }

} // namespace hd
