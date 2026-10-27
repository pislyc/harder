// Sound library under a Live-like VST3 host.
//
// Models the DPF VST3 wrapper (parameter cache, processor input queues: first point at offset 0 before run,
// last point after run, controller setParamNormalized) and the behaviour Ableton Live is reported to have
// (forum.juce.com/t/60565): at the start of a gesture it sends the processor the value it last believed in,
// and it calls setParamNormalized on the controller with stale values while edits are in flight.
// The UI side is HarderUI's loadPreset / notifyHostAll; the plugin side is the real Core + Engine.
// Compares the sounding track with an engine that received only the web app's messages, and checks that the
// model, the engine and the host all end with the preset's values.
//
// build: g++ -std=gnu++17 -O2 -pthread -I<dir with HarderShared.hpp> tools/host_echo_test.cpp libhmath.a
// run:   host_echo_test <dpfPatched 0|1>
#include "HarderShared.hpp"
#include <cstdio>
#include <vector>
#include <map>
#include <thread>
using namespace hd;

struct Point { int idx; float v; int offset; };
struct DPFSim;
struct LiveSim {
  float known[HP_COUNT];                       // what Live believes each parameter is
  float prev[HP_COUNT];                        // the value before that
  std::vector<Point> nextBlock;                // input parameter changes for the next process()
  std::vector<std::pair<int, float>> ctrlLater;   // stale setParamNormalized calls (delivered on the main thread)
  void beginEdit(int idx) { nextBlock.push_back({ idx, known[idx], 0 }); }          // stale value at gesture start
  void performEdit(int idx, float v) { prev[idx] = known[idx]; known[idx] = v; nextBlock.push_back({ idx, v, 0 }); ctrlLater.push_back({ idx, prev[idx] }); }
  void endEdit(int) {}
  void outputChange(int idx, float v) { prev[idx] = known[idx]; known[idx] = v; }   // processor reported a value
};
struct DPFSim {
  Core* core; LiveSim* host; bool patched; float cache[HP_COUNT]; bool outFlag[HP_COUNT] = { false };
  float tolN(int idx, float a, float b) { double mn, mx, df; hostRange(idx, mn, mx, df); return std::fabs(a - b) / (float)(mx - mn); }
  void readback(int idx, float v) {   // DPF patch: the plugin may keep its own value; then the host is told
    if (!patched) return; const float a = core->hostVal[idx].load(); if (a != v) { cache[idx] = a; outFlag[idx] = true; }
  }
  // UI -> controller messages
  void uiEdit(int idx, bool started) { if (started) host->beginEdit(idx); else host->endEdit(idx); }
  void uiSet(int idx, float v) { cache[idx] = v; core->setHost(idx, v); readback(idx, v); host->performEdit(idx, v); }
  // processor: _setNormalizedPluginParameterValue
  void procSet(int idx, float v) { if (tolN(idx, cache[idx], v) < 1e-7f) return; cache[idx] = v; core->setHost(idx, v); readback(idx, v); }
  // controller: setParamNormalized. With DISTRHO_PLUGIN_WANT_DIRECT_ACCESS the controller is not separate and DPF
  // ignores plugin parameters here (DistrhoPluginVST3.cpp setParameterNormalized), so stale calls cannot reach the plugin.
  void ctrlSet(int, float) {}
  void process(Engine& E, std::vector<float>& L, std::vector<float>& R, int n) {
    std::map<int, std::vector<Point>> q; for (auto& p : host->nextBlock) q[p.idx].push_back(p); host->nextBlock.clear();
    for (auto& kv : q) { const Point& f = kv.second.front(); if (f.offset == 0) procSet(kv.first, f.v); }
    core->link.drain(E); core->applyHost(E); E.process(L.data(), R.data(), n);   // HarderPlugin::run
    for (auto& kv : q) {
      const Point& l = kv.second.back(); const bool apply = patched ? (kv.second.size() > 1 || l.offset != 0) : l.offset != 0;
      if (apply) procSet(kv.first, l.v);
    }
    for (int i = 0; i < HP_COUNT; i++) if (outFlag[i]) { outFlag[i] = false; host->outputChange(i, core->hostVal[i].load()); }
  }
  void mainIdle() { auto later = host->ctrlLater; host->ctrlLater.clear(); for (auto& c : later) ctrlSet(c.first, c.second); }
};

int main(int argc, char** argv) {
  const bool patched = argc > 1 && atoi(argv[1]);
  const double sr = 48000; const int B = 256, HOLD = (int)(sr * 1.0); const int tr = 0;
  std::unique_ptr<Core> core(new Core()); std::unique_ptr<Engine> A(new Engine()); core->eng = A.get();
  defaultProject(*core->S); core->refreshHost(nullptr, false); A->init(sr); core->sendAll(true); core->link.drainAll(*A);
  for (int i = 0; i < HP_COUNT; i++) core->markDirty(i); core->applyHost(*A);
  std::unique_ptr<LiveSim> live(new LiveSim()); std::unique_ptr<DPFSim> dpf(new DPFSim());
  for (int i = 0; i < HP_COUNT; i++) { live->known[i] = live->prev[i] = dpf->cache[i] = core->hostVal[i].load(); }
  dpf->core = core.get(); dpf->host = live.get(); dpf->patched = patched;
  std::unique_ptr<Engine> W(new Engine()); W->init(sr);   // web message stream
  { const Project& P = *core->S; for (int t = 0; t < NT; t++) W->loadTrack(t, P.tracks[t].m, P.tracks[t].p); W->loadMaster(P.master);
    for (int i = 0; i < NPAT; i++) W->setPattern(i, P.patterns[i]); W->loadDone(P.cur, P.bpm, P.swing, P.mutes); }
  std::vector<float> sa(B), sw(B), L(B), R(B), L2(B), R2(B);
  int badSound = 0, badModel = 0, badHost = 0; double worst = 0;
  for (int pi = 0; pi < NPRESETS; pi++) {
    double want[NP];
    {   // HarderUI::loadPreset + notifyHostAll (UI thread)
      std::lock_guard<std::recursive_mutex> g(core->mtx); TrackM& t = core->S->tracks[tr];
      double p[NP]; presetParams(pi, p); p[22] = t.p[22]; p[23] = t.p[23]; std::memcpy(want, p, sizeof p);
      const bool changed = t.m != PRESETS[pi].m; t.m = PRESETS[pi].m; std::memcpy(t.p, p, sizeof p); t.pre = PRESETS[pi].n;
      core->sendMachine(tr);
      if (changed) for (int q = 0; q < NPAT; q++) for (int s = 0; s < NSTEP; s++) { Step& st = core->S->patterns[q].steps[tr][s]; if (!st.on || !(st.l.mask & 0xFF)) continue; for (int i = 0; i < 8; i++) st.l.del(i); core->sendStep(q, tr, s); }
      std::vector<int> ch; core->refreshHost(&ch);
      for (int idx : ch) { dpf->uiEdit(idx, true); dpf->uiSet(idx, core->hostVal[idx].load()); dpf->uiEdit(idx, false); }
      core->sendAud(tr, 110, nullptr);
    }
    W->setMachine(tr, PRESETS[pi].m, want); W->audition(tr, 110, nullptr);
    double md = 0, pk = 0;
    for (int n = 0; n < HOLD; n += B) {
      A->stems[tr] = sa.data(); dpf->process(*A, L, R, B);
      W->stems[tr] = sw.data(); W->process(L2.data(), R2.data(), B);
      for (int i = 0; i < B; i++) { md = std::max(md, (double)std::fabs(sa[i] - sw[i])); pk = std::max(pk, (double)std::fabs(sw[i])); }
      if ((n / B) % 4 == 1) { dpf->mainIdle(); std::lock_guard<std::recursive_mutex> g(core->mtx); core->pullHost(); }   // UI idle
    }
    double dm = 0, de = 0; for (int i = 0; i < NP; i++) { dm = std::max(dm, std::fabs(core->S->tracks[tr].p[i] - want[i])); de = std::max(de, std::fabs(A->tracks[tr].p[i] - want[i])); }
    double dh = 0; for (int k = 0; k < NTP; k++) { const int idx = tr * NTP + k; dh = std::max(dh, (double)std::fabs(live->known[idx] - core->hostVal[idx].load())); dh = std::max(dh, (double)std::fabs(dpf->cache[idx] - core->hostVal[idx].load())); }
    const bool bs = md > 1e-6 * std::max(1.0, pk), bm = dm > 1e-3 || de > 1e-3, bh = dh > 1e-3;
    badSound += bs; badModel += bm; badHost += bh; worst = std::max(worst, md);
    if ((bs || bm || bh) && badSound + badModel + badHost <= 12)
      std::printf("  %-14s sound diff %.3g (peak %.2f)  model dev %.3g  engine dev %.3g  Live vs plugin %.3g\n", PRESETS[pi].n, md, pk, dm, de, dh);
  }
  std::printf("DPF %s: %d/%d presets sound different from the web, %d with wrong model/engine values, %d where Live or the wrapper cache shows other values; worst diff %.3g\n",
              patched ? "patched" : "as shipped", badSound, NPRESETS, badModel, badHost, worst);
  // genuine automation from the host must still work once the plugin is not editing that parameter
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const int idx = tr * NTP + 14; const float target = 30;   // T1 Volume (engine index 21)
  live->known[idx] = target; live->nextBlock.push_back({ idx, target, 0 }); dpf->process(*A, L, R, B);
  { std::lock_guard<std::recursive_mutex> g(core->mtx); core->pullHost(); }
  const bool autoOk = std::fabs(A->tracks[tr].p[21] - target) < 1e-3 && std::fabs(core->S->tracks[tr].p[21] - target) < 1e-3;
  std::printf("host automation after the edit: %s (engine %.2f, model %.2f, want %.2f)\n", autoOk ? "applied" : "NOT APPLIED", A->tracks[tr].p[21], core->S->tracks[tr].p[21], target);
  return (badSound || badModel || badHost || !autoOk) ? 1 : 0;
}
