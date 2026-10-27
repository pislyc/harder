// HARDER 2.0 features, engine and file level: sample slices and stereo, song / chain (internal clock and host
// positions), resampling, per-track outputs, MIDI clock in / out, kits, WAV cue points, transient detection,
// project keys. Build with -fsanitize=address,undefined. usage: v2_test <temp dir>
#include "../plugins/Harder/HarderShared.hpp"
#include "../plugins/Harder/Library.hpp"
#include "../plugins/Harder/Export.hpp"
#include "../plugins/Harder/Clock.hpp"
#include <cstdio>
#include <random>
#include <tuple>
using namespace hd;
static std::string gDir; namespace hd { std::string harderConfigDir() { return gDir; } }
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double SR = 48000;
static void load(Engine& E, const Project& P) {
  for (int i = 0; i < NSLOT; i++) E.setSample(i, P.smp[i].get());
  for (int t = 0; t < NT; t++) E.loadTrack(t, P.tracks[t].m, P.tracks[t].p);
  E.loadMaster(P.master); for (int i = 0; i < NPAT; i++) E.setPattern(i, P.patterns[i]); E.loadDone(P.cur, P.bpm, P.swing, P.mutes);
  E.setArrangement(P.arrangement());
}
struct Hit { long t; int tr; };
static std::vector<Hit>* gHits = nullptr; static long gOff = 0;
static void hitCb(int tr, double t, double) { if (gHits) gHits->push_back({ (long)t - gOff, tr }); }
// a project where pattern p fires only track p at its step 0 (track 7 = a click on every step, for the mutes)
static void markerProject(Project& P, const int* mlen, bool click) {
  P.reset();
  for (int t = 0; t < NT; t++) { P.tracks[t].m = M_SUB; defaultTrackParams(M_SUB, P.tracks[t].p); P.tracks[t].p[42] = 100; }
  for (int p = 0; p < NPAT; p++) {
    Pattern& Q = P.patterns[p]; Q.reset(); Q.mlen = mlen[p]; Q.bpm = 120; Q.swing = 50;
    for (int t = 0; t < NTR; t++) { Q.len[t] = mlen[p]; Q.scl[t] = 4; }
    Q.steps[p][0].on = 1;
    if (click) for (int s = 0; s < mlen[p]; s++) Q.steps[7][s].on = 1;
  }
  P.cur = 0; P.bpm = 120; P.swing = 50;
}
static std::vector<Hit> run(Engine& E, long n, int B = 128) {
  std::vector<Hit> h; gHits = &h; gOff = 0; E.dbgTrig = hitCb; std::vector<float> L(B), R(B);
  for (long pos = 0; pos < n; pos += B) E.process(L.data(), R.data(), B);
  gHits = nullptr; return h;
}
static std::vector<std::pair<long, int>> markers(const std::vector<Hit>& h) { std::vector<std::pair<long, int>> o; for (auto& x : h) if (x.tr != 7) o.push_back({ x.t, x.tr }); return o; }

int main(int argc, char** argv) {
  gDir = argc > 1 ? std::string(argv[1]) + "/" : "/tmp/hd_v2/"; makeDir(gDir);
  // ===== A: slices and stereo in the SAMPLE machine =====
  {
    const int n = 48000; std::shared_ptr<SampleData> s(new SampleData()); s->rate = SR; s->fmt = SF_F32; s->ch = 2; s->d.resize(n); s->dR.resize(n);
    for (int i = 0; i < n; i++) { s->d[i] = (float)(0.5 * i / n); s->dR[i] = -s->d[i]; }
    s->slices = { 0, 3000, 9000, 20000, 41000 }; finishSample(*s);
    CHECK(s->stereo() && s->slices.size() == 5, "stereo sample with slices");
    const SampleData* tab[NSLOT] = { s.get() };
    double e[NP]; defaultTrackParams(M_SMP, e);
    struct Case { int slc, sel, step; double lo, hi; } C[] = {
      { 0, 0, 5, 0, n }, { 1, 0, 3, 3 * 6000, 4 * 6000 }, { 2, 0, 17, 1 * 3000, 2 * 3000 }, { 2, 5, 9, 4 * 3000, 5 * 3000 },
      { 4, 0, 63, 63 * 750, 64 * 750 }, { 5, 0, 2, 9000, 20000 }, { 5, 0, 4, 41000, n }, { 5, 7, 0, 3000, 9000 } };   // 7 % 5 = 2 -> slice 2
    for (const Case& c : C) {
      e[51] = c.slc; e[52] = c.sel; MSmp m; m.step = c.step; m.ctl(e, 1, SR, tab); m.trig(tab);
      double lo, hi; MSmp::sliceRange(*s, c.slc, c.sel, c.step, lo, hi);
      if (c.slc == 5 && c.sel == 7) { lo = s->slices[(7 - 1) % 5]; hi = (7 - 1) % 5 + 1 < 5 ? s->slices[(7 - 1) % 5 + 1] : n; }
      CHECK(lo == c.lo && hi == c.hi, "slice range slc %d sel %d step %d: %g-%g, want %g-%g", c.slc, c.sel, c.step, lo, hi, c.lo, c.hi);
      CHECK(m.a == lo && m.b == hi, "playback range %g-%g", m.a, m.b);
      // one-shot inside the slice: the sample values (rate 1), the 32-sample fade-in after a cut, the 64-sample fade-out
      int k = 0; double worst = 0; const double lev = mpow(100.0 / 100, 2);
      while (m.alive() && k < n + 10) {
        const double y = m.tick(tab), yR = m.outR; const double i = lo + k;
        double g = 1; if (lo > 0 && k < 32) g *= (k + 1.0) / 33.0; const double left = hi - i; if (hi < n && left < 64) g *= std::max(0.0, left / 64);
        worst = std::max(worst, std::fabs(y - s->d[(int)i] * g * lev)); worst = std::max(worst, std::fabs(yR - s->dR[(int)i] * g * lev)); k++;
      }
      CHECK(k == (int)(hi - lo) && worst < 1e-9, "one-shot slice: %d samples (want %g), worst %g", k, hi - lo, worst);
    }
    // reversed slice: starts at the slice's last frame
    e[51] = 2; e[52] = 0; e[5] = 1; MSmp m; m.step = 2; m.ctl(e, 1, SR, tab); m.trig(tab);
    const double y0 = m.tick(tab); CHECK(std::fabs(y0 - s->d[3 * 3000 - 1] * (1.0 / 33)) < 1e-9, "reverse slice start %g", y0);
    // the engine passes the trig's step: 16 slices, steps 0..15, each trig starts at its slice
    std::unique_ptr<Project> P(new Project()); P->reset(); P->smp[0] = s;
    P->tracks[0].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[0].p); P->tracks[0].p[51] = 2;
    Pattern& Q = P->patterns[0]; Q.mlen = 16; for (int t = 0; t < NTR; t++) Q.len[t] = 16; for (int st = 0; st < 16; st++) Q.steps[0][st].on = 1;
    P->bpm = 120;
    std::unique_ptr<Engine> E(new Engine()); E->init(SR); load(*E, *P); E->play(0);
    std::vector<float> L(1), R(1); int ok = 0, bad = 0, stereoDiff = 0, trigs = 0;
    static Engine* gEng; static int* gOk; static int* gBad; static int* gTr; gEng = E.get(); gOk = &ok; gBad = &bad; gTr = &trigs;
    E->dbgTrig = [](int, double, double) { const int stp = (*gTr)++ % 16; const double want = std::floor((double)stp * 48000 / 16); if (gEng->voices[0].mach.smp.a == want && gEng->voices[0].mach.smp.step == stp) (*gOk)++; else (*gBad)++; };
    for (long i = 0; i < (long)(16 * SR * 60 / 120 / 4) - 64; i++) {
      E->process(L.data(), R.data(), 1);
      if (std::fabs(L[0] - R[0]) > 1e-3) stereoDiff++;
    }
    CHECK(ok == 16 && bad == 0, "engine: %d trigs at their slice, %d not", ok, bad);
    CHECK(stereoDiff > 1000, "stereo sample plays different channels (%d samples)", stereoDiff);
    // a mono sample without slices keeps the id it had in 1.x (FNV over rate, format, bytes)
    std::shared_ptr<SampleData> mo(new SampleData()); mo->rate = 44100; mo->fmt = SF_S16; mo->d = { 0.f, 0.5f, -0.5f, 0.25f }; finishSample(*mo);
    { const std::vector<uint8_t> b = mo->bytes(); uint64_t h = 1469598103934665603ull; const uint32_t r = 44100; const uint8_t hd5[5] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24), (uint8_t)SF_S16 };
      h = fnv1a(hd5, 5, h); h = fnv1a(b.data(), b.size(), h); char id[20]; std::snprintf(id, sizeof id, "%016llx", (unsigned long long)h);
      CHECK(mo->id == id, "mono id unchanged (%s vs %s)", mo->id.c_str(), id); }
    SampleRef sl = withSlices(*mo, { 0, 2 }); CHECK(sl->id != mo->id && sl->d == mo->d, "slice points give a new id, same audio");
  }
  // ===== B: chain and song =====
  {
    const int ml[NPAT] = { 16, 32, 8, 16, 16, 16, 16, 16 };
    const double st = SR * 60 / 120 / 4;   // samples per 16th at 120 BPM
    std::unique_ptr<Project> P(new Project()); markerProject(*P, ml, true);
    // chain 1 1 3 2 (patterns 0 0 2 1), looping
    P->seq = ARR_CHAIN; P->chain = { 0, 0, 2, 1 };
    std::unique_ptr<Engine> E(new Engine()); E->init(SR); load(*E, *P); E->play(0);
    auto h = markers(run(*E, (long)(st * (16 + 16 + 8 + 32 + 16 + 4))));
    const long want[][2] = { { 0, 0 }, { 16, 0 }, { 32, 2 }, { 40, 1 }, { 72, 0 }, { 88, 0 } };
    bool ok = h.size() >= 6; for (int k = 0; k < 6 && ok; k++) ok = std::labs(h[k].first - (long)jsround(want[k][0] * st)) <= 1 && h[k].second == want[k][1];
    CHECK(ok, "chain order / times (%zu hits)", h.size());
    // song: P1 x2, P3 x1 muting the click, P2 x1 at 150 BPM, then stop
    P->seq = ARR_SONG; P->song = Arrangement(); P->song.mode = ARR_SONG; P->song.loop = false; P->song.n = 3;
    P->song.rows[0].pat = 0; P->song.rows[0].reps = 2; P->song.rows[1].pat = 2; P->song.rows[1].reps = 1; P->song.rows[1].mute = 1 << 7;
    P->song.rows[2].pat = 1; P->song.rows[2].reps = 1; P->song.rows[2].bpm = 150;
    E.reset(new Engine()); E->init(SR); load(*E, *P); E->play(0);
    const double st150 = SR * 60 / 150 / 4; const long end = (long)jsround(40 * st + 32 * st150);
    auto all = run(*E, end + (long)(SR * 2));
    auto mk = markers(all);
    CHECK(mk.size() == 4 && mk[0].second == 0 && mk[1].second == 0 && std::labs(mk[1].first - (long)(16 * st)) <= 1 && mk[2].second == 2 && std::labs(mk[2].first - (long)(32 * st)) <= 1
          && mk[3].second == 1 && std::labs(mk[3].first - (long)(40 * st)) <= 1, "song rows (%zu markers)", mk.size());
    int clicksRow2 = 0, clicksAfter = 0; long lastClick = 0;
    for (auto& x : all) if (x.tr == 7) { if (x.t >= (long)(32 * st) - 2 && x.t < (long)(40 * st) - 2) clicksRow2++; if (x.t >= end - 2) clicksAfter++; lastClick = x.t; }
    CHECK(clicksRow2 == 0, "row mute: %d clicks in the muted row", clicksRow2);
    CHECK(clicksAfter == 0 && !E->playing && !E->fb.playing.load(), "song stops at the end (%d clicks after, last %ld vs end %ld)", clicksAfter, lastClick, end);
    { long a = -1, b = -1; for (auto& x : all) if (x.tr == 7 && x.t >= (long)(40 * st) - 2) { if (a < 0) a = x.t; else if (b < 0) b = x.t; }
      CHECK(b > a && std::labs((b - a) - (long)jsround(st150)) <= 1, "row tempo 150 BPM: click spacing %ld (want %.1f)", b - a, st150); }
    // a cue while the song plays takes one loop, then the song goes on where it was
    P->song.loop = true; P->song.rows[2].bpm = 0; E.reset(new Engine()); E->init(SR); load(*E, *P); E->play(0);
    std::vector<Hit> hh; gHits = &hh; E->dbgTrig = hitCb; std::vector<float> L(128), R(128);
    for (long pos = 0; pos < (long)(st * 120); pos += 128) { if (pos >= (long)(st * 4) && pos < (long)(st * 4) + 128) E->msgCue(5); E->process(L.data(), R.data(), 128); }
    gHits = nullptr; auto cm = markers(hh);
    // P1 (row 1, rep 1), cued P6, P1 (row 1, rep 2), P3, P2, P1 ...
    const int seq[] = { 0, 5, 0, 2, 1, 0 }; bool cok = cm.size() >= 6; for (int k = 0; k < 6 && cok; k++) cok = cm[k].second == seq[k];
    CHECK(cok, "cue during the song: %d %d %d %d %d %d", cm.size() > 0 ? cm[0].second : -1, cm.size() > 1 ? cm[1].second : -1, cm.size() > 2 ? cm[2].second : -1, cm.size() > 3 ? cm[3].second : -1, cm.size() > 4 ? cm[4].second : -1, cm.size() > 5 ? cm[5].second : -1);
    // host positions: starting Live at bar N plays what the song plays there (same trigs as from bar 1)
    P->song.rows[2].bpm = 0; P->song.loop = true;
    for (double startPpq : { 0.0, 4.0, 8.0, 9.25, 17.0, 26.0 }) {
      const double bpm = 120; const int B = 128; std::vector<Hit> ta, tc;
      std::unique_ptr<Engine> A(new Engine()), Cn(new Engine()); A->init(SR); Cn->init(SR); load(*A, *P); load(*Cn, *P);
      const long total = (long)((startPpq + 40) * 60 / bpm * SR);
      A->dbgTrig = hitCb; gHits = &ta; gOff = 0; double ppq = 0;
      for (long pos = 0; pos < total; pos += B) { A->hostBlock(true, ppq, bpm); A->process(L.data(), R.data(), B); ppq += B / SR * bpm / 60; }
      const long s0 = (long)std::llround(startPpq * 60 / bpm * SR); Cn->dbgTrig = hitCb; gHits = &tc; gOff = -s0; ppq = startPpq;
      for (long pos = s0; pos < total; pos += B) { Cn->hostBlock(true, ppq, bpm); Cn->process(L.data(), R.data(), B); ppq += B / SR * bpm / 60; }
      gHits = nullptr;
      const long from = (long)std::ceil(std::ceil(startPpq * 4 - 1e-9) / 4 * 60 / bpm * SR) + 2, to = total - B * 8;
      std::vector<std::pair<long, int>> a, c; for (auto& x : ta) if (x.t >= from && x.t < to) a.push_back({ x.t, x.tr }); for (auto& x : tc) if (x.t >= from && x.t < to) c.push_back({ x.t, x.tr });
      std::sort(a.begin(), a.end()); std::sort(c.begin(), c.end());
      bool same = a.size() == c.size() && !a.empty(); long dt = 0; for (size_t k = 0; same && k < a.size(); k++) { same = a[k].second == c[k].second; dt = std::max(dt, std::labs(a[k].first - c[k].first)); }
      CHECK(same && dt <= 1, "song from host ppq %.2f: %zu vs %zu trigs, max dt %ld", startPpq, a.size(), c.size(), dt);
    }
    // song with STOP: a host position after the end is silent
    P->song.loop = false; E.reset(new Engine()); E->init(SR); load(*E, *P);
    { std::vector<Hit> x; gHits = &x; gOff = 0; E->dbgTrig = hitCb; double ppq = 30; for (int k = 0; k < 400; k++) { E->hostBlock(true, ppq, 120); E->process(L.data(), R.data(), 128); ppq += 128 / SR * 2; } gHits = nullptr;
      CHECK(x.empty() && !E->playing, "after the end of a song that stops: %zu trigs", x.size()); }
  }
  // ===== C: resampling =====
  {
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    const int cap = (int)(SR * 60); std::vector<float> RL(cap), RR(cap);
    std::unique_ptr<Engine> E(new Engine()); E->init(SR); load(*E, *P);
    E->recArm(RL.data(), RR.data(), cap, -1, 0, false);   // main, one pattern, from the pattern start
    CHECK(E->fb.recState.load() == REC_WAIT, "armed");
    E->play(0);
    const int ml = P->patterns[P->cur].mlen; const double stepLen = SR * 60 / P->patterns[P->cur].bpm / 4; const long len = (long)jsround(ml * stepLen);
    std::vector<float> OL((size_t)len + 4096), OR2((size_t)len + 4096);
    for (long pos = 0; pos < len + 4096; pos += 256) E->process(OL.data() + pos, OR2.data() + pos, (int)std::min<long>(256, len + 4096 - pos));
    CHECK(E->fb.recState.load() == REC_DONE && E->fb.recN.load() == len, "main: done, %lld of %ld samples", (long long)E->fb.recN.load(), len);
    bool same = true; for (long i = 0; i < len && same; i++) same = RL[i] == OL[i] && RR[i] == OR2[i];
    CHECK(same, "main: the recording is the output from the pattern start");
    // one track, at once, two bars; and the stop button keeps what is there
    E.reset(new Engine()); E->init(SR); load(*E, *P); E->play(0);
    std::vector<float> st0(4096); E->stems[2] = nullptr;
    std::vector<float> L(128), R(128); for (int k = 0; k < 40; k++) E->process(L.data(), R.data(), 128);
    E->recArm(RL.data(), RR.data(), cap, 2, 32, true);
    std::vector<float> stem(1 << 16); long n = 0;
    for (; n < (1 << 16); n += 128) { E->stems[2] = stem.data() + n; E->process(L.data(), R.data(), 128); }
    E->stems[2] = nullptr;
    const double gL = E->voices[2].gL, gR = E->voices[2].gR; double worst = 0;
    for (long i = 0; i < (1 << 16); i++) worst = std::max(worst, std::max(std::fabs(RL[i] - stem[i] * gL), std::fabs(RR[i] - stem[i] * gR)));
    CHECK(E->fb.recState.load() == REC_ON && worst < 1e-6, "track 3 tap: state %d, worst %g", E->fb.recState.load(), worst);
    E->recCancel(); CHECK(E->fb.recState.load() == REC_DONE && E->fb.recN.load() == (1 << 16), "stop keeps %lld", (long long)E->fb.recN.load());
    E->recArm(RL.data(), RR.data(), cap, -1, 0, false); E->recCancel(); CHECK(E->fb.recState.load() == REC_IDLE, "cancel while waiting");
  }
  // ===== D: per-track outputs =====
  {
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    for (int t = 0; t < NT; t++) { P->tracks[t].p[24] = 0; P->tracks[t].p[25] = 0; P->tracks[t].p[23] = 0; }   // no sends, no duck amount
    P->master[24] = 0;   // no sidechain source
    int RT = 0, best = -1; for (int t = 0; t < NT; t++) { int n = 0; for (auto& st2 : P->patterns[P->cur].steps[t]) n += st2.on; if (n > best) { best = n; RT = t; } }
    std::unique_ptr<Engine> A(new Engine()), B(new Engine()); A->init(SR); B->init(SR); load(*A, *P); load(*B, *P); B->msgMute(RT, true);
    A->play(0); B->play(0);
    const int N = 1 << 17, BL = 256; std::vector<float> aL(N), aR(N), bL(N), bR(N), oL(N), oR(N), stem(N);
    for (int pos = 0; pos < N; pos += BL) { A->tout[RT][0] = oL.data() + pos; A->tout[RT][1] = oR.data() + pos; A->stems[RT] = stem.data() + pos; A->process(aL.data() + pos, aR.data() + pos, BL); B->process(bL.data() + pos, bR.data() + pos, BL); }
    double dm = 0; for (int i = 0; i < N; i++) dm = std::max(dm, (double)std::max(std::fabs(aL[i] - bL[i]), std::fabs(aR[i] - bR[i])));
    double pk = 0; for (int i = 0; i < N; i++) pk = std::max(pk, (double)std::fabs(oL[i]));
    const int D = A->LA + 1; const double gL = A->voices[RT].gL, gR = A->voices[RT].gR; double dr = 0;
    for (int i = D; i < N; i++) dr = std::max(dr, std::max(std::fabs(oL[i] - 0.5 * stem[i - D] * gL), std::fabs(oR[i] - 0.5 * stem[i - D] * gR)));
    CHECK(dm == 0, "main without the routed track = main with the track muted (max diff %g)", dm);
    CHECK(pk > 0.05 && dr < 1e-6, "routed output = the track x 0.5, %d samples later like the limiter (peak %.3f, diff %g)", D, pk, dr);
  }
  // ===== E/F: MIDI clock =====
  {
    // follow a jittery clock at 125 BPM and compare the trigs with the internal clock started at the first tick
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    for (auto& pt : P->patterns) for (auto& row : pt.steps) for (auto& s2 : row) { s2.l.del(42); s2.l.del(50); s2.l.del(44); }
    for (int t = 0; t < NT; t++) P->tracks[t].p[42] = 100;
    for (auto& pt : P->patterns) pt.swing = 50;
    P->swing = 50;
    const double bpm = 125, tick = SR * 60 / bpm / 24, t0 = 10000.5; const int B = 256;
    std::mt19937 rng(7); std::uniform_real_distribution<double> J(-SR * 0.002, SR * 0.002);
    std::unique_ptr<Engine> F(new Engine()), G(new Engine()); F->init(SR); G->init(SR); load(*F, *P); load(*G, *P);
    ClockFollower cf; cf.reset(SR);
    // a few ticks before Start (clock runs while stopped), Start, then ticks
    std::vector<std::pair<double, uint8_t>> ev;
    for (int k = -48; k < 24 * 64; k++) { if (k == 0) ev.push_back({ t0 - 100, 0xFA }); ev.push_back({ t0 + k * tick + J(rng), 0xF8 }); }
    std::vector<Hit> hf, hg; std::vector<float> L(B), R(B); size_t ei = 0;
    const long total = (long)(t0 + 24 * 60 * tick);
    F->dbgTrig = hitCb; gHits = &hf; gOff = 0;
    for (long pos = 0; pos < total; pos += B) {
      while (ei < ev.size() && ev[ei].first < pos + B) { cf.message(ev[ei].second, 0, 0, std::max((double)pos, ev[ei].first)); ei++; }
      const bool pl = cf.playing(pos); if (pl || F->hostSync) F->hostBlock(pl, pl ? cf.ppq(pos) : 0, cf.bpm());
      F->process(L.data(), R.data(), B);
    }
    G->dbgTrig = hitCb; gHits = &hg; gOff = 0; G->msgBpm(bpm);
    { long pos = 0; for (; pos + B <= (long)t0; pos += B) G->process(L.data(), R.data(), B); G->play(t0 - pos); for (; pos < total; pos += B) G->process(L.data(), R.data(), B); }
    gHits = nullptr;
    // after the first 4 beats (the tracker settles) the trigs match within 1.5 ms
    std::vector<std::pair<long, int>> a, b; const long from = (long)(t0 + 24 * 4 * tick), to = total - (long)(SR * 0.5);
    for (auto& x : hf) if (x.t >= from && x.t < to) a.push_back({ x.t, x.tr }); for (auto& x : hg) if (x.t >= from && x.t < to) b.push_back({ x.t, x.tr });
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    bool same = a.size() == b.size() && a.size() > 100; long dt = 0;
    for (size_t k = 0; same && k < a.size(); k++) { same = a[k].second == b[k].second; dt = std::max(dt, std::labs(a[k].first - b[k].first)); }
    CHECK(same && dt < SR * 0.0015, "clock in, +-2 ms jitter: %zu vs %zu trigs, max %.2f ms", a.size(), b.size(), dt / SR * 1000);
    CHECK(std::fabs(cf.bpm() - bpm) < 0.05, "tempo %.3f", cf.bpm());
    // Stop, then Continue from song position 32 (16ths): the sequencer resumes at step 32
    { ClockFollower c2; c2.reset(SR); for (int k = 0; k < 30; k++) c2.message(0xF8, 0, 0, k * tick);
      c2.message(0xF2, 32, 0, 30 * tick); c2.message(0xFB, 0, 0, 30 * tick); c2.message(0xF8, 0, 0, 31 * tick);
      CHECK(c2.playing(31 * tick) && std::fabs(c2.ppq(31 * tick) - 8.0) < 1e-9, "continue at SPP 32: ppq %.3f", c2.ppq(31 * tick));
      c2.message(0xFC, 0, 0, 32 * tick); CHECK(!c2.playing(32 * tick), "stop"); }
    // clock out: a tick every stepLen / 6, Start just before the tick at the sequencer's start, Stop
    { ClockGen g; std::vector<std::pair<double, uint8_t>> out; auto em = [&](double t, uint8_t b2) { out.push_back({ t, b2 }); };
      const double stepLen = SR * 60 / 133 / 4; double t = 0;
      for (int k = 0; k < 40; k++, t += 256) g.block(t, t + 256, stepLen, false, 0, em);
      const double origin = t + 64; for (int k = 0; k < 400; k++, t += 256) g.block(t, t + 256, stepLen, true, origin, em);
      for (int k = 0; k < 4; k++, t += 256) g.block(t, t + 256, stepLen, false, origin, em);
      int starts = 0, stops = 0; double startT = -1, firstAfter = -1, worstSp = 0, prev = -1;
      for (auto& m : out) { if (m.second == 0xFA) { starts++; startT = m.first; } if (m.second == 0xFC) stops++;
        if (m.second == 0xF8) { if (startT >= 0 && firstAfter < 0 && m.first >= startT) firstAfter = m.first; if (prev >= 0 && startT >= 0 && m.first > startT && prev >= firstAfter) worstSp = std::max(worstSp, std::fabs(m.first - prev - stepLen / 6)); prev = m.first; } }
      CHECK(starts == 1 && stops == 1 && std::fabs(firstAfter - origin) < 1e-6 && startT < origin && worstSp < 1e-6, "clock out: %d starts %d stops, first tick %.2f (origin %.2f), spacing error %g", starts, stops, firstAfter, origin, worstSp); }
  }
  // ===== G: kits in the library =====
  {
    ensureLibrary(); std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    std::shared_ptr<SampleData> s1(new SampleData()); s1->rate = 44100; s1->fmt = SF_S24; s1->ch = 2; s1->name = "loop";
    for (int i = 0; i < 20000; i++) { s1->d.push_back((float)std::sin(i * 0.01) * 0.5f); s1->dR.push_back((float)std::cos(i * 0.013) * 0.4f); }
    s1->slices = { 0, 5000, 12000 }; finishSample(*s1);
    std::shared_ptr<SampleData> s2(new SampleData()); s2->rate = 48000; s2->fmt = SF_S16; s2->name = "hit"; for (int i = 0; i < 3000; i++) s2->d.push_back((float)(std::exp(-i / 300.0) * std::sin(i * 0.2))); finishSample(*s2);
    P->smp[4] = s1; P->smp[9] = s2;
    P->tracks[2].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[2].p); P->tracks[2].p[2] = 4; P->tracks[2].p[51] = 5; P->tracks[2].pre = "Break 底鼓";
    P->tracks[5].m = M_SMP; defaultTrackParams(M_SMP, P->tracks[5].p); P->tracks[5].p[2] = 9; P->tracks[5].p[0] = -3;
    P->roles[1] = 3; P->master[8] = 99;
    std::string err; const std::string dir = joinPath(libKitsDir(), "Mine");
    const std::string jp = saveKit(dir, "Kit A", *P, true, err);
    CHECK(!jp.empty() && pathExists(kitSamplesDir(jp)), "kit saved %s", err.c_str());
    LibIndex L; scanLibrary(libKitsDir(), false, L, true);
    CHECK(L.items.size() == 1 && L.cats.size() == 1 && L.items[0].kind == LK_KIT, "kit listed, its samples folder is not a category (%zu items, %zu cats)", L.items.size(), L.cats.size());
    UserKit k; SampleCache cache; CHECK(loadKitItem(L.items[0], k, &cache, err), "kit loads: %s", err.c_str());
    bool pe = true; for (int t = 0; t < NT; t++) { pe = pe && k.t[t].m == P->tracks[t].m; for (int i = 0; i < NP; i++) pe = pe && k.t[t].p[i] == P->tracks[t].p[i]; }
    CHECK(pe && k.t[2].smp && k.t[2].smp->id == s1->id && k.t[5].smp && k.t[5].smp->id == s2->id && k.t[2].name == "Break 底鼓", "kit round trip: machines, params, samples (stereo + cue slices), names");
    CHECK(k.roles[1] == 3 && k.hasMaster && k.master[8] == 99, "roles and master FX");
    // rename: the samples folder moves along and the references follow
    const std::string np = moveItem(L.items[0], dir, err, "Kit B");
    CHECK(!np.empty() && pathExists(kitSamplesDir(np)) && !pathExists(kitSamplesDir(jp)), "rename moves the samples (%s)", err.c_str());
    scanLibrary(libKitsDir(), false, L, true); UserKit k2; CHECK(L.items.size() == 1 && loadKitItem(L.items[0], k2, &cache, err) && k2.t[2].smp && k2.t[2].smp->id == s1->id, "renamed kit loads: %s", err.c_str());
    CHECK(trashItem(L.items[0], err) && !pathExists(kitSamplesDir(np)), "kit to trash with its samples");
    // EXPORT KIT renders SAMPLE tracks (they were silent before 2.0)
    std::vector<uint8_t> z; CHECK(exportKitZip(*P, z, nullptr), "export kit");
    const std::vector<float> o = kitOneShot(P->tracks[5], P->bpm, 48000, P.get()); double pk = 0; for (float x : o) pk = std::max(pk, (double)std::fabs(x));
    CHECK(pk > 0.1, "kit one-shot of a SAMPLE track is not silent (peak %.3f)", pk);
  }
  // ===== H: WAV stereo + cue points, transient detection =====
  {
    std::shared_ptr<SampleData> s(new SampleData()); s->rate = 44100; s->fmt = SF_S24; s->ch = 2; s->name = "w";
    for (int i = 0; i < 10000; i++) { s->d.push_back((float)(i % 100) / 200); s->dR.push_back(-(float)(i % 77) / 200); } s->slices = { 0, 1234, 7000 }; finishSample(*s);
    const std::vector<uint8_t> w = encodeWav(*s); std::string err;
    SampleRef r = decodeWav("w", w.data(), w.size(), &err);
    CHECK(r && r->stereo() && r->d == s->d && r->dR == s->dR && r->slices == s->slices && r->id == s->id, "stereo WAV with cue points: round trip");
    // a break: 12 hits (decaying noise, levels 0.2-0.9) at known times over a -60 dB floor
    std::mt19937 g(3); std::uniform_real_distribution<double> U(-1, 1); const double rate = 44100;
    std::shared_ptr<SampleData> b(new SampleData()); b->rate = rate; b->fmt = SF_F32; b->d.assign((size_t)(rate * 3), 0.f);
    std::vector<int> on; int t = 1500;
    for (int k = 0; k < 12; k++) { on.push_back(t); const double a = 0.2 + 0.7 * ((k * 5) % 7) / 6.0, dec = rate * (0.03 + 0.05 * (k % 3));
      for (int i = 0; i < (int)(rate * 0.18) && t + i < (int)b->d.size(); i++) b->d[t + i] += (float)(a * U(g) * std::exp(-i / dec)); t += (int)(rate * (0.11 + 0.09 * ((k * 3) % 4))); }
    for (float& x : b->d) x += (float)(0.001 * U(g));
    finishSample(*b);
    for (double sens : { 50.0, 85.0 }) {
      const std::vector<int> det = detectSlices(*b, sens);
      int found = 0; double worstMs = 0; for (int o2 : on) { double best = 1e9; for (int d2 : det) best = std::min(best, std::fabs((double)(d2 - o2))); if (best < rate * 0.004) { found++; worstMs = std::max(worstMs, best / rate * 1000); } }
      int extra = 0; for (int d2 : det) { bool near = d2 == 0; for (int o2 : on) if (std::fabs((double)(d2 - o2)) < rate * 0.004) near = true; if (!near) extra++; }
      std::printf("  detect, sensitivity %.0f: %d of 12 hits within 4 ms (worst %.2f ms), %d extra points\n", sens, found, worstMs, extra);
      // sensitivity 50 may miss a hit that rises < 7 dB over the tail of a louder one (hits 5 and 9 rise 8.4 and 3.9 dB)
      CHECK(extra == 0 && found >= (sens < 60 ? 11 : 12), "detect at %.0f: %d found, %d extra", sens, found, extra);
    }
  }
  // ===== I: project keys =====
  {
    std::unique_ptr<Project> P(new Project()); defaultProject(*P);
    const std::string base = projectToString(*P);
    CHECK(base.find("\"seq\"") == std::string::npos && base.find("\"chain\"") == std::string::npos && base.find("\"song\"") == std::string::npos && base.find("\"outs\"") == std::string::npos, "unused 2.0 keys are not written");
    P->seq = ARR_SONG; P->chain = { 3, 1, 1 }; P->song.n = 2; P->song.rows[0].pat = 4; P->song.rows[0].reps = 7; P->song.rows[0].mute = 0x81; P->song.rows[1].pat = 2; P->song.rows[1].bpm = 99.5f; P->song.loop = false; P->song.start = 1;
    P->outs[3] = 1; P->outs[7] = 1;
    const std::string js = projectToString(*P); std::unique_ptr<Project> Q(new Project());
    CHECK(projectFromString(js, *Q) && projectToString(*Q) == js, "song / chain / outs round trip");
    CHECK(Q->seq == ARR_SONG && Q->chain.size() == 3 && Q->song.n == 2 && Q->song.rows[0].mute == 0x81 && Q->song.rows[1].bpm == 99.5f && !Q->song.loop && Q->song.start == 1 && Q->outs[3] && Q->outs[7] && !Q->outs[0], "values");
    const Arrangement a = Q->arrangement(); CHECK(a.mode == ARR_SONG && a.n == 2 && a.rows[0].reps == 7, "arrangement for the engine");
    Q->seq = ARR_CHAIN; const Arrangement c = Q->arrangement(); CHECK(c.mode == ARR_CHAIN && c.n == 3 && c.rows[0].pat == 3 && c.loop, "chain for the engine");
  }
  std::printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
  return fails ? 1 : 0;
}
