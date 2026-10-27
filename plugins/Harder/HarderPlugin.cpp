// HARDER — synth percussion & groove box. DSP side of the plugin.
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "HarderShared.hpp"
#include <chrono>
#include <cstring>
#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
#endif
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
# include <xmmintrin.h>
# define HARDER_HAVE_SSE 1
#endif

START_NAMESPACE_DISTRHO
using namespace hd;

static const char* const TP_NAMES[NTP] = { "Src 1", "Src 2", "Src 3", "Src 4", "Src 5", "Src 6", "Src 7", "Src 8",
  "Filter Freq", "Filter Reso", "Filter Env", "Amp Decay", "Drive", "Pan", "Volume", "Delay Send", "Reverb Send", "LFO Depth" };
static const char* const TP_SYM[NTP] = { "src1", "src2", "src3", "src4", "src5", "src6", "src7", "src8", "freq", "reso", "fenv", "adec", "drive", "pan", "vol", "dly", "rev", "lfodep" };
static const char* const MP_NAMES[NMP] = { "Delay Feedback", "Delay Return", "Reverb Decay", "Reverb Return", "Glue", "Master Drive", "DJ Filter", "Master Volume", "Duck Shape", "Duck Depth" };
static const char* const MP_SYM[NMP] = { "dlyfb", "dlyrtn", "revdec", "revrtn", "glue", "mdrive", "djf", "mvol", "duckshape", "duckdepth" };

class HarderPlugin : public Plugin {
public:
  Core core;
  Engine* eng;
  double sr = 0;
  bool wasHostPlaying = false;
  double loadAvg = 0;
  double outGain = 1;   // smoothed output level (audio thread)

  HarderPlugin() : Plugin(HP_COUNT, 0, 2), eng(new Engine()) {
    core.eng = eng;
    defaultProject(*core.S);
    if (harderIsStandalone()) {   // standalone: continue where the last session ended
      std::string s; std::unique_ptr<Project> p(new Project());
      if (readTextFile(harderConfigDir() + "autosave.json", s) && projectFromString(s, *p, &core.cache)) *core.S = *p;
      if (readTextFile(harderConfigDir() + "ui.json", s)) core.uiState = s;
    }
    core.applyUiStateDsp();
    core.refreshHost(nullptr, false);   // initial values: the host may set its own right away
    sr = getSampleRate() > 0 ? getSampleRate() : 48000;
    eng->init(sr);
    core.sendAll(true); core.link.drainAll(*eng);
    for (int i = 0; i < HP_COUNT; i++) core.markDirty(i);
    core.applyHost(*eng);
  }
  ~HarderPlugin() override { if (harderIsStandalone()) saveSession(); delete eng; }
  // standalone: the project and the UI settings are saved on exit (and every few seconds by the UI)
  void saveSession() {
    std::lock_guard<std::recursive_mutex> g(core.mtx);
    const int ec = eng->fb.cur.load(); if (ec >= 0 && ec < NPAT) core.S->cur = ec;
    writeFileAtomic(harderConfigDir() + "autosave.json", projectToString(*core.S));
    if (!core.uiState.empty()) writeFileAtomic(harderConfigDir() + "ui.json", core.uiState);
  }

protected:
  const char* getLabel() const override { return "HARDER"; }
  const char* getDescription() const override { return "Synth percussion & groove box: nine synth-drum machines and a sample player, Elektron-style sequencer with parameter and conditional locks."; }
  const char* getMaker() const override { return "HARDER"; }
  const char* getHomePage() const override { return "https://github.com/pislyc/harder"; }
  const char* getLicense() const override { return "GPL-3.0-or-later"; }
  uint32_t getVersion() const override { return d_version(1, 3, 1); }

  void initParameter(uint32_t index, Parameter& p) override {
    const HostParamInfo h = hostParamInfo((int)index);
    double mn, mx, df; hostRange((int)index, mn, mx, df);
    p.hints = kParameterIsAutomatable; p.ranges.min = (float)mn; p.ranges.max = (float)mx;
    char name[64], sym[64];
    switch (h.kind) {
      case 0: { const int k = (int)index % NTP; std::snprintf(name, sizeof name, "T%d %s", h.tr + 1, TP_NAMES[k]); std::snprintf(sym, sizeof sym, "t%d_%s", h.tr + 1, TP_SYM[k]); break; }
      case 1: { const int k = (int)index - HP_MASTER0; std::snprintf(name, sizeof name, "%s", MP_NAMES[k]); std::snprintf(sym, sizeof sym, "m_%s", MP_SYM[k]); break; }
      case 2: std::snprintf(name, sizeof name, "T%d Mute", h.tr + 1); std::snprintf(sym, sizeof sym, "t%d_mute", h.tr + 1); p.hints |= kParameterIsBoolean | kParameterIsInteger; break;
      case 3: std::snprintf(name, sizeof name, "Fill"); std::snprintf(sym, sizeof sym, "fill"); p.hints |= kParameterIsBoolean | kParameterIsInteger; break;
      default: std::snprintf(name, sizeof name, "Pattern"); std::snprintf(sym, sizeof sym, "pattern"); p.hints |= kParameterIsInteger; break;
    }
    p.name = name; p.shortName = name; p.symbol = sym;
    p.ranges.def = (float)core.hostVal[index].load();
    if (p.ranges.def < p.ranges.min || p.ranges.def > p.ranges.max) p.ranges.def = (float)df;
  }
  float getParameterValue(uint32_t index) const override { return index < (uint32_t)HP_COUNT ? core.hostVal[index].load(std::memory_order_relaxed) : 0.f; }
  void setParameterValue(uint32_t index, float value) override { core.setHost((int)index, value); }

  void initState(uint32_t index, State& s) override {
    if (index == 0) { s.key = "project"; s.label = "Project"; s.hints = kStateIsOnlyForDSP; s.defaultValue = ""; }
    else { s.key = "ui"; s.label = "UI"; s.hints = kStateIsOnlyForDSP; s.defaultValue = ""; }
  }
  String getState(const char* key) const override {
    HarderPlugin* self = const_cast<HarderPlugin*>(this);
    if (!std::strcmp(key, "ui")) { std::lock_guard<std::recursive_mutex> g(self->core.mtx); return String(self->core.uiState.c_str()); }
    if (std::strcmp(key, "project")) return String();
    std::lock_guard<std::recursive_mutex> g(self->core.mtx);
    self->core.pullHost();
    const int ec = eng->fb.cur.load(); if (ec >= 0 && ec < NPAT) self->core.S->cur = ec;
    return String(projectToString(*self->core.S).c_str());
  }
  void setState(const char* key, const char* value) override {
    if (!std::strcmp(key, "ui")) { std::lock_guard<std::recursive_mutex> g(core.mtx); core.uiState = value ? value : ""; core.applyUiStateDsp(); core.modelSeq++; return; }
    if (std::strcmp(key, "project") || !value || !*value) return;
    std::unique_ptr<Project> p(new Project());
    if (!projectFromString(value, *p, &core.cache)) return;   // keep the current project if the data is not a HARDER project
    std::lock_guard<std::recursive_mutex> g(core.mtx);
    *core.S = *p;
    core.pruneCache();
    core.refreshHost(nullptr);
    core.sendAll();
    for (int i = 0; i < HP_COUNT; i++) if (i != HP_FILL) core.markDirty(i);
    core.modelSeq++;
  }

  void activate() override {
    const double nsr = getSampleRate();
    if (nsr > 0 && nsr != sr) {   // the audio thread is not running here
      sr = nsr; eng->init(sr);
      std::lock_guard<std::recursive_mutex> g(core.mtx);
      core.sendAll(true); core.link.drainAll(*eng);
      for (int i = 0; i < HP_COUNT; i++) if (i != HP_FILL) core.markDirty(i);
      core.applyHost(*eng);
    }
  }
  void deactivate() override { eng->panic(); }

  void handleMidi(const MidiEvent& ev) { if (ev.size >= 2 && ev.size <= 4) handleMidiBytes(ev.data[0], ev.data[1] & 0x7F, ev.size > 2 ? (ev.data[2] & 0x7F) : 0); }
  void handleMidiBytes(const uint8_t s0, const uint8_t d1, const uint8_t d2) {
    const uint8_t st = s0 & 0xF0;
    if (core.midiMapped.load(std::memory_order_relaxed)) {   // standalone: mapping from LAB > CONTROL
      if (st == 0x90 && d2 > 0) { const int t = core.padMap[s0 & 15][d1].load(std::memory_order_relaxed); if (t > 0 && t <= NT) eng->audition(t - 1, d2, nullptr); }
      else if (st == 0xC0) { if (d1 < NPAT) eng->msgCue(d1); return; }
      else if (st == 0xB0 && (d1 == 120 || d1 == 123)) { eng->panic(); return; }
      if (st == 0x80 || st == 0x90 || st == 0xB0) { Core::MidiMsg m; m.st = s0; m.d1 = d1; m.d2 = d2; core.midiPush(m); }
      return;
    }
    if (st == 0x90 && d2 > 0) {
      if (d1 >= 36 && d1 < 36 + NT) eng->audition(d1 - 36, d2, nullptr);   // C1.. = tracks 1-8 (as the web MIDI export)
      else if (d1 >= 24 && d1 < 24 + NPAT) eng->msgCue(d1 - 24);           // C0..G0 = patterns 1-8
    } else if (st == 0xC0) { if (d1 < NPAT) eng->msgCue(d1); }
    else if (st == 0xB0 && (d1 == 120 || d1 == 123)) eng->panic();
  }

  void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t nmidi) override {
    const auto t0 = std::chrono::steady_clock::now();
#ifdef HARDER_HAVE_SSE
    const unsigned csr = _mm_getcsr(); _mm_setcsr(csr | 0x8040);   // flush denormals to zero (FTZ | DAZ)
#endif
    core.link.drain(*eng);   // never blocks: if the UI holds the link, commands wait one block
    core.applyHost(*eng);
    const TimePosition& tp = getTimePosition();
    double bpm = tp.bbt.valid ? tp.bbt.beatsPerMinute : 0;
    if (!(bpm >= 20 && bpm <= 999)) bpm = 0;
    double ppq = 0;
    if (tp.bbt.valid && tp.bbt.beatsPerBar > 0) {
      const int ppqPerBar = (int)tp.bbt.beatsPerBar * 4 / (int)(tp.bbt.beatType > 0 ? tp.bbt.beatType : 4);
      ppq = (tp.bbt.bar - 1) * (double)ppqPerBar + ((tp.bbt.beat - 1) + tp.bbt.tick / tp.bbt.ticksPerBeat) * ppqPerBar / tp.bbt.beatsPerBar;
    } else if (bpm > 0) ppq = (double)tp.frame / sr * bpm / 60.0;
    const bool hp = tp.playing && bpm > 0;
    eng->hostBlock(hp, ppq, bpm);
    core.hostPlaying.store(hp, std::memory_order_relaxed); core.hostSynced.store(eng->hostSync, std::memory_order_relaxed); core.hostBpm.store(bpm, std::memory_order_relaxed);

    float* L = outputs[0]; float* R = outputs[1]; uint32_t pos = 0;
#ifdef HARDER_TEST_MIDI_INJECT
    { Core::MidiMsg m; while (core.injectPop(m)) handleMidiBytes(m.st, m.d1 & 0x7F, m.d2 & 0x7F); }
#endif
    for (uint32_t k = 0; k < nmidi; k++) {
      const uint32_t f = std::min(midi[k].frame, frames);
      if (f > pos) { eng->process(L + pos, R + pos, (int)(f - pos)); pos = f; }
      handleMidi(midi[k]);
    }
    if (pos < frames) eng->process(L + pos, R + pos, (int)(frames - pos));
    // output level (after everything, ~15 ms smoothing so moving the fader never clicks)
    { const float od = core.outDb.load(std::memory_order_relaxed); const double target = od <= -80 ? 0.0 : mpow(10.0, od / 20.0);
      if (outGain != target || target != 1.0) {
        const double k = 1 - mexp(-1.0 / (0.015 * sr));
        for (uint32_t i = 0; i < frames; i++) { outGain += (target - outGain) * k; L[i] = (float)(L[i] * outGain); R[i] = (float)(R[i] * outGain); }
        if (std::fabs(outGain - target) < 1e-6) outGain = target;
      } }
#ifdef HARDER_HAVE_SSE
    _mm_setcsr(csr);
#endif
    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double load = frames > 0 ? used / (frames / sr) : 0;
    const double a = std::min(1.0, frames / (0.5 * sr));   // ~0.5 s average, like a DAW CPU meter
    loadAvg += (load - loadAvg) * a;
    core.cpuPermille.store((uint32_t)std::min(9999.0, loadAvg * 1000), std::memory_order_relaxed);
  }

  DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarderPlugin)
};

Plugin* createPlugin() { return new HarderPlugin(); }

END_NAMESPACE_DISTRHO

namespace hd {
Core* harderCore(void* p) { return p ? &static_cast<DISTRHO_NAMESPACE::HarderPlugin*>(p)->core : nullptr; }
bool harderIsStandalone() { return std::strstr(DISTRHO_NAMESPACE::getPluginFormatName(), "Standalone") != nullptr; }
std::string harderConfigDir() { const char* d = DISTRHO_NAMESPACE::getSpecialDir(DISTRHO_NAMESPACE::kSpecialDirDocumentsForPlugin); return d ? d : ""; }
} // namespace hd

// Parameter value text for the host (Live shows these in the device view and automation lanes).
bool dpf_param_text_hook(void* instance, uint32_t index, float value, char* buf, size_t size) {
  using namespace hd;
  Core* c = harderCore(instance); if (!c || index >= (uint32_t)HP_COUNT) return false;
  const HostParamInfo h = hostParamInfo((int)index); std::string s;
  if (h.kind == 0) {
    if (h.i < 8) { const int mid = c->machineOf[h.tr].load(); const PDef& d = T().mach[mid][h.i]; s = std::string(d.n) + " " + formatValue(d, srcToEngine(mid, h.i, value)); }
    else s = formatValue(T().common[h.i], value);
  } else if (h.kind == 1) s = formatValue(T().master[h.i], value);
  else if (h.kind == 2) s = value > 0.5f ? "Muted" : "On";
  else if (h.kind == 3) s = value > 0.5f ? "On" : "Off";
  else { char b[8]; std::snprintf(b, sizeof b, "P%d", (int)jsround(value)); s = b; }
  std::snprintf(buf, size, "%s", s.c_str()); return true;
}
