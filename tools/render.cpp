// Offline renderer for scenario files written by scen.js: same messages at the same block boundaries.
// usage: render <scenario.json> <out.f32> [block]
#include "../plugins/Harder/Convert.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cstdlib>
#include <xmmintrin.h>
#include <pmmintrin.h>
using namespace hd;

static std::string readFile(const char* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

static void applyMsg(Engine& E, const JVal& m) {
  const std::string& t = m["t"].str();
  if (t == "play") E.play();
  else if (t == "stop") E.msgStop();
  else if (t == "cue") E.msgCue((int)m["i"].num());
  else if (t == "fill") E.fill = m["v"].truthy();
  else if (t == "mute") E.msgMute((int)m["tr"].num(), m["v"].truthy());
  else if (t == "solo") E.solo = (int)m["tr"].num(-1);
  else if (t == "param") E.setParam((int)m["tr"].num(), (int)m["i"].num(), m["v"].num());
  else if (t == "bpm") E.msgBpm(m["v"].num());
  else if (t == "swing") E.msgSwing(m["v"].num());
  else if (t == "aud") { Locks L; locksFromJson(m["L"], L); E.audition((int)m["tr"].num(), m["vel"].num(110), L.any() ? &L : nullptr); }
  else if (t == "roll") { Locks L; locksFromJson(m["L"], L); E.roll((int)m["tr"].num(), m["on"].truthy(), m["st"].num(1), m["vel"].num(110), L.any() ? &L : nullptr); }
  else if (t == "machine") { const int mid = machineIndex(m["m"].str().c_str()); double p[NP]; paramsFromJson(m["p"], mid, p); E.setMachine((int)m["tr"].num(), mid, p); }
  else if (t == "trackp") { double p[NP]; paramsFromJson(m["p"], E.tracks[(int)m["tr"].num()].m, p); E.setTrackP((int)m["tr"].num(), p); }
  else if (t == "panic") E.panic();
  else std::fprintf(stderr, "unknown msg %s\n", t.c_str());
}

void loadInit(Engine& E, const JVal& st) {
  for (int tr = 0; tr < NT; tr++) {
    const JVal& t = st["tracks"][tr]; int mid = machineIndex(t["m"].str().c_str()); if (mid < 0) mid = 0;
    double p[NP]; paramsFromJson(t["p"], mid, p); E.loadTrack(tr, mid, p);
  }
  double m[NM]; defaultMaster(m); for (int i = 0; i < NM; i++) if (st["master"][i].isNum()) m[i] = st["master"][i].n; E.loadMaster(m);
  for (int i = 0; i < NPAT; i++) { Pattern P; patternFromJson(st["patterns"][i], P); E.setPattern(i, P); }
  uint8_t mu[NT] = { 0 }; for (int i = 0; i < NT; i++) mu[i] = st["mutes"][i].truthy();
  E.loadDone((int)st["cur"].num(), st["bpm"].num(125), st["swing"].num(50), mu);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: render scen.json out.f32 [block]\n"); return 1; }
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  JVal sc; if (!parseJson(readFile(argv[1]), sc)) { std::fprintf(stderr, "bad json\n"); return 1; }
  const double sr = sc["sr"].num(48000); const int n = (int)sc["n"].num(); const int block = (int)sc["block"].num(128);
  const int pblock = argc > 3 ? std::atoi(argv[3]) : block;
  Engine* E = new Engine(); E->init(sr); loadInit(*E, sc["init"]);
  std::vector<float> L(n), R(n), I(2 * (size_t)n);
  const JVal& msgs = sc["msgs"]; size_t mi = 0;
  auto t0 = std::chrono::steady_clock::now();
  double worst = 0; std::vector<std::pair<double,int>> bt;
  for (int pos = 0; pos < n; pos += pblock) {
    while (mi < msgs.size() && msgs[mi]["at"].num() <= pos) applyMsg(*E, msgs[mi++]["m"]);
    const int nn = std::min(pblock, n - pos);
#ifndef NO_BLOCK_TIMING
    timespec c0, c1; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c0);
    E->process(&L[pos], &R[pos], nn);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c1);
    const double us = (c1.tv_sec - c0.tv_sec) * 1e6 + (c1.tv_nsec - c0.tv_nsec) / 1e3;   // thread CPU time: excludes preemption
#else
    E->process(&L[pos], &R[pos], nn); const double us = 0;   // (per-block clock reads are syscalls under Wine)
#endif
    if (us > worst) worst = us; bt.push_back({us, pos});
  }
  std::sort(bt.begin(), bt.end()); const double p50 = bt[bt.size() / 2].first, p99 = bt[bt.size() * 99 / 100].first, p999 = bt[bt.size() * 999 / 1000].first;
  if (getenv("TOPBLK")) for (int k = 0; k < 5 && k < (int)bt.size(); k++) std::printf("  slow block %.0f us at %.3fs\n", bt[bt.size() - 1 - k].first, bt[bt.size() - 1 - k].second / sr);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  for (int i = 0; i < n; i++) { I[2 * i] = L[i]; I[2 * i + 1] = R[i]; }
  std::ofstream(argv[2], std::ios::binary).write((const char*)I.data(), I.size() * sizeof(float));
  const double audioMs = n / sr * 1000, blockMs = pblock / sr * 1000;
  std::printf("%-16s %6.1fs audio: %.2f%% of one core | block %d: median %.0f us, p99 %.0f us, p99.9 %.0f us, max %.0f us (budget %.0f us)\n",
              sc["name"].str().c_str(), n / sr, ms / audioMs * 100, pblock, p50, p99, p999, worst, blockMs * 1000);
  delete E; return 0;
}
