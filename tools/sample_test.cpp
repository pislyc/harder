// SAMPLE machine, samples in projects, the user library and sample lifetime across threads.
// usage: sample_test <empty temp dir>        (build with -fsanitize=address,undefined for the thread part)
#include "../plugins/Harder/HarderShared.hpp"
#include "../plugins/Harder/Library.hpp"
#include "../plugins/Harder/Export.hpp"
#include <cstdio>
#include <random>
#include <thread>
#include <atomic>
using namespace hd;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static std::string gDir;
namespace hd { std::string harderConfigDir() { return gDir; } }

// ---------- WAV writer for the decoder tests ----------
static std::vector<uint8_t> makeWav(int fmtTag, int bits, int ch, int rate, const std::vector<std::vector<double>>& chans, bool extensible = false, bool junk = false) {
  const size_t n = chans[0].size(); const int bps = bits / 8, ba = bps * ch;
  std::vector<uint8_t> o; auto u8 = [&](uint32_t x) { o.push_back((uint8_t)x); }; auto u16 = [&](uint32_t x) { u8(x & 255); u8(x >> 8 & 255); };
  auto u32 = [&](uint32_t x) { u16(x & 65535); u16(x >> 16); }; auto tag = [&](const char* t) { for (int i = 0; i < 4; i++) u8((uint8_t)t[i]); };
  tag("RIFF"); u32(0); tag("WAVE");
  if (junk) { tag("LIST"); u32(5); for (int i = 0; i < 5; i++) u8('x'); u8(0); }   // odd size + pad byte
  tag("fmt "); u32(extensible ? 40 : 16); u16(extensible ? 0xFFFE : fmtTag); u16(ch); u32(rate); u32(rate * ba); u16(ba); u16(bits);
  if (extensible) { u16(22); u16(bits); u32(3); u16(fmtTag); u16(0); u32(0x00100000); u32(0xAA000080); u32(0x719B3800); }
  tag("data"); u32((uint32_t)(n * ba));
  for (size_t i = 0; i < n; i++) for (int c = 0; c < ch; c++) {
    const double v = chans[c][i];
    if (fmtTag == 3 && bits == 32) { float f = (float)v; uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    else if (fmtTag == 3) { uint64_t u; std::memcpy(&u, &v, 8); u32((uint32_t)u); u32((uint32_t)(u >> 32)); }
    else if (bits == 8) u8((uint32_t)(int)std::lround(v * 127 + 128));
    else if (bits == 16) u16((uint32_t)(int32_t)std::lround(v * 32767));
    else if (bits == 24) { const uint32_t x = (uint32_t)(int32_t)std::lround(v * 8388607); u8(x & 255); u8(x >> 8 & 255); u8(x >> 16 & 255); }
    else u32((uint32_t)(int32_t)std::lround(v * 2147483647.0));
  }
  const uint32_t riff = (uint32_t)o.size() - 8; for (int i = 0; i < 4; i++) o[4 + i] = (uint8_t)(riff >> (8 * i));
  return o;
}

// render track 0 of an engine (voices only, stem = after the voice's DC blocker)
static std::vector<float> renderStem(Engine& E, int n, double vel = 127) {
  std::vector<float> stem(n), L(n), R(n); E.skipMaster = true;
  E.audition(0, vel, nullptr);
  for (int p = 0; p < n; p += 64) { const int k = std::min(64, n - p); E.stems[0] = stem.data() + p; E.process(L.data() + p, R.data() + p, k); }
  E.stems[0] = nullptr; return stem;
}
static std::vector<double> dcblock(const std::vector<double>& x) { std::vector<double> y(x.size()); double dx = 0, dy = 0; for (size_t i = 0; i < x.size(); i++) { dy = x[i] - dx + 0.9993 * dy; dx = x[i]; y[i] = dy; } return y; }
static double hermite(const std::vector<float>& x, double pos) {
  const int n = (int)x.size(); auto at = [&](int i) { return (i < 0 || i >= n) ? 0.0 : (double)x[i]; };
  const int i = (int)std::floor(pos); const double f = pos - i, y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
  const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2); return ((c3 * f + c2) * f + c1) * f + y1;
}
static double maxDiff(const std::vector<float>& a, const std::vector<double>& b, size_t from, size_t to) { double m = 0; for (size_t i = from; i < to && i < a.size() && i < b.size(); i++) m = std::max(m, std::fabs((double)a[i] - b[i])); return m; }

static SampleRef noiseSample(const char* name, int n, int rate, int fmt, uint32_t seed) {
  std::shared_ptr<SampleData> s(new SampleData()); s->name = name; s->rate = rate; s->fmt = fmt; s->d.resize(n);
  std::mt19937 g(seed); std::uniform_real_distribution<double> U(-0.9, 0.9);
  for (int i = 0; i < n; i++) s->d[i] = (float)(0.5 * std::sin(i * 0.031) + 0.4 * U(g));
  finishSample(*s); return s;
}

int main(int argc, char** argv) {
  gDir = argc > 1 ? argv[1] : "/tmp/hd_sample_test"; makeDir(gDir);
  // ===== A: WAV decoding =====
  {
    const int n = 1000; std::vector<double> l(n), r(n); for (int i = 0; i < n; i++) { l[i] = 0.8 * std::sin(i * 0.05); r[i] = -0.3 + 0.2 * std::cos(i * 0.11); }
    struct C { int tag, bits, ch; bool ext, junk; double tol; int fmt; } cases[] = {
      { 1, 8, 1, false, false, 1.0 / 127, SF_S16 }, { 1, 16, 1, false, true, 1.0 / 32767, SF_S16 }, { 1, 24, 2, true, false, 1.0 / 8388607, SF_S24 },
      { 1, 32, 1, false, false, 1e-7, SF_S24 }, { 3, 32, 2, false, true, 1e-7, SF_F32 }, { 3, 64, 1, true, false, 1e-7, SF_F32 } };
    for (const C& c : cases) {
      std::vector<std::vector<double>> ch{ l }; if (c.ch == 2) ch.push_back(r);
      const std::vector<uint8_t> w = makeWav(c.tag, c.bits, c.ch, 44100, ch, c.ext, c.junk);
      std::string err; SampleRef s = decodeWav("t", w.data(), w.size(), &err);
      CHECK(s, "decode %d/%d/%d: %s", c.tag, c.bits, c.ch, err.c_str()); if (!s) continue;
      CHECK(s->len == n && s->rate == 44100 && s->fmt == c.fmt, "decode %d/%d header len %d rate %g fmt %d", c.tag, c.bits, s->len, s->rate, s->fmt);
      double md = 0; for (int i = 0; i < n; i++) md = std::max(md, std::fabs(s->d[i] - (c.ch == 2 ? (l[i] + r[i]) / 2 : l[i])));
      CHECK(md <= c.tol * 1.5 + 2.0 / 8388608, "decode %d/%d/%d max err %g", c.tag, c.bits, c.ch, md);
      // the stored form reloads bit-exactly
      SampleRef s2 = sampleFromBytes("t", s->rate, s->fmt, s->bytes());
      CHECK(s2 && s2->id == s->id && s2->d == s->d, "stored form round trip %d/%d", c.tag, c.bits);
      const std::vector<uint8_t> e = encodeWav(*s); SampleRef s3 = decodeWav("t", e.data(), e.size());
      CHECK(s3 && s3->d == s->d && s3->id == s->id, "encodeWav round trip %d/%d", c.tag, c.bits);
    }
    std::string err; const uint8_t bad[] = "RIFF\0\0\0\0WAVEjunk";
    CHECK(!decodeWav("x", bad, sizeof bad, &err), "garbage accepted");
    std::vector<uint8_t> mp3 = makeWav(1, 16, 1, 48000, { l }); mp3[20] = 0x55;   // format tag 0x55 = MP3
    CHECK(!decodeWav("x", mp3.data(), mp3.size(), &err), "compressed WAV accepted");
    std::vector<std::vector<double>> big{ std::vector<double>((size_t)(61 * 8000), 0.1) }; const std::vector<uint8_t> w = makeWav(1, 16, 1, 8000, big);
    bool cut = false; SampleRef s = decodeWav("long", w.data(), w.size(), &err, &cut);
    CHECK(s && cut && s->len == 60 * 8000, "60 s limit");
  }
  // ===== B-F: playback =====
  const double sr = 48000; const int N = 24000;
  SampleRef smp = noiseSample("noise", N, 48000, SF_S24, 7);
  std::unique_ptr<Engine> E(new Engine());
  auto setup = [&](const SampleRef& s, std::initializer_list<std::pair<int, double>> kv) {
    E->init(sr); E->setSample(5, s.get()); double p[NP]; defaultTrackParams(M_SMP, p); p[2] = 5;
    for (auto& x : kv) p[x.first] = x.second; E->loadTrack(0, M_SMP, p);
  };
  std::vector<double> src(smp->d.begin(), smp->d.end());
  { setup(smp, {}); const std::vector<float> y = renderStem(*E, N + 2000);
    std::vector<double> ex(N + 2000, 0.0); for (int i = 0; i < N; i++) ex[i] = src[i]; ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, N + 2000) < 1e-6, "unity playback differs %g", maxDiff(y, ex, 0, N + 2000)); }
  { setup(smp, { { 0, 12 } }); const std::vector<float> y = renderStem(*E, N);
    std::vector<double> ex(N, 0.0); for (int i = 0; i < N / 2; i++) ex[i] = src[2 * i]; ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, N) < 1e-6, "+12 st differs %g", maxDiff(y, ex, 0, N)); }
  { setup(smp, { { 0, -12 } }); const std::vector<float> y = renderStem(*E, 2 * N + 100);
    std::vector<double> ex(2 * N + 100, 0.0); for (int i = 0; i * 0.5 < N; i++) ex[i] = hermite(smp->d, i * 0.5); ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, 2 * N + 100) < 1e-6, "-12 st differs %g", maxDiff(y, ex, 0, 2 * N + 100)); }
  { SampleRef s44 = noiseSample("n44", 44100, 44100, SF_S16, 9); setup(s44, {}); const std::vector<float> y = renderStem(*E, 50000);
    const double inc = 44100.0 / 48000;
    std::vector<double> ex(50000, 0.0); for (int i = 0; i * inc < 44100; i++) ex[i] = hermite(s44->d, i * inc); ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, 50000) < 1e-6, "rate conversion differs %g", maxDiff(y, ex, 0, 50000)); }
  { setup(smp, { { 5, 1 } }); const std::vector<float> y = renderStem(*E, N + 100);
    std::vector<double> ex(N + 100, 0.0); for (int i = 0; i < N; i++) ex[i] = src[N - 1 - i]; ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, N + 100) < 1e-6, "reverse differs %g", maxDiff(y, ex, 0, N + 100)); }
  { setup(smp, { { 5, 2 }, { 6, 64 } }); const int M = 3 * N; const std::vector<float> y = renderStem(*E, M);
    const double lp = 64.0 / 127 * N; const double L = N - lp; std::vector<double> ex(M);
    for (int i = 0; i < M; i++) { double pos = i; if (pos >= N) pos = lp + std::fmod(pos - N, L); ex[i] = hermite(smp->d, pos); } ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, M) < 1e-5, "loop differs %g", maxDiff(y, ex, 0, M)); }
  { setup(smp, { { 3, 32 }, { 4, 96 } }); const std::vector<float> y = renderStem(*E, N);
    const int a = (int)std::floor(32.0 / 127 * (N - 1)), b = (int)std::floor(96.0 / 127 * N); std::vector<double> ex(N, 0.0);
    for (int k = 0; a + k < b; k++) { double g = 1; if (k < 32) g *= (k + 1.0) / 33; const double left = b - (a + k); if (left < 64) g *= left / 64; ex[k] = src[a + k] * g; } ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, N) < 1e-6, "start/end with fades differs %g", maxDiff(y, ex, 0, N)); }
  { setup(smp, { { 7, 50 } }); const std::vector<float> y = renderStem(*E, 2000); std::vector<double> ex(2000); for (int i = 0; i < 2000; i++) ex[i] = src[i] * 0.25; ex = dcblock(ex);
    CHECK(maxDiff(y, ex, 0, 2000) < 1e-6, "level 50 = -12 dB differs"); }
  { E->init(sr); double p[NP]; defaultTrackParams(M_SMP, p); p[2] = 9; E->loadTrack(0, M_SMP, p); const std::vector<float> y = renderStem(*E, 6000);
    bool silent = true; for (float v : y) if (v != 0) silent = false; CHECK(silent && !E->voices[0].active, "empty slot is not silent / voice stays active"); }
  { setup(smp, {}); const std::vector<float> y = renderStem(*E, N + 20000); CHECK(!E->voices[0].active, "voice still active after the sample ended"); }

  // ===== G: projects =====
  {
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    const std::string plain = projectToString(*P);
    CHECK(plain.find("\"samples\"") == std::string::npos, "project without samples has a samples key");
    P->smp[0] = noiseSample("a 16", 3000, 44100, SF_S16, 1); P->smp[7] = noiseSample("b 24", 2000, 48000, SF_S24, 2); P->smp[63] = noiseSample("c f32", 1000, 96000, SF_F32, 3);
    P->tracks[2].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[2].p); P->tracks[2].p[2] = 7;
    const std::string full = projectToString(*P), lean = projectToString(*P, false);
    std::unique_ptr<Project> Q(new Project());
    CHECK(projectFromString(full, *Q), "project with samples rejected");
    for (int i : { 0, 7, 63 }) CHECK(Q->smp[i] && Q->smp[i]->id == P->smp[i]->id && Q->smp[i]->d == P->smp[i]->d && Q->smp[i]->name == P->smp[i]->name && Q->smp[i]->rate == P->smp[i]->rate, "slot %d not restored", i);
    CHECK(projectToString(*Q) == full, "save/load/save not identical");
    int used = 0; for (auto& x : Q->smp) if (x) used++; CHECK(used == 3, "extra slots: %d", used);
    SampleCache cache; for (auto& x : P->smp) cache.add(x);
    std::unique_ptr<Project> U(new Project());
    CHECK(projectFromString(lean, *U, &cache) && U->smp[7].get() == P->smp[7].get(), "undo snapshot does not resolve from the cache");
    CHECK(projectFromString(lean, *U) && !U->smp[7], "lean snapshot without cache filled a slot");
    CHECK(lean.find("\"data\"") == std::string::npos && full.size() > lean.size() + 8000, "lean %zu full %zu", lean.size(), full.size());
    // export renders the sample track
    std::vector<uint8_t> z; auto st = [](const std::string&) {}; std::atomic<bool> cancel{ false };
    CHECK(exportKitZip(*Q, z, st, &cancel) && z.size() > 1000, "kit export with a sample failed");
  }
  // ===== I: library =====
  {
    CHECK(ensureLibrary(), "library folders");
    CHECK(pathExists(joinPath(libRoot(), "README.txt")), "README missing");
    CHECK(safeFileName("a/b:c*?\"<>|d") == "abcd" && safeFileName(" . ") == "untitled" && safeFileName("CON") == "CON_" && safeFileName("底鼓 Kick") == "底鼓 Kick", "safeFileName");
    std::string err; double p[NP]; defaultTrackParams(M_SMP, p); p[0] = 3; p[2] = 11; p[18] = 90;
    const std::string kick = saveSound(joinPath(libSoundsDir(), "Kicks"), "底鼓 Deep", M_SMP, p, smp, err);
    CHECK(!kick.empty(), "saveSound: %s", err.c_str());
    double q[NP]; defaultTrackParams(M_SUB, q); q[0] = 77;
    CHECK(!saveSound(joinPath(libSoundsDir(), "Kicks"), "Sub One", M_SUB, q, nullptr, err).empty(), "save SUB");
    CHECK(!saveSound(joinPath(libSoundsDir(), "Kicks"), "Sub One", M_SUB, q, nullptr, err).empty(), "save SUB twice");
    { const std::vector<uint8_t> w = encodeWav(*smp); writeFileAtomic(joinPath(libSoundsDir(), "inbox hit.wav"), w.data(), w.size()); }
    LibIndex L; scanLibrary(libSoundsDir(), true, L);
    CHECK(L.cats.size() == 1 && L.cats[0] == "Kicks", "categories");
    int nk = 0, ni = 0; for (auto& it : L.items) { if (it.cat == "Kicks") nk++; if (it.cat.empty()) ni++; }
    CHECK(nk == 3 && ni == 1, "items kicks %d inbox %d (the sound's WAV must not be listed)", nk, ni);
    const LibItem* deep = nullptr; for (auto& it : L.items) if (it.path == kick) deep = &it;
    CHECK(deep && deep->kind == LK_SOUND, "saved sound not found");
    SampleCache cache; SoundDef d;
    if (deep) { CHECK(loadSoundItem(*deep, d, &cache, err), "load: %s", err.c_str());
      CHECK(d.m == M_SMP && d.smp && d.smp->id == smp->id && d.p[0] == 3 && d.p[18] == 90 && d.name == "底鼓 Deep", "loaded sound differs"); }
    for (auto& it : L.items) if (it.cat.empty()) { SoundDef w; CHECK(loadSoundItem(it, w, &cache, err) && w.m == M_SMP && w.smp->id == smp->id, "WAV item"); }
    // move + rename (the WAV follows, the reference is rewritten)
    if (deep) { const LibItem c = *deep; const std::string np = moveItem(c, joinPath(libSoundsDir(), "Low"), err, "Renamed");
      CHECK(!np.empty() && pathExists(joinPath(joinPath(libSoundsDir(), "Low"), "Renamed.wav")) && !pathExists(kick), "move/rename: %s", err.c_str());
      LibIndex L2; scanLibrary(libSoundsDir(), true, L2); const LibItem* r = nullptr; for (auto& it : L2.items) if (it.path == np) r = &it;
      SoundDef d2; CHECK(r && loadSoundItem(*r, d2, &cache, err) && d2.smp && d2.smp->id == smp->id && d2.name == "Renamed", "moved sound does not load: %s", err.c_str());
      if (r) { const LibItem c2 = *r; CHECK(trashItem(c2, err) && !pathExists(np) && pathExists(joinPath(libTrashDir(), "Renamed.json")) && pathExists(joinPath(libTrashDir(), "Renamed.wav")), "trash"); } }
    CHECK(trashFolder(joinPath(libSoundsDir(), "Kicks"), err) && !pathExists(joinPath(libSoundsDir(), "Kicks")), "trash folder: %s", err.c_str());
    // grooves
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    const std::string g = saveGroove(joinPath(libGroovesDir(), "Mine"), "Groove 1", P->patterns[3], err);
    LibIndex G; scanLibrary(libGroovesDir(), false, G);
    CHECK(G.items.size() == 1 && G.items[0].kind == LK_GROOVE && G.items[0].path == g, "groove listing");
    std::unique_ptr<Pattern> PP(new Pattern()); std::string name;
    CHECK(!G.items.empty() && loadGrooveItem(G.items[0], *PP, name, err) && Core::samePattern(*PP, P->patterns[3]) && name == "Groove 1", "groove round trip");
    // a file written by another program (sound JSON that names its WAV)
    { JVal o = JVal::Obj(); o.set("format", JVal::Str("harder-sound")); o.set("version", JVal::Num(1)); o.set("name", JVal::Str("From Lower")); o.set("machine", JVal::Str("SMP"));
      JVal pa = JVal::Arr(); pa.push(JVal::Num(-5)); o.set("p", pa); JVal s = JVal::Obj(); s.set("file", JVal::Str("lower.wav")); o.set("sample", s);
      std::string out; writeJson(out, o); makeDir(joinPath(libSoundsDir(), "Bass")); writeFileAtomic(joinPath(joinPath(libSoundsDir(), "Bass"), "lower.json"), out);
      const std::vector<uint8_t> w = encodeWav(*smp); writeFileAtomic(joinPath(joinPath(libSoundsDir(), "Bass"), "lower.wav"), w.data(), w.size());
      LibIndex B; scanLibrary(joinPath(libSoundsDir(), ""), true, B); const LibItem* it = nullptr; for (auto& x : B.items) if (x.cat == "Bass") it = &x;
      SoundDef e; CHECK(it && loadSoundItem(*it, e, &cache, err) && e.p[0] == -5 && e.p[3] == 0 && e.p[4] == 127 && e.smp, "external sound file: %s", err.c_str()); }
  }
  // ===== K: 1.3.1 fixes =====
  {
    // export names keep non-ASCII letters; zip entries with such names carry the UTF-8 flag, ASCII ones do not (web-identical)
    CHECK(safeName("底鼓 Kick #1") == "底鼓_Kick_1" && safeName("Snap 808") == "Snap_808", "safeName: %s", safeName("底鼓 Kick #1").c_str());
    std::vector<ZipEntry> zf{ { "01_底鼓.wav", { 1, 2, 3 } }, { "02_Kick.wav", { 4 } } }; const std::vector<uint8_t> z = zipStore(zf);
    CHECK(z.size() > 30 && (z[6] | z[7] << 8) == 0x800, "UTF-8 flag on the local header");
    size_t second = 30 + zf[0].name.size() + 3; CHECK((z[second + 6] | z[second + 7] << 8) == 0, "no flag on an ASCII name");
    // large WAV: only the header and 60 s are read; truncation is reported only when the audio really is longer
    auto writeWav = [&](const std::string& path, int secs, bool tailChunk, uint32_t junkBefore) {
      const int rate = 8000; const uint32_t nb = (uint32_t)(secs * rate * 2); std::string f;
      auto u32 = [&](uint32_t x) { for (int i = 0; i < 4; i++) f += (char)(x >> (8 * i)); }; auto u16 = [&](uint32_t x) { f += (char)(x & 255); f += (char)(x >> 8); };
      f += "RIFF"; u32(0); f += "WAVE"; f += "fmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
      if (junkBefore) { f += "JUNK"; u32(junkBefore); f.append(junkBefore, '\0'); }
      f += "data"; u32(nb); for (uint32_t i = 0; i < nb / 2; i++) u16((uint32_t)(int16_t)(1000 * std::sin(i * 0.01)));
      if (tailChunk) { f += "LIST"; u32(1 << 20); f.append(1 << 20, 'x'); }
      writeFileAtomic(path, f); return f.size(); };
    std::string err; bool cut = false; SampleRef s;
    const size_t big = writeWav(joinPath(gDir, "long.wav"), 600, false, 0);   // 10 min at 8 kHz: 9.6 MB, more than the 4 MB first read
    CHECK(loadWavFile(joinPath(gDir, "long.wav"), s, err, &cut) && cut && s->len == 60 * 8000, "10 min file: len %d cut %d %s (file %zu)", s ? s->len : -1, cut, err.c_str(), big);
    writeWav(joinPath(gDir, "tail.wav"), 30, true, 0);   // 30 s + a 1 MB chunk after the audio, file > 4 MB? no: then make it longer
    writeWav(joinPath(gDir, "tail2.wav"), 50, true, 0);
    cut = true; CHECK(loadWavFile(joinPath(gDir, "tail2.wav"), s, err, &cut) && !cut && s->len == 50 * 8000, "50 s + trailing chunk: not cut (len %d cut %d)", s ? s->len : -1, cut);
    cut = false; writeWav(joinPath(gDir, "junk.wav"), 70, false, 5u << 20);   // 5 MB of other chunks before the audio
    CHECK(loadWavFile(joinPath(gDir, "junk.wav"), s, err, &cut) && cut && s->len == 60 * 8000, "audio after 5 MB of chunks: len %d cut %d %s", s ? s->len : -1, cut, err.c_str());
    { std::vector<double> l(480000, 0.2); const auto w = makeWav(1, 16, 1, 8000, { l }); uint64_t tot = 0; std::string pre;
      writeFileAtomic(joinPath(gDir, "r.wav"), w.data(), w.size()); CHECK(readFilePrefix(joinPath(gDir, "r.wav"), 100, pre, &tot) && pre.size() == 100 && tot == w.size(), "readFilePrefix"); }
    // the session cache lets go of samples nothing refers to, keeps pinned ones (undo snapshots) and ones still in use
    std::unique_ptr<Core> core(new Core()); defaultProject(*core->S);
    SampleRef a = noiseSample("a", 100, 48000, SF_S16, 1), b2 = noiseSample("b", 100, 48000, SF_S16, 2), c = noiseSample("c", 100, 48000, SF_S16, 3);
    core->cache.add(a); core->cache.add(b2); core->cache.add(c); core->S->smp[0] = a; const std::string bid = b2->id, cid = c->id; b2.reset(); c.reset();
    core->pinned.insert(bid);
    CHECK(core->pruneCache() == 1 && core->cache.find(a->id) && core->cache.find(bid) && !core->cache.find(cid), "pruneCache");
    core->pinned.clear(); CHECK(core->pruneCache() == 1 && !core->cache.find(bid), "unpinned sample released");
    // a sound's own WAV counts in its change signature (auto-load of a re-rendered sample)
    { const std::string dir = joinPath(libSoundsDir(), "Sig"); double p[NP]; defaultTrackParams(M_SMP, p);
      const std::string jp = saveSound(dir, "pair", M_SMP, p, a, err); LibIndex L1; scanLibrary(libSoundsDir(), true, L1);
      const std::vector<uint8_t> w2 = encodeWav(*noiseSample("pair", 3000, 48000, SF_S16, 9)); writeFileAtomic(joinPath(dir, "pair.wav"), w2.data(), w2.size());
      LibIndex L2; scanLibrary(libSoundsDir(), true, L2); uint64_t s1 = 0, s2 = 0; for (auto& it : L1.items) if (it.path == jp) s1 = it.size; for (auto& it : L2.items) if (it.path == jp) s2 = it.size;
      CHECK(s1 && s2 && s1 != s2, "the sound's WAV is part of its size (%llu vs %llu)", (unsigned long long)s1, (unsigned long long)s2); }
  }
  // ===== J: sample lifetime between the main thread and the audio thread =====
  {
    std::unique_ptr<Core> core(new Core()); std::unique_ptr<Engine> A(new Engine()); core->eng = A.get();
    defaultProject(*core->S); A->init(sr);
    for (int t = 0; t < NT; t++) { core->S->tracks[t].m = M_SMP; defaultTrackParams(M_SMP, core->S->tracks[t].p); core->S->tracks[t].p[2] = t; core->S->tracks[t].p[5] = 2; }
    core->sendAll(true); core->link.drainAll(*A);
    std::atomic<bool> stop{ false }; std::atomic<long> blocks{ 0 };
    std::thread audio([&] { std::vector<float> L(64), R(64); while (!stop) { core->link.drain(*A); A->process(L.data(), R.data(), 64); blocks++; } });
    std::mt19937 g(5);
    for (int k = 0; k < 6000; k++) {
      std::lock_guard<std::recursive_mutex> lk(core->mtx);
      const int slot = (int)(g() % 8);
      core->S->smp[slot] = (g() % 5) ? noiseSample("x", 200 + (int)(g() % 3000), 48000, SF_S24, (uint32_t)k) : nullptr;
      core->cache.m.clear();   // do not keep them alive through the cache: only the retire list may
      core->sendSamples();
      if (k % 7 == 0) core->sendAud((int)(g() % 8), 120, nullptr);
      if (k % 500 == 250) { for (int i = 0; i < 9000; i++) core->sendParam(0, 21, 100); core->syncAllIfOverflow(); }   // overflow path
      if (k % 50 == 0) core->sendAll(true);
      core->gcSamples(); core->cache.m.clear();
    }
    stop = true; audio.join();
    core->link.drainAll(*A); core->gcSamples();
    CHECK(core->retired.empty(), "retired samples left after the engine caught up: %zu", core->retired.size());
    CHECK(blocks > 1000, "audio thread did not run");
    for (int i = 0; i < NSLOT; i++) CHECK(A->smp[i] == core->S->smp[i].get(), "engine slot %d out of sync", i);
  }
  std::printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
  return fails ? 1 : 0;
}
