// Mutation fuzzing of every file HARDER reads: projects (with samples), sound / groove files, control.json, WAV.
// Build with -fsanitize=address,undefined. usage: fuzz_parsers <iterations> <temp dir>
#include "../plugins/Harder/Library.hpp"
#include "../plugins/Harder/Control.hpp"
#include <random>
#include <cstdio>
using namespace hd;
static std::string gDir; namespace hd { std::string harderConfigDir() { return gDir; } }

static std::mt19937 rng(12345);
static std::string mutate(std::string s) {
  const int n = 1 + (int)(rng() % 8);
  for (int k = 0; k < n && !s.empty(); k++) {
    const size_t p = rng() % s.size();
    switch (rng() % 9) {
      case 0: s[p] = (char)(rng() & 255); break;
      case 1: s.erase(p, 1 + rng() % 16); break;
      case 2: s.insert(p, std::string(1 + rng() % 4, (char)(rng() & 255))); break;
      case 3: s.resize(p); break;
      case 4: { static const char* const T[] = { "1e308", "-1e308", "NaN", "-0", "99999999999", "-2147483649", "null", "[]", "{}", "\"\"", "1.5", "64", "-1", "0" };
                const size_t q = s.find_first_of("0123456789", p); if (q != std::string::npos) { size_t e = q; while (e < s.size() && std::strchr("0123456789.-e", s[e])) e++; s.replace(q, e - q, T[rng() % 14]); } break; }
      case 5: { const size_t q = rng() % s.size(); if (q > p) s.insert(p, s.substr(p, std::min<size_t>(q - p, 64))); break; }
      case 6: s[p] = "{}[]\",:"[rng() % 7]; break;
      case 7: if (s.size() > 8) { const uint32_t v = (uint32_t)rng(); for (int i = 0; i < 4 && p + i < s.size(); i++) s[p + i] = (char)(v >> (8 * i)); } break;   // WAV sizes
      default: for (size_t i = p; i < s.size() && i < p + 4; i++) s[i] = (char)0xFF; break;
    }
  }
  return s;
}
static std::vector<uint8_t> makeWav(int tag, int bits, int ch, int frames) {
  std::vector<uint8_t> o; auto u16 = [&](uint32_t x) { o.push_back(x & 255); o.push_back(x >> 8 & 255); }; auto u32 = [&](uint32_t x) { u16(x & 65535); u16(x >> 16); };
  auto t = [&](const char* s) { o.insert(o.end(), s, s + 4); };
  const int ba = bits / 8 * ch; t("RIFF"); u32(36 + frames * ba); t("WAVE"); t("fmt "); u32(16); u16(tag); u16(ch); u32(44100); u32(44100 * ba); u16(ba); u16(bits);
  t("data"); u32(frames * ba); for (int i = 0; i < frames * ba; i++) o.push_back((uint8_t)(rng() & 255)); return o;
}

int main(int argc, char** argv) {
  const long N = argc > 1 ? std::atol(argv[1]) : 20000; gDir = argc > 2 ? argv[2] : "/tmp/hdfuzz"; makeDir(gDir); ensureLibrary();
  // seeds
  std::unique_ptr<Project> P(new Project()); defaultProject(*P);
  { std::shared_ptr<SampleData> s(new SampleData()); s->name = "x"; s->rate = 48000; s->fmt = SF_S16; s->d.assign(300, 0.25f); finishSample(*s); P->smp[3] = s; P->tracks[1].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[1].p); P->tracks[1].p[2] = 3; }
  // 2.0: a stereo sample with slice points on a slicing track, chain, song, track outputs
  { std::shared_ptr<SampleData> s(new SampleData()); s->name = "st"; s->rate = 44100; s->fmt = SF_S24; s->ch = 2; for (int i = 0; i < 400; i++) { s->d.push_back((float)((i % 50) / 100.0)); s->dR.push_back((float)(-(i % 30) / 100.0)); } s->slices = { 0, 100, 250 }; finishSample(*s);
    P->smp[5] = s; P->tracks[4].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[4].p); P->tracks[4].p[2] = 5; P->tracks[4].p[51] = 5; P->tracks[4].p[52] = 2; }
  P->seq = ARR_SONG; P->chain = { 1, 2, 2 }; P->song.n = 3; P->song.rows[0].pat = 1; P->song.rows[0].reps = 2; P->song.rows[1].pat = 3; P->song.rows[1].mute = 5; P->song.rows[1].bpm = 140; P->song.rows[2].pat = 0; P->song.loop = false; P->outs[2] = 1;
  const std::string projSeed = projectToString(*P);
  std::string err; double p[NP]; defaultTrackParams(M_SMP, p);
  const std::string snd = saveSound(joinPath(libSoundsDir(), "F"), "seed", M_SMP, p, P->smp[3], err);
  std::string soundSeed; readTextFile(snd, soundSeed);
  { JVal o = JVal::Obj(); o.set("format", JVal::Str("harder-sound")); o.set("version", JVal::Num(1)); o.set("machine", JVal::Str("SMP")); o.set("sample", sampleToJson(*P->smp[3], true)); std::string s2; writeJson(s2, o); soundSeed += "\n"; soundSeed = s2; }
  const std::string grv = saveGroove(joinPath(libGroovesDir(), "F"), "g", P->patterns[0], err); std::string grooveSeed; readTextFile(grv, grooveSeed);
  ControlMap cm; cm.defaults(); { MidiBind b; b.type = MT_CC; b.ch = 2; b.num = 7; b.a = act(AC_PARAM, 8, 19); b.mode = CM_REL64; cm.bind(b); }
  std::string ctlSeed; writeJson(ctlSeed, cm.toJson());
  const std::string kitPath = saveKit(joinPath(libKitsDir(), "F"), "k", *P, true, err); std::string kitSeed; readTextFile(kitPath, kitSeed);
  std::vector<std::string> wavSeeds;
  { const auto w = encodeWav(*P->smp[5]); wavSeeds.push_back(std::string(w.begin(), w.end())); }   // stereo with cue points
  for (int tag : { 1, 3 }) for (int bits : { 8, 16, 24, 32, 64 }) { if ((tag == 3) != (bits >= 32 && tag == 3)) continue; const auto w = makeWav(tag, bits, 1 + (int)(rng() % 3), 50); wavSeeds.push_back(std::string(w.begin(), w.end())); }
  long ok[6] = { 0 }, runs[6] = { 0 };
  SampleCache cache;
  for (long it = 0; it < N; it++) {
    const int kind = (int)(it % 6);
    runs[kind]++;
    if (kind == 0) { std::unique_ptr<Project> Q(new Project()); if (projectFromString(mutate(projSeed), *Q, &cache)) { ok[0]++; projectToString(*Q); std::unique_ptr<Engine> E(new Engine()); E->init(8000); for (int i = 0; i < NSLOT; i++) E->setSample(i, Q->smp[i].get()); for (int t = 0; t < NT; t++) E->loadTrack(t, Q->tracks[t].m, Q->tracks[t].p); for (int i = 0; i < NPAT; i++) E->setPattern(i, Q->patterns[i]); E->loadDone(Q->cur, Q->bpm, Q->swing, Q->mutes); E->setArrangement(Q->arrangement()); E->internalPlay(); std::vector<float> L(256), R(256), O[2 * NT]; for (int t = 0; t < NT; t++) E->audition(t, 127, nullptr, (int)(rng() % 70) - 2);
      for (int t = 0; t < NT; t++) if (Q->outs[t]) { O[2 * t].resize(256); O[2 * t + 1].resize(256); }
      std::vector<float> RL(4096), RR(4096); E->recArm(RL.data(), RR.data(), 4096, (int)(rng() % 9) - 1, 1, true);
      for (int b = 0; b < 8; b++) { for (int t = 0; t < NT; t++) { E->tout[t][0] = O[2 * t].empty() ? nullptr : O[2 * t].data(); E->tout[t][1] = O[2 * t + 1].empty() ? nullptr : O[2 * t + 1].data(); } E->process(L.data(), R.data(), 256); }
      for (int b = 0; b < 4; b++) { E->hostBlock(true, b * 0.7 + (rng() % 100), 120); E->process(L.data(), R.data(), 256); } } }
    else if (kind == 1) { JVal j; SoundDef d; if (parseJson(mutate(soundSeed), j) && soundFromJson(j, dirName(snd), d, &cache, err)) { ok[1]++; std::unique_ptr<Engine> E(new Engine()); E->init(8000); if (d.smp) E->setSample(0, d.smp.get()); d.p[2] = 0; E->loadTrack(0, d.m, d.p); E->audition(0, 127, nullptr); std::vector<float> L(256), R(256); for (int b = 0; b < 4; b++) E->process(L.data(), R.data(), 256); } }
    else if (kind == 2) { const std::string g = mutate(grooveSeed); writeFileAtomic(grv, g); LibItem li; li.path = grv; li.kind = LK_GROOVE; std::unique_ptr<Pattern> Pt(new Pattern()); std::string nm; if (loadGrooveItem(li, *Pt, nm, err)) ok[2]++; }
    else if (kind == 3) { JVal j; ControlMap c; if (parseJson(mutate(ctlSeed), j)) { c.fromJson(j); ok[3]++; for (auto& b : c.midi) { (void)relDelta(b.mode, (int)(rng() % 128)); (void)actId(b.a); } } }
    else if (kind == 5) { const std::string k = mutate(kitSeed); writeFileAtomic(kitPath, k); LibItem li; li.path = kitPath; li.kind = LK_KIT; UserKit uk; if (loadKitItem(li, uk, &cache, err)) ok[5]++; }
    else { const std::string w = mutate(wavSeeds[rng() % wavSeeds.size()]); bool cut; SampleRef s = decodeWav("f", (const uint8_t*)w.data(), w.size(), &err, &cut); if (s) { ok[4]++; (void)detectSlices(*s, (double)(rng() % 101)); std::unique_ptr<Engine> E(new Engine()); E->init(8000); E->setSample(0, s.get()); double q[NP]; defaultTrackParams(M_SMP, q); q[0] = (double)(rng() % 49) - 24; q[5] = rng() % 4; q[51] = rng() % 6; q[52] = rng() % 65; E->loadTrack(0, M_SMP, q); E->audition(0, 127, nullptr); std::vector<float> L(256), R(256); for (int b = 0; b < 4; b++) E->process(L.data(), R.data(), 256); } }
  }
  std::printf("fuzz: %ld inputs, no crash. accepted after mutation: project %ld/%ld, sound %ld/%ld, groove %ld/%ld, control %ld/%ld, wav %ld/%ld, kit %ld/%ld\n", N, ok[0], runs[0], ok[1], runs[1], ok[2], runs[2], ok[3], runs[3], ok[4], runs[4], ok[5], runs[5]);
}
