// Host-sync position test: starting Live at bar N must produce the same trigs (time, track, velocity) as
// running from the song start and looking from bar N on. Probability locks are removed (they draw from the RNG).
#include "../plugins/Harder/Model.hpp"
#include <cstdio>
#include <vector>
#include <tuple>
#include <algorithm>
using namespace hd;
static std::vector<std::tuple<long, int, int>>* sink = nullptr;
static long offset = 0;
static void cb(int tr, double t, double vel) { if (sink) sink->push_back({ (long)t - offset, tr, (int)jsround(vel * 1000) }); }
static void load(Engine& E, const Project& P) { for (int t = 0; t < NT; t++) E.loadTrack(t, P.tracks[t].m, P.tracks[t].p); E.loadMaster(P.master); for (int i = 0; i < NPAT; i++) E.setPattern(i, P.patterns[i]); E.loadDone(P.cur, P.bpm, P.swing, P.mutes); }
int main() {
  std::unique_ptr<Project> P(new Project()); defaultProject(*P);
  for (auto& pat : P->patterns) for (auto& row : pat.steps) for (auto& st : row) { st.l.del(42); st.l.del(50); }
  for (int t = 0; t < NT; t++) P->tracks[t].p[42] = 100;
  // include an odd-length polymeter track and a slow track to exercise the per-track phase maths
  P->patterns[0].len[3] = 13; P->patterns[0].scl[5] = 1; P->patterns[0].len[5] = 7; P->patterns[0].scl[2] = 5;
  const double bpm = 128, sr = 48000; const int B = 128; int fails = 0;
  for (int startBar : { 0, 1, 3, 7, 12 }) {
    for (double frac : { 0.0, 0.37 }) {
      const double startPpq = startBar * 4 + frac;
      std::unique_ptr<Engine> A(new Engine()), C(new Engine()); A->init(sr); C->init(sr); load(*A, *P); load(*C, *P);
      std::vector<std::tuple<long, int, int>> ta, tc; std::vector<float> L(B), R(B);
      // A: Live started at bar 1 and played through
      A->dbgTrig = cb; sink = &ta; offset = 0; double ppq = 0;
      const long total = (long)((startPpq + 32) * 60 / bpm * sr) + B * 4;
      for (long pos = 0; pos < total; pos += B) { A->hostBlock(true, ppq, bpm); A->process(L.data(), R.data(), B); ppq += B / sr * bpm / 60; }
      // C: Live started directly at startPpq
      C->dbgTrig = cb; sink = &tc; const long startSample = (long)std::llround(startPpq * 60 / bpm * sr);
      offset = -startSample; ppq = startPpq;
      for (long pos = startSample; pos < total; pos += B) { C->hostBlock(true, ppq, bpm); C->process(L.data(), R.data(), B); ppq += B / sr * bpm / 60; }
      sink = nullptr;
      // compare trigs from the first full step after the start
      const long from = (long)std::ceil((std::ceil(startPpq * 4 - 1e-9) / 4) * 60 / bpm * sr) + 2, to = total - B * 8;
      std::vector<std::tuple<long, int, int>> a, c;
      for (auto& x : ta) if (std::get<0>(x) >= from && std::get<0>(x) < to) a.push_back(x);
      for (auto& x : tc) if (std::get<0>(x) >= from && std::get<0>(x) < to) c.push_back(x);
      std::sort(a.begin(), a.end()); std::sort(c.begin(), c.end());
      long maxDt = 0; bool same = a.size() == c.size();
      for (size_t i = 0; same && i < a.size(); i++) { if (std::get<1>(a[i]) != std::get<1>(c[i]) || std::get<2>(a[i]) != std::get<2>(c[i])) same = false; maxDt = std::max(maxDt, std::labs(std::get<0>(a[i]) - std::get<0>(c[i]))); }
      if (!same || maxDt > 1) fails++;
      std::printf("start at ppq %5.2f: %3zu trigs vs %3zu, %s, max timing difference %ld samples\n", startPpq, a.size(), c.size(), same ? "same tracks and velocities" : "DIFFERENT", maxDt);
    }
  }
  std::printf("%s\n", fails ? "SYNC TEST FAILED" : "SYNC TEST PASSED");
  return fails ? 1 : 0;
}
