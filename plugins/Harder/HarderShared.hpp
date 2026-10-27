// Shared between the DSP side (HarderPlugin.cpp) and the UI (HarderUI.cpp).
//
// Threads:
//   audio thread  owns Engine. Takes nothing that can block: it only *tries* the command spin-lock,
//                 reads atomics, and writes Engine::Feedback atomics.
//   main thread   owns the Project model (under `mtx`), pushes commands, renders previews.
// Host parameters are a mirror of selected model values held in atomics; setParameterValue may be
// called from either thread, so it only stores and flags.
#pragma once
#include "Model.hpp"
#include "HostParams.hpp"
#include "FileSys.hpp"
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>
#include <string>

namespace hd {

struct PlenData { int mlen; int len[NTR]; int scl[NTR]; };
struct Cmd {
  enum T : uint8_t { PARAM, MACHINE, TRACKP, STEP, PATTERN, PLEN, BPM, SWING, PLAY, STOP, CUE, FILL, MUTE, SOLO, AUD, ROLL, PANIC, MASTER, LOADDONE, SAMPLE };
  uint8_t t = 0; int8_t tr = 0; int16_t i = 0; int s = 0; double v = 0, v2 = 0; bool flag = false; const SampleData* ptr = nullptr;
  union U { U() {} double p[NP]; Step st; Locks L; PlenData pl; struct { double m[NM]; } ms; struct { int cur; double bpm, swing; uint8_t mutes[NT]; } ld; } u;
};

struct Link {
  std::atomic<bool> busy{ false };
  std::vector<Cmd> q;
  std::unique_ptr<Pattern[]> pat{ new Pattern[NPAT] };   // latest copy of each pattern sent with Cmd::PATTERN
  std::atomic<uint32_t> dropped{ 0 };
  bool overflow = false;
  // command counters: pushed (main thread, under the lock) and consumed (applied, skipped as a duplicate pattern, or
  // discarded on overflow). A sample the engine no longer points to may be freed once consumed passed its command.
  uint64_t pushed = 0; std::atomic<uint64_t> consumed{ 0 };
  Link() { q.reserve(8192); }
  void lock() { while (busy.exchange(true, std::memory_order_acquire)) std::this_thread::yield(); }
  void unlock() { busy.store(false, std::memory_order_release); }
  // main thread
  // returns the command's sequence number (consumed > it means the audio thread is past it), UINT64_MAX if the queue
  // was full and the command dropped (syncAllIfOverflow resends everything)
  uint64_t push(const Cmd& c) { lock(); uint64_t n = UINT64_MAX; if (q.size() < 8000) { q.push_back(c); n = pushed++; } else overflow = true; unlock(); return n; }
  void pushPattern(int i, const Pattern& P) { lock(); pat[i] = P; Cmd c; c.t = Cmd::PATTERN; c.i = (int16_t)i; if (q.size() < 8000) { q.push_back(c); pushed++; } else overflow = true; unlock(); }
  // audio thread: never waits. Returns true if something was applied.
  // A pattern copy is 267 KB, so per block at most 2 are applied and at most MAX_CMDS commands (the rest wait for
  // the next block, order kept), and a
  // pattern sent several times is only copied at its last occurrence (the staging copy is the newest).
  static constexpr int MAX_PATTERN_COPIES = 2, MAX_CMDS = 384;   // per audio block: bursts are spread out
  bool drain(Engine& E) {
    if (busy.exchange(true, std::memory_order_acquire)) return false;
    const bool any = !q.empty();
    int last[NPAT]; for (int& l : last) l = -1;
    for (int k = 0; k < (int)q.size(); k++) if (q[k].t == Cmd::PATTERN) last[q[k].i] = k;
    int copies = 0, k = 0;
    for (; k < (int)q.size(); k++) {
      const Cmd& c = q[k];
      if (k >= MAX_CMDS) break;
      if (c.t == Cmd::PATTERN) { if (last[c.i] != k) continue; if (copies == MAX_PATTERN_COPIES) break; copies++; }
      apply(E, c);
    }
    consumed.fetch_add((uint64_t)k, std::memory_order_release);
    if (k >= (int)q.size()) q.clear();     // keeps capacity: no deallocation on the audio thread
    else q.erase(q.begin(), q.begin() + k);
    busy.store(false, std::memory_order_release);
    return any;
  }
  // non-realtime contexts only (constructor, activate): apply everything now
  void drainAll(Engine& E) { for (int k = 0; k < 64; k++) { lock(); const bool empty = q.empty(); unlock(); if (empty) break; while (!drain(E)) std::this_thread::yield(); } }
  void apply(Engine& E, const Cmd& c) {
    switch (c.t) {
      case Cmd::PARAM: E.setParam(c.tr, c.i, c.v); break;
      case Cmd::MACHINE: E.setMachine(c.tr, c.i, c.u.p); break;
      case Cmd::TRACKP: E.setTrackP(c.tr, c.u.p); break;
      case Cmd::STEP: E.setStep(c.i, c.tr, c.s, c.u.st); break;
      case Cmd::PATTERN: E.setPattern(c.i, pat[c.i]); break;
      case Cmd::PLEN: E.setPlen(c.i, c.u.pl.mlen, c.u.pl.len, c.u.pl.scl); break;
      case Cmd::BPM: E.msgBpm(c.v); break;
      case Cmd::SWING: E.msgSwing(c.v); break;
      case Cmd::PLAY: if (!E.hostSync) E.internalPlay(); break;
      case Cmd::STOP: if (!E.hostSync) E.msgStop(); else for (Voice& v : E.voices) v.choke(); break;
      case Cmd::CUE: E.msgCue(c.i); break;
      case Cmd::FILL: E.fill = c.flag; break;
      case Cmd::MUTE: E.msgMute(c.tr, c.flag); break;
      case Cmd::SOLO: E.solo = c.tr; break;
      case Cmd::AUD: E.audition(c.tr, c.v, c.flag ? &c.u.L : nullptr); break;
      case Cmd::ROLL: E.roll(c.tr, c.flag, c.v, c.v2, c.s ? &c.u.L : nullptr); break;
      case Cmd::PANIC: E.panic(); break;
      case Cmd::MASTER: E.loadMaster(c.u.ms.m); E.masterDirty = true; break;
      case Cmd::LOADDONE: E.loadDone(c.u.ld.cur, c.u.ld.bpm, c.u.ld.swing, c.u.ld.mutes); break;
      case Cmd::SAMPLE: E.setSample(c.i, c.ptr); break;
    }
  }
};

struct Core {
  std::recursive_mutex mtx;                 // model lock: main thread only
  std::unique_ptr<Project> S{ new Project() };
  Link link;
  Engine* eng = nullptr;                    // audio-owned; UI may read eng->fb only
  std::atomic<float> hostVal[HP_COUNT];
  std::atomic<uint64_t> dirty[(HP_COUNT + 63) / 64];
  std::atomic<bool> hostPlaying{ false }, hostSynced{ false }; std::atomic<double> hostBpm{ 0 };
  std::atomic<int> machineOf[NT];           // for parameter text on any thread
  std::atomic<uint32_t> modelSeq{ 0 };      // bumps when the model changes outside the UI (state load)
  std::atomic<uint32_t> cpuPermille{ 0 };   // audio-thread load estimate (0.1 %)
  std::string uiState;                      // UI-only state (theme, drawer, scale, output level) persisted in the project
  SampleCache cache;                        // every sample seen this session (undo snapshots refer to samples by id)
  SampleRef engSmp[NSLOT];                  // what the audio engine points to (main thread)
  struct Retired { SampleRef r; uint64_t seq; }; std::vector<Retired> retired;   // freed once the engine let go
  // ---------- standalone MIDI controller (Control.hpp) ----------
  // The audio thread plays pads itself (padMap: note -> track + 1, per channel) and hands every note and CC to the UI
  // through a single-producer / single-consumer ring (the UI maps them to buttons and knobs, and learns new ones).
  struct MidiMsg { uint8_t st = 0, d1 = 0, d2 = 0; };
  static constexpr uint32_t MQN = 1024;
  MidiMsg mq[MQN]; std::atomic<uint32_t> mqW{ 0 }, mqR{ 0 };
  bool midiPush(const MidiMsg& m) { const uint32_t w = mqW.load(std::memory_order_relaxed); if (w - mqR.load(std::memory_order_acquire) >= MQN) return false; mq[w % MQN] = m; mqW.store(w + 1, std::memory_order_release); return true; }
  bool midiPop(MidiMsg& m) { const uint32_t r = mqR.load(std::memory_order_relaxed); if (r == mqW.load(std::memory_order_acquire)) return false; m = mq[r % MQN]; mqR.store(r + 1, std::memory_order_release); return true; }
  std::atomic<uint8_t> padMap[16][128];
  std::atomic<bool> midiMapped{ false };   // the standalone UI owns the MIDI mapping (else the plugin's fixed notes apply)
#ifdef HARDER_TEST_MIDI_INJECT
  MidiMsg iq[MQN]; std::atomic<uint32_t> iqW{ 0 }, iqR{ 0 };   // test builds: UI -> audio, treated like MIDI input
  bool injectPush(const MidiMsg& m) { const uint32_t w = iqW.load(std::memory_order_relaxed); if (w - iqR.load(std::memory_order_acquire) >= MQN) return false; iq[w % MQN] = m; iqW.store(w + 1, std::memory_order_release); return true; }
  bool injectPop(MidiMsg& m) { const uint32_t r = iqR.load(std::memory_order_relaxed); if (r == iqW.load(std::memory_order_acquire)) return false; m = iq[r % MQN]; iqR.store(r + 1, std::memory_order_release); return true; }
#endif
  // output level: after the limiter, like a hardware MAIN knob; not part of the project or of exports (dB, <= -80 = off)
  std::atomic<float> outDb{ 0 };
  void applyUiStateDsp() { JVal j; if (parseJson(uiState, j) && j.isObj()) { const double d = j["outDb"].num(0); outDb.store((float)(d < -80 ? -80 : d > 0 ? 0 : d)); } }

  Core() { for (auto& h : hostVal) h.store(0); for (auto& d : dirty) d.store(0); for (auto& m : machineOf) m.store(0); for (auto& o : ownUntil) o.store(0); for (auto& c : padMap) for (auto& n : c) n.store(0); }

  // ---------- host parameter mirror ----------
  double hostFromModel(int idx) const {
    const HostParamInfo h = hostParamInfo(idx); const Project& P = *S;
    switch (h.kind) {
      case 0: return h.i < 8 ? srcToHost(P.tracks[h.tr].m, h.i, P.tracks[h.tr].p[h.i]) : P.tracks[h.tr].p[h.i];
      case 1: return P.master[h.i];
      case 2: return P.mutes[h.tr] ? 1 : 0;
      case 3: return hostVal[idx].load();   // fill is momentary, not model state
      default: return P.cur + 1;
    }
  }
  void markDirty(int idx) { dirty[idx >> 6].fetch_or((uint64_t)1 << (idx & 63), std::memory_order_acq_rel); }

  // ---------- who owns a host value ----------
  // When the plugin itself changes an exposed value (sound or kit loaded, knob turned, project loaded) the model is
  // the truth and the host is only being told. Hosts can send old values back while that is in flight: Ableton
  // Live sends the processor the value it last knew when a gesture begins, and calls setParamNormalized with
  // values that are a few updates old. Taking those as automation reverted a freshly loaded sound to the previous
  // sound's values. So for OWN_MS after the plugin set a value, a host value that disagrees with it is ignored
  // (the wrapper then reports the plugin's value back to the host); values equal to the current one are echoes
  // and change nothing. After OWN_MS host automation is applied as usual.
  static constexpr int64_t OWN_MS = 1000;
  std::atomic<int64_t> ownUntil[HP_COUNT];
  static int64_t clockMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  static float tolerance(int idx) { double mn, mx, df; hostRange(idx, mn, mx, df); return (float)((mx - mn) * 1e-5); }
  // the plugin sets idx to v (main thread)
  void own(int idx, float v) { ownUntil[idx].store(clockMs() + OWN_MS, std::memory_order_relaxed); hostVal[idx].store(v, std::memory_order_relaxed); }
  // called by setParameterValue (any thread). Returns false if the value was refused.
  bool setHost(int idx, float v) {
    if (idx < 0 || idx >= HP_COUNT || !std::isfinite(v)) return false;
    if (std::fabs(v - hostVal[idx].load(std::memory_order_relaxed)) <= tolerance(idx)) return true;   // echo
    if (clockMs() < ownUntil[idx].load(std::memory_order_relaxed)) return false;                        // stale
    hostVal[idx].store(v, std::memory_order_relaxed); markDirty(idx); return true;
  }
  // main thread: recompute mirror from the model; returns indices that changed (caller informs host)
  void refreshHost(std::vector<int>* changed, bool arm = true) {
    for (int i = 0; i < NT; i++) machineOf[i].store(S->tracks[i].m);
    for (int idx = 0; idx < HP_COUNT; idx++) {
      if (idx == HP_FILL) continue;
      const float v = (float)hostFromModel(idx);
      if (std::fabs(hostVal[idx].load() - v) > 1e-4f) { if (arm) own(idx, v); else hostVal[idx].store(v); if (changed) changed->push_back(idx); }
    }
  }
  // main thread: host automation -> model (so the UI and the saved project see it)
  bool pullHost() {
    bool ch = false; Project& P = *S;
    for (int idx = 0; idx < HP_COUNT; idx++) {
      const HostParamInfo h = hostParamInfo(idx); const double hv = hostVal[idx].load();
      if (h.kind == 0) {
        double& m = P.tracks[h.tr].p[h.i];
        const double v = h.i < 8 ? srcToEngine(P.tracks[h.tr].m, h.i, hv) : hv;
        if (std::fabs(v - m) > 1e-3) { m = h.i < 8 || std::fabs(v - jsround(v * 10) / 10) > 1e-4 ? v : jsround(v * 10) / 10; ch = true; }
      } else if (h.kind == 1) { double& m = P.master[h.i]; if (std::fabs(hv - m) > 1e-3) { m = hv; ch = true; } }
      else if (h.kind == 2) { const uint8_t mv = hv > 0.5f; if (P.mutes[h.tr] != mv) { P.mutes[h.tr] = mv; ch = true; } }
    }
    return ch;
  }
  // audio thread: apply flagged host values to the engine
  void applyHost(Engine& E) {
    for (int w = 0; w < (int)(sizeof dirty / sizeof dirty[0]); w++) {
      uint64_t bits = dirty[w].exchange(0, std::memory_order_acq_rel);
      while (bits) {
        const int b = __builtin_ctzll(bits); bits &= bits - 1; const int idx = w * 64 + b; if (idx >= HP_COUNT) break;
        const HostParamInfo h = hostParamInfo(idx); const double hv = hostVal[idx].load(std::memory_order_relaxed);
        switch (h.kind) {
          case 0: { const double v = h.i < 8 ? srcToEngine(E.tracks[h.tr].m, h.i, hv) : hv; if (std::fabs(E.tracks[h.tr].p[h.i] - v) > 1e-4) E.setParam(h.tr, h.i, v); break; }
          case 1: if (std::fabs(E.master[h.i] - hv) > 1e-4) E.setParam(8, h.i, hv); break;
          case 2: E.msgMute(h.tr, hv > 0.5); break;
          case 3: E.fill = hv > 0.5; break;
          case 4: { const int p = (int)jsround(hv) - 1; if (p >= 0 && p < NPAT && p != (E.cue >= 0 ? E.cue : E.cur)) E.msgCue(p); break; }
        }
      }
    }
  }

  // ---------- main-thread commands (model must already hold the new values) ----------
  void sendParam(int tr, int i, double v) { Cmd c; c.t = Cmd::PARAM; c.tr = (int8_t)tr; c.i = (int16_t)i; c.v = v; link.push(c); }
  void sendMachine(int tr) { Cmd c; c.t = Cmd::MACHINE; c.tr = (int8_t)tr; c.i = (int16_t)S->tracks[tr].m; std::memcpy(c.u.p, S->tracks[tr].p, sizeof c.u.p); link.push(c); machineOf[tr].store(S->tracks[tr].m); }
  void sendTrackP(int tr) { Cmd c; c.t = Cmd::TRACKP; c.tr = (int8_t)tr; std::memcpy(c.u.p, S->tracks[tr].p, sizeof c.u.p); link.push(c); }
  void sendStep(int pat, int tr, int s) { sent[pat].steps[tr][s] = S->patterns[pat].steps[tr][s]; Cmd c; c.t = Cmd::STEP; c.i = (int16_t)pat; c.tr = (int8_t)tr; c.s = s; c.u.st = S->patterns[pat].steps[tr][s]; link.push(c); }
  std::unique_ptr<Pattern[]> sent{ new Pattern[NPAT] };   // what the engine was last given (main thread)
  static bool sameLocks(const Locks& a, const Locks& b) { if (a.mask != b.mask) return false; for (int i = 0; i < NP; i++) if (a.has(i) && a.v[i] != b.v[i]) return false; return true; }
  static bool samePattern(const Pattern& a, const Pattern& b) {
    if (a.mlen != b.mlen || a.bpm != b.bpm || a.swing != b.swing) return false;
    for (int t = 0; t < NTR; t++) {
      if (a.len[t] != b.len[t] || a.scl[t] != b.scl[t]) return false;
      for (int s = 0; s < NSTEP; s++) { const Step& x = a.steps[t][s]; const Step& y = b.steps[t][s]; if (x.on != y.on || (x.on && (x.tl != y.tl || !sameLocks(x.l, y.l)))) return false; }
    }
    return true;
  }
  void sendPattern(int pat) { link.pushPattern(pat, S->patterns[pat]); sent[pat] = S->patterns[pat]; }
  void sendPlen(int pat) { const Pattern& P = S->patterns[pat]; sent[pat].mlen = P.mlen; std::memcpy(sent[pat].len, P.len, sizeof P.len); std::memcpy(sent[pat].scl, P.scl, sizeof P.scl); Cmd c; c.t = Cmd::PLEN; c.i = (int16_t)pat; c.u.pl.mlen = P.mlen; std::memcpy(c.u.pl.len, P.len, sizeof P.len); std::memcpy(c.u.pl.scl, P.scl, sizeof P.scl); link.push(c); }
  void sendSimple(uint8_t t, int tr = 0, int i = 0, double v = 0, bool flag = false) { Cmd c; c.t = t; c.tr = (int8_t)tr; c.i = (int16_t)i; c.v = v; c.flag = flag; link.push(c); }
  void sendAud(int tr, double vel, const Locks* L) { Cmd c; c.t = Cmd::AUD; c.tr = (int8_t)tr; c.v = vel; c.flag = L && L->any(); if (c.flag) c.u.L = *L; link.push(c); }
  void sendRoll(int tr, bool on, double st, double vel, const Locks* L) { Cmd c; c.t = Cmd::ROLL; c.tr = (int8_t)tr; c.flag = on; c.v = st; c.v2 = vel; c.s = L && L->any(); if (c.s) c.u.L = *L; link.push(c); }
  void sendMaster() { Cmd c; c.t = Cmd::MASTER; std::memcpy(c.u.ms.m, S->master, sizeof S->master); link.push(c); }
  // sample slots that differ from what the engine has (force: all). The old data stays alive until the engine has
  // applied the change: the audio thread may be reading it until then.
  void sendSamples(bool force = false) {
    for (int i = 0; i < NSLOT; i++) {
      if (!force && engSmp[i] == S->smp[i]) continue;
      if (S->smp[i]) cache.add(S->smp[i]);
      Cmd c; c.t = Cmd::SAMPLE; c.i = (int16_t)i; c.ptr = S->smp[i].get();
      const uint64_t seq = link.push(c);
      if (engSmp[i]) retired.push_back({ engSmp[i], seq });
      engSmp[i] = S->smp[i];
    }
    gcSamples();
  }
  void gcSamples() {
    const uint64_t done = link.consumed.load(std::memory_order_acquire);
    retired.erase(std::remove_if(retired.begin(), retired.end(), [done](const Retired& r) { return r.seq < done; }), retired.end());
  }
  // whole project, e.g. after a state load (force: also patterns the engine already has, after an engine re-init).
  // Unchanged patterns are skipped so the audio thread copies as little as possible.
  void sendAll(bool force = false) {
    sendSamples(force);   // before the tracks: a voice may trigger a slot right after its MACHINE command
    for (int tr = 0; tr < NT; tr++) sendMachine(tr);
    sendMaster();
    for (int i = 0; i < NPAT; i++) if (force || !samePattern(sent[i], S->patterns[i])) sendPattern(i);
    Cmd c; c.t = Cmd::LOADDONE; c.u.ld.cur = S->cur; c.u.ld.bpm = S->bpm; c.u.ld.swing = S->swing; std::memcpy(c.u.ld.mutes, S->mutes, NT); link.push(c);
  }
  void syncAllIfOverflow() {
    bool ov; link.lock(); ov = link.overflow; if (ov) { link.consumed.fetch_add(link.q.size()); link.q.clear(); link.overflow = false; } link.unlock();
    if (!ov) return;
    // discarded SAMPLE commands never reached the engine: it may still point to any retired sample. Keep all of them
    // until the full resend below has been applied.
    for (Retired& r : retired) r.seq = UINT64_MAX;
    for (int i = 0; i < NSLOT; i++) if (engSmp[i]) retired.push_back({ engSmp[i], UINT64_MAX });
    sendAll(true);
    link.lock(); const uint64_t last = link.pushed; link.unlock();
    for (Retired& r : retired) if (r.seq == UINT64_MAX) r.seq = last - 1;   // the last resend command
  }
};

Core* harderCore(void* pluginInstance);   // implemented in HarderPlugin.cpp

// ---------- standalone: files in the user's folder (Documents/HARDER on Windows) ----------
bool harderIsStandalone();                     // implemented in HarderPlugin.cpp (DPF plugin format name)
std::string harderConfigDir();                 // with trailing separator; created if missing
// file helpers: FileSys.hpp

} // namespace hd
