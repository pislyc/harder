// Offline export, ported from the web app (ui.js: zip, wav24, midiFile, renderLoop, exportLoop, exportKit).
// Same files, names, sample values and MIDI events as the web export; used by the plugin and the standalone.
#pragma once
#include "Model.hpp"
#include <atomic>
#include <functional>

namespace hd {

struct ZipEntry { std::string name; std::vector<uint8_t> data; };

inline uint32_t crc32(const uint8_t* d, size_t n) {
  static uint32_t T[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; } init = true; }
  uint32_t c = 0xFFFFFFFFu; for (size_t i = 0; i < n; i++) c = T[(c ^ d[i]) & 255] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
// stored (no compression), fixed DOS date 0x5a21 like the web zip()
inline std::vector<uint8_t> zipStore(const std::vector<ZipEntry>& files) {
  std::vector<uint8_t> out, central;
  auto u16 = [](std::vector<uint8_t>& v, uint32_t x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); };
  auto u32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 255); };
  uint32_t off = 0;
  for (const ZipEntry& f : files) {
    const uint32_t crc = crc32(f.data.data(), f.data.size()), sz = (uint32_t)f.data.size(), nl = (uint32_t)f.name.size();
    u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0x5a21);
    u32(out, crc); u32(out, sz); u32(out, sz); u16(out, nl); u16(out, 0);
    out.insert(out.end(), f.name.begin(), f.name.end()); out.insert(out.end(), f.data.begin(), f.data.end());
    u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0x5a21);
    u32(central, crc); u32(central, sz); u32(central, sz); u16(central, nl); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0);
    u32(central, 0); u32(central, off); central.insert(central.end(), f.name.begin(), f.name.end());
    off += 30 + nl + sz;
  }
  const uint32_t csize = (uint32_t)central.size(); out.insert(out.end(), central.begin(), central.end());
  u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (uint32_t)files.size()); u16(out, (uint32_t)files.size()); u32(out, csize); u32(out, off); u16(out, 0);
  return out;
}
inline std::vector<uint8_t> wav24(const std::vector<const float*>& chs, size_t n, int sr) {
  const uint32_t nc = (uint32_t)chs.size(), bytes = (uint32_t)(n * nc * 3);
  std::vector<uint8_t> u(44 + (size_t)bytes);
  auto ws = [&](size_t o, const char* s) { for (size_t i = 0; s[i]; i++) u[o + i] = (uint8_t)s[i]; };
  auto w32 = [&](size_t o, uint32_t x) { for (int i = 0; i < 4; i++) u[o + i] = (x >> (8 * i)) & 255; };
  auto w16 = [&](size_t o, uint32_t x) { u[o] = x & 255; u[o + 1] = (x >> 8) & 255; };
  ws(0, "RIFF"); w32(4, 36 + bytes); ws(8, "WAVE"); ws(12, "fmt "); w32(16, 16); w16(20, 1); w16(22, nc);
  w32(24, (uint32_t)sr); w32(28, (uint32_t)sr * nc * 3); w16(32, nc * 3); w16(34, 24); ws(36, "data"); w32(40, bytes);
  size_t o = 44;
  for (size_t i = 0; i < n; i++) for (uint32_t c = 0; c < nc; c++) {
    double x = chs[c][i]; x = x < -1 ? -1 : x > 1 ? 1 : x;
    int32_t v = (int32_t)jsround(x * 8388607); if (v < 0) v += 16777216;
    u[o++] = v & 255; u[o++] = (v >> 8) & 255; u[o++] = (v >> 16) & 255;
  }
  return u;
}
struct MidiEv { int tr; long tick; double vel; };
inline std::vector<uint8_t> midiFile(const std::vector<MidiEv>& events, double bpm, long loopTicks) {
  struct E { long t; uint8_t d[3]; int ord; };
  std::vector<E> ev; int ord = 0;
  for (const MidiEv& e : events) {
    double v = e.vel; v = v < 1 ? 1 : v > 127 ? 127 : v;
    ev.push_back({ e.tick, { 0x99, (uint8_t)(36 + e.tr), (uint8_t)jsround(v) }, ord++ });
    ev.push_back({ e.tick + 12, { 0x89, (uint8_t)(36 + e.tr), 0 }, ord++ });
  }
  // web: by tick, note-offs first; the order of simultaneous note-ons is left to the sort there (any order means
  // the same to a DAW); here it is the order they were played
  std::stable_sort(ev.begin(), ev.end(), [](const E& a, const E& b) { if (a.t != b.t) return a.t < b.t; const bool ao = a.d[0] == 0x89, bo = b.d[0] == 0x89; return ao && !bo; });
  auto vlq = [](std::vector<uint8_t>& o, unsigned long n) { uint8_t b[5]; int k = 0; b[k++] = n & 127; while ((n >>= 7)) b[k++] = (uint8_t)((n & 127) | 128); while (k) o.push_back(b[--k]); };
  const long tempo = (long)jsround(60000000.0 / bpm);
  std::vector<uint8_t> trk = { 0, 0xFF, 0x51, 3, (uint8_t)((tempo >> 16) & 255), (uint8_t)((tempo >> 8) & 255), (uint8_t)(tempo & 255), 0, 0xFF, 0x58, 4, 4, 2, 24, 8 };
  long last = 0; for (const E& e : ev) { vlq(trk, (unsigned long)std::max(0L, e.t - last)); trk.insert(trk.end(), e.d, e.d + 3); last = std::max(last, e.t); }
  vlq(trk, (unsigned long)std::max(0L, loopTicks - last)); trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0);
  const uint32_t L = (uint32_t)trk.size();
  std::vector<uint8_t> out = { 0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 0x4D, 0x54, 0x72, 0x6B, (uint8_t)(L >> 24), (uint8_t)(L >> 16), (uint8_t)(L >> 8), (uint8_t)L };
  out.insert(out.end(), trk.begin(), trk.end()); return out;
}

inline void loadEngine(Engine& E, const Project& S, const double* master) {
  for (int t = 0; t < NT; t++) E.loadTrack(t, S.tracks[t].m, S.tracks[t].p);
  E.loadMaster(master);
  for (int i = 0; i < NPAT; i++) E.setPattern(i, S.patterns[i]);
  E.loadDone(S.cur, S.bpm, S.swing, S.mutes);
}

struct LoopRender { std::vector<float> L, R; long loopTicks = 0; };
// web renderLoop(sr, loops, solo, events). Returns false if cancelled.
inline bool renderLoop(const Project& S, double sr, int loops, int solo, std::vector<MidiEv>* events, LoopRender& out, const std::atomic<bool>* cancel = nullptr) {
  double master[NM]; std::memcpy(master, S.master, sizeof master);
  if (solo >= 0) { master[16] = 0; master[17] = 0; }
  std::unique_ptr<Engine> e(new Engine()); e->init(sr); loadEngine(*e, S, master); e->solo = solo;
  const Pattern& P = S.patterns[S.cur];
  const double stepLen = sr * 60 / S.bpm / 4; const long loopN = (long)jsround(P.mlen * loops * stepLen), tail = (long)jsround(sr * 4);
  struct Ctx { std::vector<MidiEv>* ev; long loopN; double stepLen; } ctx{ events, loopN, stepLen };
  if (events) {
    e->onTrigCtx = &ctx;
    e->onTrig = [](void* c, int tr, double t, double vel) { Ctx& x = *(Ctx*)c; if (t < x.loopN) x.ev->push_back({ tr, std::max(0L, (long)jsround(t / x.stepLen * 24)), vel }); };
  }
  e->play(0);
  std::vector<float> L((size_t)(loopN + tail)), R((size_t)(loopN + tail)); long pos = 0; bool crossed = false;
  while (pos < (long)L.size()) {
    long n = std::min(512L, (long)L.size() - pos);
    if (!crossed && pos + n > loopN) n = loopN - pos;
    if (n > 0) { e->process(L.data() + pos, R.data() + pos, (int)n); pos += n; }
    if (!crossed && pos >= loopN) { crossed = true; e->stopAt((double)loopN); }
    if (cancel && cancel->load()) return false;
  }
  out.L.assign(L.begin(), L.begin() + loopN); out.R.assign(R.begin(), R.begin() + loopN);
  for (long i = loopN; i < (long)L.size(); i++) { const long j = (i - loopN) % loopN; out.L[j] = (float)((double)out.L[j] + L[i]); out.R[j] = (float)((double)out.R[j] + R[i]); }
  double pk = 0; for (long i = 0; i < loopN; i++) pk = std::max(pk, (double)std::max(std::fabs(out.L[i]), std::fabs(out.R[i])));
  if (pk > 0.995) { const double g = 0.995 / pk; for (long i = 0; i < loopN; i++) { out.L[i] = (float)(out.L[i] * g); out.R[i] = (float)(out.R[i] * g); } }
  out.loopTicks = (long)jsround(P.mlen * loops * 24);
  return true;
}

inline std::string safeName(const std::string& s) {   // web: s.replace(/[^A-Za-z0-9]+/g, '_')
  std::string o; bool run = false;
  for (char c : s) { const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); if (ok) { o += c; run = false; } else if (!run) { o += '_'; run = true; } }
  return o;
}
inline std::string soundName(const TrackM& t) { return t.pre.size() ? t.pre : std::string(MACH_NAMES[t.m]); }
inline std::string loopBaseName(int cur, double bpm) { return "HARDER_P" + std::to_string(cur + 1) + "_" + std::to_string((long)jsround(bpm)) + "bpm"; }
inline std::string loopBaseName(const Project& S) { return loopBaseName(S.cur, S.bpm); }
inline std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }

typedef std::function<void(const std::string&)> ExportStatus;
// web exportLoop(): mix, optional stems, MIDI, project
inline bool exportLoopZip(const Project& S, int loops, bool stems, std::vector<uint8_t>& zip, const ExportStatus& status, const std::atomic<bool>* cancel = nullptr) {
  const double sr = 48000; std::vector<ZipEntry> files; std::vector<MidiEv> events; const std::string base = loopBaseName(S);
  if (status) status("RENDERING MIX");
  LoopRender mix; if (!renderLoop(S, sr, loops, -1, &events, mix, cancel)) return false;
  files.push_back({ base + ".wav", wav24({ mix.L.data(), mix.R.data() }, mix.L.size(), (int)sr) });
  if (stems) {
    const Pattern& P = S.patterns[S.cur];
    for (int t = 0; t < NT; t++) {
      bool any = false; for (int s = 0; s < NSTEP; s++) if (P.steps[t][s].on) any = true;
      if (S.mutes[t] || !any) continue;
      if (status) status("RENDERING STEM " + std::to_string(t + 1) + " OF 8");
      LoopRender st; if (!renderLoop(S, sr, loops, t, nullptr, st, cancel)) return false;
      files.push_back({ "stems/" + std::to_string(t + 1) + "_" + safeName(soundName(S.tracks[t])) + ".wav", wav24({ st.L.data(), st.R.data() }, st.L.size(), (int)sr) });
    }
  }
  files.push_back({ base + ".mid", midiFile(events, S.bpm, mix.loopTicks) });
  const std::string js = projectToString(S); files.push_back({ "harder-project.json", std::vector<uint8_t>(js.begin(), js.end()) });
  if (status) status("PACKING");
  zip = zipStore(files); return true;
}
// web renderOneShot(track, null, sr, 6, 127) + exportKit() trimming
inline std::vector<float> kitOneShot(const TrackM& tk, double bpm, double sr) {
  std::unique_ptr<Engine> e(new Engine()); e->init(sr); e->loadTrack(0, tk.m, tk.p); e->msgBpm(bpm);
  e->trigTrack(0, nullptr, 127);
  const long N = (long)jsround(sr * 6); std::vector<float> L((size_t)N), R((size_t)N), st((size_t)N);
  e->stems[0] = st.data(); e->process(L.data(), R.data(), (int)N); e->stems[0] = nullptr;
  double pk = 0; long last = 0; for (long i = 0; i < N; i++) { const double a = std::fabs(st[i]); if (a > pk) pk = a; if (a > 0.0003) last = i; }
  const long n = std::min(N, last + (long)jsround(sr * 0.01)); const double g = pk > 0.89 ? 0.89 / pk : 1; const long fade = (long)jsround(sr * 0.003);
  std::vector<float> out(st.begin(), st.begin() + n);
  for (long i = 0; i < n; i++) out[i] = (float)(out[i] * (g * (i > n - fade ? (double)(n - i) / fade : 1)));
  return out;
}
inline bool exportKitZip(const Project& S, std::vector<uint8_t>& zip, const ExportStatus& status, const std::atomic<bool>* cancel = nullptr) {
  const double sr = 48000; std::vector<ZipEntry> files;
  for (int t = 0; t < NT; t++) {
    if (status) status("RENDERING ONE-SHOT " + std::to_string(t + 1) + " OF 8");
    const std::vector<float> o = kitOneShot(S.tracks[t], S.bpm, sr);
    files.push_back({ std::string(t + 1 < 10 ? "0" : "") + std::to_string(t + 1) + "_" + safeName(soundName(S.tracks[t])) + ".wav", wav24({ o.data() }, o.size(), (int)sr) });
    if (cancel && cancel->load()) return false;
  }
  JVal pj = projectToJson(S), kj = JVal::Obj(); kj.set("tracks", pj["tracks"]); kj.set("roles", pj["roles"]); kj.set("master", pj["master"]);
  std::string js; writeJson(js, kj); files.push_back({ "harder-kit.json", std::vector<uint8_t>(js.begin(), js.end()) });
  zip = zipStore(files); return true;
}

} // namespace hd
