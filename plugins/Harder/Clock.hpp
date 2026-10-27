// MIDI clock for the standalone program: following an external clock (24 ticks per quarter note, Start / Continue /
// Stop, Song Position Pointer) and sending one. Audio thread only; no allocation, no locks.
//
// Following: tick times (in engine samples) go through an alpha-beta tracker, a phase and a period estimate, so the
// sequencer gets a smooth position and tempo even though ticks arrive with the jitter of the MIDI driver and of the
// audio blocks. The result is a "host transport" (playing, position in quarter notes, BPM) for Engine::hostBlock,
// which already keeps the sequencer locked to a host the same way.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace hd {

struct ClockFollower {
  double sr = 48000;
  // tracker: tick number kRef was (estimated to be) at sample time tRef, one tick is T samples
  bool haveT = false, running = false, armed = false; double T = 0, tRef = 0, lastTick = -1; int64_t kRef = 0;
  int64_t startTick = 0;       // tick count at Start (0) or Continue (song position x 6)
  int64_t spp = 0;             // last song position pointer, in 16ths
  int nTicks = 0;              // ticks seen since the clock (re)started, for the tracker gains
  double lastSeen = -1e18;     // sample time of the last tick
  void reset(double rate) { *this = ClockFollower(); sr = rate; }
  double bpm() const { return haveT && T > 0 ? 60.0 * sr / (24.0 * T) : 0; }
  // one realtime message at sample time t
  void message(uint8_t st, uint8_t d1, uint8_t d2, double t) {
    switch (st) {
      case 0xFA: armed = true; running = false; startTick = 0; break;                 // Start: the next tick is beat 1
      case 0xFB: armed = true; running = false; startTick = spp * 6; break;           // Continue: from the song position
      case 0xFC: armed = false; running = false; break;                               // Stop
      case 0xF2: spp = (int64_t)d1 | ((int64_t)d2 << 7); if (running) { kRef = spp * 6; tRef = lastTick >= 0 ? lastTick : t; } break;
      case 0xF8: tick(t); break;
    }
  }
  void tick(double t) {
    // period: from the tick spacing, robust to a missing or a doubled tick
    if (lastTick >= 0) {
      const double dt = t - lastTick;
      if (!haveT) { if (dt > sr * 60.0 / (24 * 400) && dt < sr * 60.0 / (24 * 20)) { T = dt; haveT = true; } }
      else if (!running && dt > 0.5 * T && dt < 2 * T) T += 0.1 * (dt - T);   // stopped: tempo follows the clock loosely
    }
    if (t - lastTick > sr * 0.5) nTicks = 0;   // a pause: the tracker starts again
    lastTick = t; lastSeen = t;
    if (armed && haveT) { armed = false; running = true; kRef = startTick; tRef = t; nTicks = 1; return; }
    if (!running || !haveT) return;
    // alpha-beta: predicted time of this tick, then correct phase and period by part of the error
    const double pred = tRef + T, err = t - pred;
    if (std::fabs(err) > 4 * T) { kRef++; tRef = t; return; }   // a jump (lost ticks): re-anchor, keep the tempo
    nTicks++;
    const double a = nTicks < 24 ? 0.3 : 0.08, b = nTicks < 24 ? 0.05 : 0.004;
    kRef++; tRef = pred + a * err; T += b * err;
    if (T < sr * 60.0 / (24 * 400)) T = sr * 60.0 / (24 * 400);
  }
  // transport at sample time t (the start of a block)
  bool playing(double t) const { return running && haveT && t - lastSeen < sr * 1.0; }
  double ppq(double t) const { return ((double)kRef + (t - tRef) / T) / 24.0; }
};

// Clock out: ticks every quarter / 24 of the engine's tempo, free-running while stopped (receivers show the tempo);
// at play the phase restarts at the sequencer's first step, preceded by a Start.
struct ClockGen {
  double next = 0; bool wasPlaying = false, init = false;
  struct Msg { double t; uint8_t b; };
  // events in [t0, t1): calls emit(sampleTime, byte). stepLen = samples per 16th, origin = the sequencer's start time
  template <class F> void block(double t0, double t1, double stepLen, bool playing, double origin, F emit) {
    const double tk = stepLen / 6;
    if (!init) { init = true; next = t0; }
    if (playing && !wasPlaying) { emit(std::max(t0, origin - 1), 0xFA); next = origin; }
    else if (!playing && wasPlaying) emit(t0, 0xFC);
    wasPlaying = playing;
    if (!(tk > 1)) return;
    if (next < t0 - 8 * tk) next = t0;   // fell behind (device change, a long stall): continue from now
    int guard = 0;
    while (next < t1 && guard++ < 256) { if (next >= t0 - tk) emit(std::max(t0, next), 0xF8); next += tk; }
  }
};

} // namespace hd
