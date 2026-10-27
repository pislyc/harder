// End-to-end check of the sound-library path of the plugin.
// A: plugin path = UI loadPreset (same code as HarderUI.cpp) -> Core/Link -> host echo (DPF parameter-set echo on
//    the main thread + the host's own echo into the processor at the next block) -> run() order (drain, applyHost,
//    process).
// B: web path = the messages the web UI sends (machine + aud) straight into an engine.
// Browses all 111 presets on one track (like clicking through the library) and compares the track stem.
#include "../plugins/Harder/HarderShared.hpp"
#include <cstdio>
#include <vector>
#include <deque>
using namespace hd;

struct FakeUI {
  Core* core; int tr = 0;
  Project& S() { return *core->S; }
  void stripSrcLocks(int t) {
    for (int p = 0; p < NPAT; p++) for (int s = 0; s < NSTEP; s++) {
      Step& st = S().patterns[p].steps[t][s]; if (!st.on || !(st.l.mask & 0xFF)) continue;
      for (int i = 0; i < 8; i++) st.l.del(i); core->sendStep(p, t, s);
    }
  }
  std::vector<std::pair<int, float>> hostEchoQueue;   // values the host will send to the processor
  void setParameterValueFromUI(int idx, float v) { core->setHost(idx, v); hostEchoQueue.push_back({ idx, v }); }
  void notifyHostAll() { std::vector<int> ch; core->refreshHost(&ch); for (int idx : ch) setParameterValueFromUI(idx, core->hostVal[idx].load()); }
  void audition(int t) { core->sendAud(t, 110, nullptr); }
  void loadPreset(int pi) {
    TrackM& t = S().tracks[tr];
    const double k22 = t.p[22], k23 = t.p[23]; double p[NP]; presetParams(pi, p); p[22] = k22; p[23] = k23;
    const bool changed = t.m != PRESETS[pi].m;
    t.m = PRESETS[pi].m; std::memcpy(t.p, p, sizeof p); t.pre = PRESETS[pi].n;
    core->sendMachine(tr); if (changed) stripSrcLocks(tr);
    notifyHostAll(); audition(tr);
  }
};

int main(int argc, char** argv) {
  const double sr = 48000; const int B = 128, HOLD = (int)(sr * 1.2);
  const int tr = argc > 1 ? atoi(argv[1]) : 0;
  const bool echo = argc > 2 ? atoi(argv[2]) != 0 : true;
  // A
  std::unique_ptr<Core> core(new Core()); std::unique_ptr<Engine> A(new Engine()); core->eng = A.get();
  defaultProject(*core->S); core->refreshHost(nullptr, false); A->init(sr); core->sendAll(true); core->link.drainAll(*A);
  for (int i = 0; i < HP_COUNT; i++) core->markDirty(i); core->applyHost(*A);
  FakeUI ui; ui.core = core.get(); ui.tr = tr;
  // B: same start state, direct messages
  std::unique_ptr<Engine> Bq(new Engine()); Bq->init(sr);
  { const Project& P = *core->S; for (int t = 0; t < NT; t++) Bq->loadTrack(t, P.tracks[t].m, P.tracks[t].p); Bq->loadMaster(P.master);
    for (int i = 0; i < NPAT; i++) Bq->setPattern(i, P.patterns[i]); Bq->loadDone(P.cur, P.bpm, P.swing, P.mutes); }
  std::vector<float> sa(B), sb(B), L(B), R(B);
  int worst = -1; double worstD = 0; int nbad = 0;
  std::vector<double> rmsA(NPRESETS), rmsB(NPRESETS);
  for (int pi = 0; pi < NPRESETS; pi++) {
    { std::lock_guard<std::recursive_mutex> g(core->mtx); ui.loadPreset(pi); }
    // B gets what the web sends: machine(tr, m, p) then aud
    { double p[NP]; presetParams(pi, p); p[22] = core->S->tracks[tr].p[22]; p[23] = core->S->tracks[tr].p[23];
      Bq->setMachine(tr, PRESETS[pi].m, p); Bq->audition(tr, 110, nullptr); }
    double md = 0, ea = 0, eb = 0; bool first = true;
    for (int n = 0; n < HOLD; n += B) {
      core->link.drain(*A);
      if (echo && !first) { for (auto& e : ui.hostEchoQueue) core->setHost(e.first, e.second); ui.hostEchoQueue.clear(); }
      core->applyHost(*A); first = false;
      A->stems[tr] = sa.data(); A->process(L.data(), R.data(), B);
      Bq->stems[tr] = sb.data(); Bq->process(L.data(), R.data(), B);
      for (int i = 0; i < B; i++) { md = std::max(md, (double)std::fabs(sa[i] - sb[i])); ea += sa[i] * sa[i]; eb += sb[i] * sb[i]; }
      if (n % (B * 40) == 0) { std::lock_guard<std::recursive_mutex> g(core->mtx); core->pullHost(); }   // UI idle
    }
    rmsA[pi] = std::sqrt(ea / HOLD); rmsB[pi] = std::sqrt(eb / HOLD);
    // model must still hold the preset
    double p[NP]; presetParams(pi, p); p[22] = core->S->tracks[tr].p[22]; p[23] = core->S->tracks[tr].p[23];
    double mdm = 0, mde = 0; for (int i = 0; i < NP; i++) { mdm = std::max(mdm, std::fabs(core->S->tracks[tr].p[i] - p[i])); mde = std::max(mde, std::fabs(A->tracks[tr].p[i] - p[i])); }
    if (md > 1e-6 || mdm > 1e-6 || mde > 1e-6) { nbad++; if (nbad < 40) std::printf("%-14s m=%d  stem maxdiff %.3g  rmsA %.4f rmsB %.4f  model dev %.3g  engine dev %.3g\n", PRESETS[pi].n, PRESETS[pi].m, md, rmsA[pi], rmsB[pi], mdm, mde); }
    if (md > worstD) { worstD = md; worst = pi; }
  }
  std::printf("track %d echo %d: %d of %d presets differ; worst %s %.3g\n", tr, (int)echo, nbad, NPRESETS, worst >= 0 ? PRESETS[worst].n : "-", worstD);
  return nbad ? 1 : 0;
}
