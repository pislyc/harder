// CPU stress tests for the engine: worst cases, automation storms, long-run counters, idle cost.
// Reports thread CPU time per block (excludes preemption), so spikes are the engine's own.
#include "../plugins/Harder/Model.hpp"
#include "../plugins/Harder/HostParams.hpp"
#include <ctime>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <xmmintrin.h>
#include <pmmintrin.h>
using namespace hd;

static double cpuUs() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
struct Stats { double avgPct, p99, worst, budget; bool nan; double rms; };
template <class F> Stats run(Engine& E, double seconds, int block, F perBlock) {
  const int n = (int)(E.sr * seconds); std::vector<float> L(block), R(block); std::vector<double> bt; bt.reserve(n / block + 1);
  double tot = 0, ss = 0; bool nan = false; long cnt = 0;
  for (int pos = 0; pos < n; pos += block) {
    perBlock(pos);
    const double a = cpuUs(); E.process(L.data(), R.data(), block); const double b = cpuUs() - a;
    bt.push_back(b); tot += b;
    for (int i = 0; i < block; i++) { if (L[i] != L[i] || R[i] != R[i] || std::fabs(L[i]) > 1) nan = true; ss += (double)L[i] * L[i]; cnt++; }
  }
  std::sort(bt.begin(), bt.end());
  const double budget = block / E.sr * 1e6;
  return { tot / (n / E.sr * 1e6) * 100, bt[bt.size() * 99 / 100], bt.back(), budget, nan, 10 * std::log10(ss / cnt + 1e-30) };
}
static void print(const char* name, const Stats& s) {
  std::printf("%-46s avg %5.2f%% core | p99 %6.0f us  max %6.0f us  of %5.0f us budget | %s rms %.1f dB\n", name, s.avgPct, s.p99, s.worst, s.budget, s.nan ? "NaN/OVER!" : "clean", s.rms);
}
static void loadProject(Engine& E, const Project& P) {
  for (int t = 0; t < NT; t++) E.loadTrack(t, P.tracks[t].m, P.tracks[t].p);
  E.loadMaster(P.master); for (int i = 0; i < NPAT; i++) E.setPattern(i, P.patterns[i]); E.loadDone(P.cur, P.bpm, P.swing, P.mutes);
}

int main() {
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  std::unique_ptr<Project> P(new Project()); std::unique_ptr<Engine> E(new Engine());
  const double SRS[] = { 48000, 96000 };
  for (double sr : SRS) {
    std::printf("---- %.0f Hz ----\n", sr);
    // 1. default project playing
    defaultProject(*P); E->init(sr); loadProject(*E, *P); E->play();
    print("default project, playing, block 64", run(*E, 20, 64, [](int) {}));
    // 2. idle (stopped, after tails): should approach zero
    E->msgStop(); run(*E, 8, 256, [](int) {});
    print("stopped after tails (idle), block 256", run(*E, 10, 256, [](int) {}));
    // 3. worst case: every track the heaviest settings, 1/64 retrigs over 16 steps at 2x speed, 300 BPM, all FX
    defaultProject(*P);
    for (int t = 0; t < NT; t++) {
      TrackM& tk = P->tracks[t]; tk.m = (t % 2) ? M_MTL : M_MDL; defaultTrackParams(tk.m, tk.p);
      double* p = tk.p; p[1] = 127; p[8] = 60; p[9] = 90; p[10] = 64; p[11] = 50; p[19] = 100; p[18] = 126; p[24] = 127; p[25] = 127;
      p[26] = 9; p[27] = 40; p[28] = 127; p[30] = 120; p[33] = 63; p[34] = 8; p[36] = 0; p[32] = 13; p[23] = 100;
    }
    for (int i = 0; i < NM; i++) P->master[i] = T().master[i].valid() ? T().master[i].def : 0;
    P->master[1] = 120; P->master[9] = 127; P->master[16] = 127; P->master[17] = 100; P->master[18] = -30; P->master[24] = 1; P->master[25] = 0; P->master[27] = 127; P->master[28] = 60;
    Pattern& Q = P->patterns[0]; emptyPattern(Q, 300, 50); Q.mlen = 64;
    for (int t = 0; t < NTR; t++) { Q.len[t] = 64; Q.scl[t] = 6; for (int s = 0; s < 64; s++) { Step& st = Q.steps[t][s]; st.on = 1; if (t < 8) { st.l.set(45, 4); st.l.set(47, 5); st.l.set(46, -30); st.l.set(48, 1); st.l.set(0, (s * 37) % 128); st.l.set(8, (s * 53) % 128); } else st.l.set(18, (s % 2) ? 40 : -40); } }
    P->cur = 0; P->bpm = 300;
    E->init(sr); loadProject(*E, *P); E->play();
    print("WORST CASE: 8 heavy voices, 1/64 retrig, all FX", run(*E, 20, 64, [](int) {}));
    // 4. automation storm on top: 40 parameter changes every 32-sample block (host automation at audio rate)
    E->init(sr); loadProject(*E, *P); E->play();
    print("WORST CASE + 40 automation changes / 32 samples", run(*E, 10, 32, [&](int pos) {
      for (int k = 0; k < 40; k++) { const int t = k % 9; const int i = t == 8 ? MP_IDX[k % NMP] : TP_IDX[k % NTP]; E->setParam(t, i, 20 + (pos / 32 + k) % 100); }
    }));
    // 5. master FX automation only (worst for masterCtl: reverb size/decay changing every block)
    defaultProject(*P); E->init(sr); loadProject(*E, *P); E->play();
    print("default + reverb size/decay automated per block", run(*E, 10, 64, [&](int pos) { E->setParam(8, 8, (pos / 64) % 128); E->setParam(8, 9, (pos / 64 * 7) % 128); }));
    // 6. long-run: start the clock beyond 2^32 samples (~25 h at 48 kHz) and check the limiter / scheduler
    defaultProject(*P); E->init(sr); E->time = ((int64_t)1 << 32) + 12345; E->laN = ((int64_t)1 << 32); loadProject(*E, *P); E->play();
    print("after 25 h of uptime (64-bit counters)", run(*E, 10, 128, [](int) {}));
  }
  // 7. host sync: Live tempo ramp + loop jumps, verify the scheduler stays locked (drift report)
  {
    defaultProject(*P); E->init(48000); loadProject(*E, *P);
    double ppq = 0, bpm = 120, maxErr = 0; int relocs = 0; const int B = 256; std::vector<float> L(B), R(B);
    for (int blk = 0; blk < 48000 * 60 / B; blk++) {
      bpm = 120 + 40 * std::sin(blk * 0.0005);               // slow tempo automation 80..160 BPM
      if (blk > 0 && blk % 3000 == 0) { ppq = 16; relocs++; } // Live loop brace jumps back to bar 5
      const bool wasSync = E->hostSync; const double before = E->mIndexNext;
      E->hostBlock(true, ppq, bpm);
      if (wasSync && E->mIndexNext == before) { const double expected = (double)E->time + (E->mIndexNext - ppq * 4) * E->stepLen; maxErr = std::max(maxErr, std::fabs(expected - E->mNext)); }
      E->process(L.data(), R.data(), B);
      ppq += B / 48000.0 * bpm / 60.0;
    }
    std::printf("host sync: 60 s with 80-160 BPM tempo automation and %d loop jumps: residual drift %.2f samples (<= 0.25 is corrected each block)\n", relocs, maxErr);
  }
  return 0;
}
