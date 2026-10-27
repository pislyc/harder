// HARDER DSP engine. Line-by-line port of engine.js (StrobeCore.createEngine) so the plugin renders
// the same sound as the web groovebox. Differences from the JS version are deliberate and listed here:
//  * no allocation after construction: events live in a fixed pool, retrigs are one re-arming event
//    (same firing order as the JS queue, which inserted every hit up front)
//  * limiter deque indices are 64-bit (the JS Int32Array index wraps after ~12 h at 48 kHz)
//  * host transport: relocate() / warp() keep the sequencer locked to the DAW's beat position
//  * master FX controls are recomputed at most once per 16-sample control tick (automation storms)
//  * an idle detector skips the FX chain once no voice sounds and the delay/reverb tails are below
//    -140 dBFS for a full delay-buffer length (output is then exact zeros instead of sub -140 dB noise)
//  * NaN / blow-up guards reset the offending voice or the FX chain instead of latching silence
#pragma once
#include "Params.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <atomic>
#include <algorithm>
#include <limits>

namespace hd {

constexpr double TAU = 6.283185307179586, PI = 3.141592653589793;
inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double exp2j(double x) { return mexp(x * 0.6931471805599453); }
inline double dk(double sec, double sr) { return mexp(-6.907755 / (std::max(sec, 1e-5) * sr)); }
inline double sat(double x) { if (x > 3) return 1; if (x < -3) return -1; const double x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
inline double jsmod1(double x) { return std::fmod(x, 1.0); }  // JS x % 1 for x >= 0
inline double wrap01(double x) { return x >= 1 ? x - 1 : x; }   // == fmod(x, 1) exactly for x in [0, 2) (Sterbenz)

struct Rng {
  uint32_t s = 1;
  void seed(uint32_t v) { s = v ? v : 1; }
  double operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 2147483648.0 - 1; }
};

struct SVF {
  double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.4, lp = 0, bp = 0, hp = 0;
  void set(double fc, double q, double sr) {
    fc = clampd(fc, 8, sr * 0.45); const double g = mtan(PI * fc / sr); k = 1 / q;
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  double run(double v0) {
    const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
    lp = v2; bp = v1; hp = v0 - k * v1 - v2; return v2;
  }
};
inline double blep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

// ---------------- locks / steps / patterns ----------------
struct Locks {
  uint64_t mask = 0;
  double v[NP];
  bool has(int i) const { return (mask >> i) & 1u; }
  void set(int i, double x) { mask |= (uint64_t)1 << i; v[i] = x; }
  void del(int i) { mask &= ~((uint64_t)1 << i); }
  void clear() { mask = 0; }
  bool any() const { return mask != 0; }
};
struct Step { uint8_t on = 0, tl = 0; Locks l; };
struct Pattern {
  int mlen = 16; int len[NTR]; int scl[NTR]; double bpm = 125, swing = 50;
  Step steps[NTR][NSTEP];
  Pattern() { for (int t = 0; t < NTR; t++) { len[t] = 16; scl[t] = 4; } }
  // in-place reset: never build a Pattern temporary (267 KB) on the stack, Windows threads have 1 MB or less
  void reset() { mlen = 16; bpm = 125; swing = 50; for (int t = 0; t < NTR; t++) { len[t] = 16; scl[t] = 4; } clearSteps(); }
  void clearSteps() { for (int t = 0; t < NTR; t++) for (int s = 0; s < NSTEP; s++) { steps[t][s].on = 0; steps[t][s].tl = 0; steps[t][s].l.mask = 0; } }
};

struct Slide { int n = 0; uint8_t i[NP]; double a[NP], b[NP]; double dur = 0, t0 = 0; bool hasT0 = false; };

// ---------------- machines ----------------
static inline int32_t imul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline int32_t byteBeat(int f, int32_t t) {
  switch (f) {
    case 0: return imul(t, (((t >> 12) | (t >> 8)) & 63 & (t >> 4)));
    case 1: return (imul(t, 5) & (t >> 7)) | (imul(t, 3) & (t >> 10));
    case 2: return imul(t, ((t >> 11) & (t >> 8) & 123 & (t >> 3)));
    default: return (int32_t)((uint32_t)imul(((t >> 6) | t | (t >> ((t >> 16) & 15))), 10) + (uint32_t)((t >> 11) & 7));
  }
}

struct MSub {
  double ph = 0, pe = 0, pk = 0, ae = 0, ak = 0, ce = 0, ck = 0, f0 = 50, semis = 28, fold = 0, harm = 0, click = 0, imp = 0; SVF bp;
  void trig() { ph = 0; pe = 1; ae = 1; ce = 1; imp = 1; }
  void ctl(const double* e, double pm, double sr) {
    f0 = expm(e[0], 25, 160) * pm; semis = e[1] / 127 * 60; pk = dk(expm(e[2], 0.002, 0.4), sr); ak = dk(expm(e[3], 0.04, 4), sr);
    click = mpow(e[4] / 127, 1.5); const double ct = expm(e[5], 300, 9000); bp.set(ct, 0.9, sr); ck = dk(0.012 - e[5] / 127 * 0.009, sr);
    fold = e[6] / 127; harm = e[7] / 127;
  }
  double tick(double sr, Rng& rnd) {
    const double f = std::min(f0 * exp2j(semis * pe / 12), sr * 0.45); pe *= pk;
    ph += f / sr; if (ph >= 1) ph -= 1;
    double s = msin(TAU * ph);
    if (fold > 0) s += (msin(s * (1 + fold * 5) * PI * 0.5) - s) * std::min(1.0, fold * 8);
    if (harm > 0) s = s * (1 - harm * 0.35) + harm * 0.55 * msin(2 * TAU * ph + 0.6);
    double o = s * ae; ae *= ak;
    if (ce > 1e-4) { bp.run(rnd()); o += click * (bp.bp * bp.k * 1.6 * ce + imp * 0.7); ce *= ck; imp = 0; }
    return o;
  }
  bool alive() const { return ae > 1e-4 || ce > 1e-4; }
};
struct MFmk {
  double cph = 0, mph = 0, mprev = 0, pe = 0, ae = 0, ie = 0, f0 = 50, semis = 30, pk = 0, ak = 0, ik = 0, ratio = 1.5, idx = 2, fb = 0;
  void trig() { cph = 0; mph = 0; mprev = 0; pe = 1; ae = 1; ie = 1; }
  void ctl(const double* e, double pm, double sr) {
    f0 = expm(e[0], 25, 160) * pm; semis = e[1] / 127 * 60; pk = dk(expm(e[2], 0.002, 0.4), sr); ak = dk(expm(e[3], 0.04, 4), sr);
    ratio = 0.5 + e[4] / 127 * 7.5; idx = mpow(e[5] / 127, 2) * 12; ik = dk(expm(e[6], 0.004, 1.5), sr); fb = e[7] / 127 * 1.4;
  }
  double tick(double sr) {
    const double f = std::min(f0 * exp2j(semis * pe / 12), sr * 0.4); pe *= pk;
    mph += f * ratio / sr; mph -= std::floor(mph);
    const double m = msin(TAU * mph + fb * mprev); mprev = m;
    cph += f / sr; if (cph >= 1) cph -= 1;
    const double o = msin(TAU * cph + idx * ie * m) * ae; ae *= ak; ie *= ik; return o;
  }
  bool alive() const { return ae > 1e-4; }
};
struct MSnr {
  double p1 = 0, p2 = 0, te = 0, ne = 0, se = 0, pe = 0, f0 = 180, r = 1.7, tk = 0, nk = 0, sk = 0, pk = 0, mix = 0.6, snap = 0.5, semis = 6, prev = 0; SVF nf;
  void trig() { p1 = 0; p2 = 0.25; te = 1; ne = 1; se = 1; pe = 1; }
  void ctl(const double* e, double pm, double sr) {
    f0 = expm(e[0], 80, 600) * pm; r = 1.1 + e[1] / 127 * 1.7; tk = dk(expm(e[2], 0.02, 1), sr); nk = dk(expm(e[3], 0.02, 1.2), sr);
    mix = e[4] / 127; nf.set(expm(e[5], 800, 12000), 0.8, sr); snap = e[6] / 127; sk = dk(0.008, sr); semis = e[7] / 127 * 24; pk = dk(0.04, sr);
  }
  double tick(double sr, Rng& rnd) {
    const double fm = exp2j(semis * pe / 12); pe *= pk;
    p1 += f0 * fm / sr; if (p1 >= 1) p1 -= 1; p2 += f0 * r * fm / sr; p2 -= std::floor(p2);
    const double tone = msin(TAU * p1) * 0.62 + msin(TAU * p2) * 0.42;
    const double n = rnd(); nf.run(n); const double nz = nf.hp * 0.55 + nf.bp * nf.k * 0.8;
    const double o = tone * te * (1 - mix * 0.7) + nz * ne * mix * 1.15 + snap * (n - prev) * se * 0.6;
    prev = n; te *= tk; ne *= nk; se *= sk; return o * 1.41;
  }
  bool alive() const { return te > 1e-4 || ne > 1e-4; }
};
struct MClp {
  int32_t times[8] = { 0 }; int n = 0, k = 0, cnt = 4, cntW = 4; double be = 0, te = 0, bk = 0, tk = 0, ring = 0, rf = 400, rph = 0, space = 0.008, sctr = 0; SVF bp;
  void trig(double sr, Rng& rnd) {
    n = 0; k = 0; be = 0; te = 0; cnt = cntW; double t = 0;
    for (int i = 0; i < cnt; i++) { times[i] = (int32_t)jsround(t * sr); t += space * (1 + sctr * (rnd() * 0.9)); }
  }
  void ctl(const double* e, double pm, double sr) {
    bp.set(expm(e[0], 400, 8000) * pm, 0.7 + mpow(e[1] / 127, 2) * 9, sr);
    cntW = 1 + (int)jsround(e[2] / 127 * 7);
    space = expm(e[3], 0.002, 0.04); bk = dk(clampd(space * 2.5, 0.006, 0.06), sr); tk = dk(expm(e[4], 0.03, 2), sr);
    sctr = e[5] / 127; ring = e[6] / 127; rf = expm(e[7], 50, 4000) * pm;
  }
  double tick(double sr, Rng& rnd) {
    if (k < cnt && n >= times[k]) { be = 1; k++; if (k == cnt) te = 1; }
    n++;
    double s = rnd();
    if (ring > 0) { rph += rf / sr; rph -= std::floor(rph); s += (msin(TAU * rph + 1.6 * s) - s) * ring; }
    bp.run(s);
    const double o = bp.bp * bp.k * (be + te * 0.55) * 3.3; be *= bk; te *= tk; return o;
  }
  bool alive() const { return k < cnt || be > 1e-4 || te > 1e-4; }
};
struct MMtl {
  double ph[6] = { 0 }, fr[6] = { 0 }, o[6] = { 0 }; double ae = 0, ak = 0, be = 0, bk = 0, noise = 0, ring = 0, bite = 0; SVF bp;
  void trig() { ae = 1; be = 1; for (int i = 0; i < 6; i++) ph[i] = jsmod1(i * 0.173); }
  void ctl(const double* e, double pm, double sr) {
    static const double R[6] = { 1, 1.4462, 1.7779, 2.5460, 2.6303, 3.8968 };
    const double base = expm(e[0], 100, 900) * pm, sp = e[4] / 63.5;
    for (int i = 0; i < 6; i++) fr[i] = std::min(base * (1 + (R[i] - 1) * sp), sr * 0.45);
    ak = dk(expm(e[1], 0.01, 2.5), sr); bp.set(expm(e[2], 1500, 16000), 0.7 + mpow(e[3] / 127, 2) * 12, sr);
    noise = e[5] / 127; ring = e[6] / 127; bite = e[7] / 127; bk = dk(0.003, sr);
  }
  double tick(double sr, Rng& rnd) {
    double sum = 0;
    for (int i = 0; i < 6; i++) {
      const double dt = fr[i] / sr; double p = ph[i] + dt; if (p >= 1) p -= 1; ph[i] = p;
      double v = p < 0.5 ? 1 : -1; v += blep(p, dt); v -= blep(wrap01(p + 0.5), dt); o[i] = v; sum += v;
    }
    double src = sum / 6 * (1 - ring) * 1.6 + (o[0] * o[3] + o[1] * o[4] + o[2] * o[5]) / 3 * ring * 1.6;
    const double n = rnd(); src += n * (noise * 0.9 + bite * be * 1.8);
    bp.run(src);
    const double y = (bp.hp * 0.55 + bp.bp * bp.k * 0.7) * ae * 1.8; ae *= ak; be *= bk; return y;
  }
  bool alive() const { return ae > 1e-4; }
};
struct MMdl {
  double y1[4] = { 0 }, y2[4] = { 0 }, c1[4] = { 0 }, c2[4] = { 0 }, b[4] = { 0 };
  double imp = 0, ne = 0, nk = 0, lp = 0, lpc = 1, norm = 1, pe = 0, pk = 0, semis = 0, est = 0, r0 = 0;
  void trig() { imp = 1; ne = 1; pe = 1; est = 1; }
  void ctl(const double* e, double pm, double sr) {
    static const double S[5][4] = { { 1, 1.594, 2.136, 2.296 }, { 1, 2.756, 5.404, 8.933 }, { 1, 2.32, 4.25, 6.63 }, { 1, 1.41, 2.21, 3.73 }, { 1, 2, 3, 4 } };
    semis = e[6] / 127 * 24; pk = dk(0.08, sr);
    const double f0 = expm(e[0], 40, 2400) * pm * exp2j(semis * pe / 12), decay = expm(e[1], 0.02, 4);
    const double m = e[2] / 127 * 4; const int i0 = std::min(3, (int)std::floor(m)); const double fr = m - i0, damp = e[3] / 127;
    const double fc = expm(e[4], 300, 16000); lpc = 1 - mexp(-TAU * fc / sr);
    const double w0 = TAU * std::min(f0, sr * 0.45) / sr;
    norm = mhypot(1 - (1 - lpc) * mcos(w0), (1 - lpc) * msin(w0)) / lpc;
    const double pos = 0.04 + e[5] / 127 * 0.46; nk = dk(expm(e[7], 0.0005, 0.03), sr);
    for (int k = 0; k < 4; k++) {
      const double ratio = S[i0][k] + (S[i0 + 1][k] - S[i0][k]) * fr, f = f0 * ratio;
      if (f > sr * 0.45) { c1[k] = 0; c2[k] = 0; b[k] = 0; continue; }
      const double w = TAU * f / sr, Tt = decay * mpow(1 / ratio, damp * 1.5), r = mexp(-6.9078 / (Tt * sr));
      c1[k] = 2 * r * mcos(w); c2[k] = r * r;
      b[k] = msin(w) * (0.35 + 0.65 * std::fabs(msin(PI * (k + 1) * pos))) / std::sqrt(k + 1.0);
      if (k == 0) r0 = r;
    }
  }
  double tick(Rng& rnd) {
    pe *= pk;
    const double ex = imp + rnd() * ne * 0.5; imp = 0; ne *= nk;
    lp += lpc * (ex - lp); const double x = lp * norm;
    double o = 0;
    for (int k = 0; k < 4; k++) { const double y = b[k] * x + c1[k] * y1[k] - c2[k] * y2[k]; y2[k] = y1[k]; y1[k] = y; o += y; }
    est *= r0; return o * 0.42;
  }
  bool alive() const { return est > 1e-4 || ne > 1e-4; }
};
struct MZap {
  double cph = 0, mph = 0, mprev = 0, pe = 0, ae = 0, ie = 0, f0 = 500, semis = 30, pk = 0, ak = 0, ik = 0, ratio = 1.7, idx = 2, fb = 0;
  void trig() { cph = 0; mph = 0; mprev = 0; pe = 1; ae = 1; ie = 1; }
  void ctl(const double* e, double pm, double sr) {
    f0 = expm(e[0], 40, 5000) * pm; semis = e[1] / 64 * 60; pk = dk(expm(e[2], 0.002, 0.5), sr); ak = dk(expm(e[3], 0.01, 2), sr);
    ratio = expm(e[4], 0.25, 12); idx = mpow(e[5] / 127, 2) * 10; ik = dk(expm(e[6], 0.004, 1.5), sr); fb = e[7] / 127 * 1.5;
  }
  double tick(double sr) {
    const double f = std::min(f0 * exp2j(semis * pe / 12), sr * 0.45); pe *= pk;
    mph += std::min(f * ratio, sr * 0.45) / sr; mph -= std::floor(mph);
    const double m = msin(TAU * mph + fb * mprev); mprev = m;
    cph += f / sr; cph -= std::floor(cph);
    const double o = msin(TAU * cph + idx * ie * m) * ae; ae *= ak; ie *= ik; return o * 0.8;
  }
  bool alive() const { return ae > 1e-4; }
};
struct MGlt {
  int src = 3; double rate = 8000; int algo = 40; double base = 3000, q = 3, sweep = 1, fk = 0, ak = 0, ph = 0, held = 0, fe = 0, ae = 0;
  int32_t t = 0; int32_t lfsr = 1; int w = 7; SVF flt;
  void seed() { w = 4 + (algo >> 4); lfsr = (1 << (w - 1)) | (algo & ((1 << (w - 1)) - 1)) | 1; t = (algo & 31) * 4096; }
  void trig() { ph = 1; fe = 1; ae = 1; seed(); }
  void ctl(const double* e, double pm, double sr) {
    src = (int)jsround(e[0]); rate = std::min(expm(e[1], 200, 48000) * pm, sr); const int a = (int)jsround(clampd(e[2], 0, 127)); if (a != algo) algo = a;
    base = expm(e[3], 100, 16000); q = 0.7 + mpow(e[4] / 127, 2) * 15; sweep = e[5] / 64 * 5;
    fk = dk(expm(e[6], 0.002, 1), sr); ak = dk(expm(e[7], 0.01, 3), sr);
    flt.set(base * exp2j(sweep * fe), q, sr);
  }
  double tick(double sr, Rng& rnd) {
    ph += rate / sr;
    if (ph >= 1) {
      ph -= std::floor(ph);
      if (src == 0) held = rnd();
      else if (src == 1) { const double r = rnd(); held = std::fabs(r) > 1 - (0.004 + mpow(algo / 127.0, 2) * 0.4) ? (r > 0 ? 1.0 : r < 0 ? -1.0 : 0.0) : held * 0.6; }
      else if (src == 2) { t++; held = (byteBeat(algo >> 5, t) & 255) / 127.5 - 1; }
      else { const int32_t bit = (lfsr ^ (lfsr >> 1)) & 1; lfsr = ((lfsr >> 1) | (bit << (w - 1))) & ((1 << w) - 1); held = (lfsr & 1) ? 0.8 : -0.8; }
    }
    fe *= fk; flt.run(held);
    const double y = (flt.bp * flt.k * 1.1 + flt.lp * 0.35) * ae * (src < 2 ? 1.7 + 0.12 * q : 1); ae *= ak; return y;
  }
  bool alive() const { return ae > 1e-4; }
};
struct MDuo {
  double p1 = 0, p2 = 0, ae = 0, ak = 0, be = 0, bk = 0, pe = 0, pk = 0, f0 = 500, r = 1.5, w = 1, semis = 0, click = 0, ce = 0, ck = 0; SVF bp;
  double osc(double ph, double dt) const {
    const double tri = 1 - 4 * std::fabs(ph - 0.5); if (w <= 0) return tri;
    const double sq = (ph < 0.5 ? 1 : -1) + blep(ph, dt) - blep(wrap01(ph + 0.5), dt); return tri + (sq - tri) * w;
  }
  void trig() { p1 = 0; p2 = 0.3; ae = 1; be = 1; pe = 1; ce = 1; }
  void ctl(const double* e, double pm, double sr) {
    f0 = expm(e[0], 40, 4000) * pm; r = 1 + e[1] / 127 * 3; w = e[2] / 127; const double d = expm(e[3], 0.01, 3);
    ak = dk(d, sr); bk = dk(std::min(0.04, d * 0.18), sr); bp.set(expm(e[4], 200, 12000), 0.7 + mpow(e[5] / 127, 2) * 10, sr);
    semis = e[6] / 127 * 24; pk = dk(0.03, sr); click = e[7] / 127; ck = dk(0.003, sr);
  }
  double tick(double sr, Rng& rnd) {
    const double fm = exp2j(semis * pe / 12); pe *= pk;
    const double f1 = std::min(f0 * fm, sr * 0.45), f2 = std::min(f0 * r * fm, sr * 0.45), d1 = f1 / sr, d2 = f2 / sr;
    p1 += d1; if (p1 >= 1) p1 -= 1; p2 += d2; if (p2 >= 1) p2 -= 1;
    const double s = (osc(p1, d1) + osc(p2, d2)) * 0.5; bp.run(s);
    double y = (bp.bp * bp.k * 1.4 + s * 0.12) * (ae * 0.55 + be * 0.6); ae *= ak; be *= bk;
    if (ce > 1e-4) { y += click * rnd() * ce * 0.8; ce *= ck; }
    return y * 2;
  }
  bool alive() const { return ae > 1e-4; }
};

struct Machine {
  int type = M_SUB;
  MSub sub; MFmk fmk; MSnr snr; MClp clp; MMtl mtl; MMdl mdl; MZap zap; MGlt glt; MDuo duo;
  void reset(int t) {
    type = t;
    switch (t) {
      case M_SUB: sub = MSub(); break; case M_FMK: fmk = MFmk(); break; case M_SNR: snr = MSnr(); break;
      case M_CLP: clp = MClp(); break; case M_MTL: mtl = MMtl(); break; case M_MDL: mdl = MMdl(); break;
      case M_ZAP: zap = MZap(); break; case M_GLT: glt = MGlt(); break; default: duo = MDuo(); break;
    }
  }
  void trig(double sr, Rng& rnd) {
    switch (type) {
      case M_SUB: sub.trig(); break; case M_FMK: fmk.trig(); break; case M_SNR: snr.trig(); break;
      case M_CLP: clp.trig(sr, rnd); break; case M_MTL: mtl.trig(); break; case M_MDL: mdl.trig(); break;
      case M_ZAP: zap.trig(); break; case M_GLT: glt.trig(); break; default: duo.trig(); break;
    }
  }
  void ctl(const double* e, double pm, double sr) {
    switch (type) {
      case M_SUB: sub.ctl(e, pm, sr); break; case M_FMK: fmk.ctl(e, pm, sr); break; case M_SNR: snr.ctl(e, pm, sr); break;
      case M_CLP: clp.ctl(e, pm, sr); break; case M_MTL: mtl.ctl(e, pm, sr); break; case M_MDL: mdl.ctl(e, pm, sr); break;
      case M_ZAP: zap.ctl(e, pm, sr); break; case M_GLT: glt.ctl(e, pm, sr); break; default: duo.ctl(e, pm, sr); break;
    }
  }
  double tick(double sr, Rng& rnd) {
    switch (type) {
      case M_SUB: return sub.tick(sr, rnd); case M_FMK: return fmk.tick(sr); case M_SNR: return snr.tick(sr, rnd);
      case M_CLP: return clp.tick(sr, rnd); case M_MTL: return mtl.tick(sr, rnd); case M_MDL: return mdl.tick(rnd);
      case M_ZAP: return zap.tick(sr); case M_GLT: return glt.tick(sr, rnd); default: return duo.tick(sr, rnd);
    }
  }
  bool alive() const {
    switch (type) {
      case M_SUB: return sub.alive(); case M_FMK: return fmk.alive(); case M_SNR: return snr.alive();
      case M_CLP: return clp.alive(); case M_MTL: return mtl.alive(); case M_MDL: return mdl.alive();
      case M_ZAP: return zap.alive(); case M_GLT: return glt.alive(); default: return duo.alive();
    }
  }
};

struct Engine;

// ---------------- voice ----------------
struct Voice {
  double sr = 48000; int tr = 0; Rng rnd;
  int mid = 0; Machine mach;
  double p[NP], e[NP]; uint8_t lockMask[NP]; double rmin[NP], rmax[NP];
  bool slideOn = false; Slide slide;
  double dkAmt = 0;
  bool active = false; double velG = 1, out = 0, dcl = 0, dclk = 0;
  int aSt = 0; double aLv = 0, aInc = 0, aHold = 0, aHoldN = 0, aK = 1, relK = 0;
  int fSt = 0; double fLv = 0, fInc = 0, fK = 1;
  SVF flt; double wl = 1, wb = 0, wh = 0; bool fBy = true;
  std::vector<float> comb; int cw = 0; double cd = 100, cMix = 0, cfb = 0, cNorm = 1;
  double srrInc = 1, srrPh = 0, srrH = 0, bitQ = 0;
  double drvG = 1, drvC = 1, gL = 0.7, gR = 0.7, sD = 0, sR = 0;
  double dcx = 0, dcy = 0; int quiet = 0;
  double lfoPh = 0; bool lfoRun = true; double lfoV = 0, lfoRnd = 0, lfoPrevPh = 0;
  double peak = 0;

  // Everything the web `new Voice(sr, tr)` sets. A reused Voice (UI preview engine, sample-rate change) must
  // start from exactly this state, or sample-rate-reduction/bit/filter/comb/LFO state leaks into the next render.
  void init(double sr_, int tr_) {
    sr = sr_; tr = tr_;
    rnd.seed((uint32_t)(0x9E3779B9u ^ (uint32_t)((tr + 1) * 7919)));
    std::memset(p, 0, sizeof p); std::memset(e, 0, sizeof e); std::memset(lockMask, 0, sizeof lockMask);
    std::memset(rmin, 0, sizeof rmin); std::memset(rmax, 0, sizeof rmax);
    slideOn = false; slide = Slide(); dkAmt = 0;
    active = false; velG = 1; out = 0; dcl = 0; dclk = dk(0.004, sr);
    aSt = 0; aLv = 0; aInc = 0; aHold = 0; aHoldN = 0; aK = 1; relK = dk(0.004, sr);
    fSt = 0; fLv = 0; fInc = 0; fK = 1;
    flt = SVF(); wl = 1; wb = 0; wh = 0; fBy = true;
    comb.assign((size_t)std::ceil(sr / 20) + 8, 0.f); cw = 0; cd = 100; cMix = 0; cfb = 0; cNorm = 1;
    srrInc = 1; srrPh = 0; srrH = 0; bitQ = 0;
    drvG = 1; drvC = 1; gL = 0.7; gR = 0.7; sD = 0; sR = 0;
    dcx = 0; dcy = 0; quiet = 0;
    lfoPh = 0; lfoRun = true; lfoV = 0; lfoRnd = 0; lfoPrevPh = 0;
    peak = 0;
    setMachine(M_SUB);
  }
  void setMachine(int m) {
    mid = (m >= 0 && m < NMACH) ? m : 0; mach.reset(mid);
    for (int i = 0; i < NP; i++) { const PDef& d = T().track(mid, i); rmin[i] = d.valid() ? d.min : 0; rmax[i] = d.valid() ? d.max : 0; }
    active = false;
  }
  double lfoWave(double ph) const {
    switch ((int)jsround(p[35])) {
      case 0: return msin(TAU * ph);
      case 1: return ph < 0.25 ? ph * 4 : ph < 0.75 ? 2 - ph * 4 : ph * 4 - 4;
      case 2: return ph < 0.5 ? 1 : -1;
      case 3: return 1 - 2 * ph;
      case 4: return 2 * ph - 1;
      case 5: return lfoRnd;
      default: return mexp(-ph * 5) * 2 - 1;
    }
  }
  inline void control(const Engine& eng);
  inline void trig(const Engine& eng, const double* base, const Locks* locks, double vel, const Slide* sl);
  void choke() { if (active) aSt = 5; }
  inline void applyLocks(const Engine& eng, const Locks* L, const Slide* sl);
  void liveParam(int i, double v) { if (!lockMask[i]) p[i] = v; }
  void hardReset() {   // numeric blow-up guard: silence and clear every state that could hold NaN
    active = false; out = 0; dcl = 0; dcx = dcy = 0; flt = SVF(); std::fill(comb.begin(), comb.end(), 0.f); srrH = 0; mach.reset(mid);
  }
};

// ---------------- master FX ----------------
struct Delay {
  double sr = 48000; int n = 0; std::vector<float> L, R; int w = 0;
  double d = 0, target = 0, fb = 0.5, wide = 1, rtn = 1, hpa = 0.99, lpa = 0.5, hL = 0, hR = 0, hxL = 0, hxR = 0, lL = 0, lR = 0;
  void init(double sr_) { sr = sr_; n = (int)std::ceil(sr * 4.2); L.assign(n, 0.f); R.assign(n, 0.f); w = 0; d = sr * 0.3; target = d; }
  double read(const std::vector<float>& buf, double dd) const {
    double r = w - dd; if (r < 0) r += n; const int i = (int)std::floor(r); const double f = r - i; const int j = i + 1 >= n ? 0 : i + 1;
    return (double)buf[i] + ((double)buf[j] - (double)buf[i]) * f;
  }
};
struct FDN {
  double sr = 48000; double base[8]; std::vector<float> bufs[8]; int len[8], idx[8]; double g[8], lp[8], o[8];
  std::vector<float> ap[4]; int apI[4];
  std::vector<float> pre; int pw = 0, pd = 1; double damp = 0.5, hp = 0, hx = 0, hpa = 0.99, rtn = 1;
  double key[5] = { -1, -1, -1, -1, -1 };
  void init(double sr_) {
    sr = sr_; const double sc = sr / 48000;
    static const int B[8] = { 1031, 1327, 1523, 1789, 2053, 2311, 2633, 2939 }; static const int A[4] = { 142, 107, 379, 277 };
    for (int i = 0; i < 8; i++) { base[i] = B[i] * sc; bufs[i].assign((size_t)std::ceil(base[i] * 1.8) + 4, 0.f); len[i] = 0; idx[i] = 0; g[i] = 0; lp[i] = 0; o[i] = 0; }
    for (int i = 0; i < 4; i++) { ap[i].assign((size_t)std::ceil(A[i] * sc) + 1, 0.f); apI[i] = 0; }
    pre.assign((size_t)std::ceil(sr * 0.21), 0.f); pw = 0; pd = 1;
    for (double& k : key) k = -1e30;
  }
  void config(double size, double dec, double dmp, double prd, double hpf) {
    if (size == key[0] && dec == key[1] && dmp == key[2] && prd == key[3] && hpf == key[4]) return;
    key[0] = size; key[1] = dec; key[2] = dmp; key[3] = prd; key[4] = hpf;
    const double s = 0.3 + size / 127 * 1.5, t60 = expm(dec, 0.3, 20);
    for (int i = 0; i < 8; i++) {
      len[i] = std::min((int)bufs[i].size() - 2, (int)jsround(base[i] * s));
      g[i] = mpow(10.0, -3.0 * len[i] / (t60 * sr));
    }
    damp = 1 - dmp / 127 * 0.85; pd = std::max(1, (int)jsround(prd / 127 * 0.2 * sr));
    hpa = mexp(-TAU * expm(hpf, 20, 2000) / sr);
  }
  inline void run(double x, double* out) {
    hp = hpa * (hp + x - hx); hx = x;
    const int PL = (int)pre.size();
    pre[pw] = (float)hp; int r = pw - pd; if (r < 0) r += PL; double v = pre[r];
    pw = (pw + 1) % PL;
    for (int a = 0; a < 4; a++) {
      std::vector<float>& b = ap[a]; const double d = b[apI[a]]; const double y = -0.7 * v + d; b[apI[a]] = (float)(v + 0.7 * y); apI[a] = (apI[a] + 1) % (int)b.size(); v = y;
    }
    for (int i = 0; i < 8; i++) {
      const std::vector<float>& b = bufs[i]; int ri = idx[i] - len[i]; if (ri < 0) ri += (int)b.size();
      lp[i] += damp * ((double)b[ri] - lp[i]); o[i] = lp[i];
    }
    const double a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3], a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
    const double b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    const double h[8] = { b0 + b4, b1 + b5, b2 + b6, b3 + b7, b0 - b4, b1 - b5, b2 - b6, b3 - b7 };
    const double s8 = 0.35355339;
    for (int i = 0; i < 8; i++) {
      std::vector<float>& b = bufs[i]; b[idx[i]] = (float)(v * 0.5 + g[i] * h[i] * s8); idx[i] = (idx[i] + 1) % (int)b.size();
    }
    out[0] = (o[0] + o[2] + o[4] + o[6]) * 0.42; out[1] = (o[1] + o[3] + o[5] + o[7]) * 0.42;
  }
};

// ---------------- events ----------------
enum EvType : uint8_t { EV_POS, EV_MPOS, EV_DUCK, EV_FX, EV_TLOCK, EV_TRIG };
struct Ev {
  double time = 0; uint64_t seq = 0; uint8_t type = 0; int8_t tr = 0;
  int sp = 0, pat = 0; bool fired = false; bool tl = false;
  // trig / duck groups (retrigs): hit k of n at base + k*spacing
  double base = 0, spacing = 0, vel = 0, fade = 0; int n = 1, k = 0; bool retrig = false;
  bool hasL = false; Locks L;
  bool hasSlide = false; Slide slide;
};

struct Feedback {   // engine -> UI, written by the audio thread only
  std::atomic<int> pos[NTR]; std::atomic<int> posPat{ 0 }; std::atomic<int> cur{ 0 }; std::atomic<int> cue{ -1 };
  std::atomic<bool> playing{ false }; std::atomic<uint32_t> fired[NT]; std::atomic<float> peak[NT];
  std::atomic<float> mpeak{ 0 }; std::atomic<float> gr{ 1 }; std::atomic<float> duckG{ 1 }; std::atomic<uint32_t> meterSeq{ 0 };
  std::atomic<double> posTime[NTR]; std::atomic<double> now{ 0 };
  Feedback() { for (int i = 0; i < NTR; i++) { pos[i] = -1; posTime[i] = 0; } for (int i = 0; i < NT; i++) { fired[i] = 0; peak[i] = 0; } }
};

struct Track { int m = M_SUB; double p[NP]; };

struct Engine {
  double sr = 48000; int64_t time = 0; bool playing = false; double bpm = 125, swing = 50, stepLen = 0;
  Track tracks[NT]; double master[NM], mEff[NM]; uint8_t mLock[NM];
  Pattern* patterns = nullptr;   // NPAT, owned
  int cur = 0, cue = -1; uint8_t mutes[NT]; bool fill = false; int solo = -1;
  Voice voices[NT];
  Rng rnd;
  double origin = 0, tNext[NTR], tSched[NTR]; int64_t tpos[NTR], tloop[NTR], tcount[NTR]; uint8_t tReset[NTR];
  double mNext = 0, mSched = 0; int64_t mstep = 0, mcount = 0, mloops = 0; double wrapAt = -1; int wrapPat = 0; int64_t wrapLoops = 0; bool wrapSwitch = false;
  uint8_t lastCond[NTR];
  struct Roll { bool on = false; double sp = 0, next = 0, vel = 110; bool hasL = false; Locks L; } rolls[NT];
  int duckSrc = 1; double duckG = 1, duckT0 = -1e12, duckFrom = 1;
  // host sync
  bool hostSync = false; double mIndexNext = 0;   // absolute host step index of mNext
  // FX
  Delay dly; FDN fdn; double rv[2] = { 0, 0 };
  SVF djL, djR; bool djOn = false; int djw = 0;
  double cEnv = 0, cAtk = 0, cRel = 0, glue = 0, cThr = 0, cRat = 1, cMake = 1, mDrv = 1, mDrvC = 1, mVol = 1;
  double dDepth = 0; int dLenIdx = 3; double dK = 1, dXa = 0; bool dFull = true; double dHigh = 1, dDly = 0, dRev = 0, dEnv = 0, dLoL = 0, dLoR = 0;
  double dAtk = 0, dRel = 0, dAttN = 0;
  int LA = 8; std::vector<float> laBuf0, laBuf1, laG; int laW = 0; double laS = 1, laSum = 0, laRel = 0;
  std::vector<int64_t> dq; std::vector<float> dqv; int64_t dqH = 0, dqT = 0, laN = 0;
  static constexpr double CEIL = 0.944;
  bool masterDirty = false;
  // meters
  int meterN = 0; double mpeak = 0;
  // idle
  int64_t idleN = 0, dlyQuiet = 0; bool fxIdle = false;
  // events
  static constexpr int POOL = 1024;
  Ev* pool = nullptr; uint16_t freeList[POOL]; int nFree = 0; uint16_t heap[POOL]; int nHeap = 0; uint64_t seqC = 0;
  Feedback fb;
  // optional per-track dry outputs (post voice chain, pre pan/vol), may be null
  float* stems[NT] = { nullptr };
  bool skipMaster = false;   // UI preview renders: voices only
  void (*dbgTrig)(int tr, double t, double vel) = nullptr;   // test hook
  // export hook (web engine onEvent): event time for sequenced hits, current time otherwise
  void (*onTrig)(void* ctx, int tr, double t, double vel) = nullptr; void* onTrigCtx = nullptr;

  Engine() {}
  ~Engine() { delete[] patterns; delete[] pool; }
  Engine(const Engine&) = delete; Engine& operator=(const Engine&) = delete;

  void init(double sampleRate) {
    sr = sampleRate; time = 0; playing = false; bpm = 125; swing = 50;
    if (!patterns) patterns = new Pattern[NPAT];
    if (!pool) pool = new Ev[POOL];
    nFree = 0; for (int i = POOL - 1; i >= 0; i--) freeList[nFree++] = (uint16_t)i; nHeap = 0;
    for (int i = 0; i < NT; i++) { voices[i].init(sr, i); voices[i].setMachine(M_SUB); tracks[i].m = M_SUB; defaultTrackParams(M_SUB, tracks[i].p); mutes[i] = 0; rolls[i].on = false; }
    defaultMaster(master); std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock);
    cur = 0; cue = -1; fill = false; solo = -1; rnd.seed(12345);
    for (int t = 0; t < NTR; t++) { tNext[t] = tSched[t] = 0; tpos[t] = tloop[t] = tcount[t] = 0; tReset[t] = 0; lastCond[t] = 0; }
    mNext = mSched = 0; mstep = mcount = mloops = 0; wrapAt = -1; wrapPat = 0; wrapLoops = 0; wrapSwitch = false;
    duckSrc = 1; duckG = 1; duckT0 = -1e12; duckFrom = 1;
    dly.init(sr); fdn.init(sr); rv[0] = rv[1] = 0;
    djL = SVF(); djR = SVF(); djOn = false; djw = 0;
    cEnv = 0; cAtk = dk(0.004, sr); cRel = dk(0.18, sr);
    dDepth = 0; dLenIdx = 3; dK = 1; dXa = 0; dFull = true; dHigh = 1; dDly = 0; dRev = 0; dEnv = 0; dLoL = dLoR = 0;
    dAtk = dk(0.002, sr); dRel = dk(0.06, sr); dAttN = 0.0015 * sr;
    LA = std::max(8, (int)jsround(sr * 0.0015));
    laBuf0.assign(LA + 1, 0.f); laBuf1.assign(LA + 1, 0.f); laG.assign(LA + 1, 1.f);
    laW = 0; laS = 1; laSum = LA + 1; laRel = dk(0.12, sr);
    dq.assign(LA + 2, 0); dqv.assign(LA + 2, 0.f); dqH = dqT = laN = 0;
    meterN = 0; mpeak = 0; idleN = 0; fxIdle = false; hostSync = false;
    setStepLen();
  }

  // ---------- event pool ----------
  bool evLess(uint16_t a, uint16_t b) const { const Ev& x = pool[a]; const Ev& y = pool[b]; return x.time < y.time || (x.time == y.time && x.seq < y.seq); }
  Ev* allocEv() { if (!nFree) return nullptr; Ev* e = &pool[freeList[--nFree]]; e->hasL = false; e->hasSlide = false; e->n = 1; e->k = 0; e->fired = false; e->tl = false; return e; }
  void heapPush(uint16_t id) {
    int i = nHeap++; heap[i] = id;
    while (i > 0) { const int pa = (i - 1) >> 1; if (!evLess(heap[i], heap[pa])) break; std::swap(heap[i], heap[pa]); i = pa; }
  }
  uint16_t heapPop() {
    const uint16_t top = heap[0]; heap[0] = heap[--nHeap]; int i = 0;
    for (;;) { const int l = 2 * i + 1, r = l + 1; int m = i; if (l < nHeap && evLess(heap[l], heap[m])) m = l; if (r < nHeap && evLess(heap[r], heap[m])) m = r; if (m == i) break; std::swap(heap[i], heap[m]); i = m; }
    return top;
  }
  void pushEv(Ev* e) { e->seq = seqC++; heapPush((uint16_t)(e - pool)); }
  void clearQueue() { while (nHeap) freeList[nFree++] = heapPop(); }
  // offline loop export (web renderLoop): at the loop end stop sequencing and keep only the sound events already
  // scheduled before it; retrig groups lose the hits at or after loopN (the web queue holds those one by one)
  void stopAt(double loopN) {
    playing = false; uint16_t keep[POOL]; int m = 0;
    for (int i = 0; i < nHeap; i++) {
      const uint16_t id = heap[i]; Ev& e = pool[id];
      const bool k = e.type != EV_POS && e.type != EV_MPOS && e.time < loopN;
      if (k && (e.type == EV_TRIG || e.type == EV_DUCK) && e.n > 1) { int nn = e.k + 1; while (nn < e.n && e.base + nn * e.spacing < loopN) nn++; e.n = nn; }
      if (k) keep[m++] = id; else freeList[nFree++] = id;
    }
    nHeap = 0; for (int i = 0; i < m; i++) heapPush(keep[i]);
  }

  // ---------- master ----------
  void masterCtl() {
    const double* m = mEff;
    dly.target = clampd(DLY_ST[(int)clampd(jsround(m[0]), 0, 9)] * stepLen, 16, dly.n - 4); dly.fb = m[1] / 127 * 0.95;
    dly.hpa = mexp(-TAU * expm(m[2], 20, 4000) / sr); dly.lpa = 1 - mexp(-TAU * expm(m[3], 400, 20000) / sr);
    dly.wide = m[4] / 127; dly.rtn = mpow(m[5] / 127, 2) * 1.2;
    fdn.config(m[8], m[9], m[10], m[11], m[12]); fdn.rtn = mpow(m[13] / 127, 2) * 1.3;
    glue = m[16] / 127; cThr = -3 - glue * 18; cRat = 1.5 + glue * 2.5; cMake = mpow(10.0, (-cThr * (1 - 1 / cRat)) * 0.35 / 20);
    mDrv = m[17] > 0 ? expm(m[17], 1, 8) : 1; mDrvC = m[17] > 0 ? 1 / (1 + m[17] / 127 * 0.5) : 1; mVol = mpow(m[19] / 100, 2);
    const double dj = m[18]; djOn = dj != 0;
    if (dj < 0) { const double f = expm(127 + dj * 1.95, 60, 20000); djL.set(f, 1.0, sr); djR.set(f, 1.0, sr); djw = -1; }
    else if (dj > 0) { const double f = expm(dj * 1.98, 20, 8000); djL.set(f, 1.0, sr); djR.set(f, 1.0, sr); djw = 1; }
    duckSrc = (int)jsround(m[24]); dLenIdx = (int)clampd(jsround(m[25]), 0, 8); dK = exp2j(-1 + m[26] / 127 * 3); dDepth = duckSrc > 0 ? m[27] / 127 : 0;
    dFull = m[28] >= 127; dXa = 1 - mexp(-TAU * expm(m[28], 40, 800) / sr); dHigh = m[29] / 127; dDly = m[30] / 127; dRev = m[31] / 127;
    masterDirty = false;
  }
  void setStepLen() { stepLen = sr * 60 / bpm / 4; masterCtl(); }
  void applyPatTempo() {
    const Pattern& P = patterns[cur];
    if (P.bpm > 0) { if (!hostSync) bpm = P.bpm; if (P.swing > 0) swing = P.swing; setStepLen(); }
  }
  int patIdxFor(double t) const { return (wrapAt >= 0 && t >= wrapAt - 1e-6) ? wrapPat : cur; }
  int64_t loopsFor(double t) const { return (wrapAt >= 0 && t >= wrapAt - 1e-6) ? wrapLoops : mloops; }
  bool condOk(int tr, int c, double t) const {
    switch (c) {
      case 1: return fill; case 2: return !fill;
      case 3: return lastCond[tr] != 0; case 4: return lastCond[tr] == 0;
      case 5: return tr > 0 ? lastCond[tr - 1] != 0 : false; case 6: return tr > 0 ? lastCond[tr - 1] == 0 : true;
      case 7: return loopsFor(t) == 0; case 8: return loopsFor(t) != 0;
      default: { if (c < 9 || c >= NCOND) return true; return (tloop[tr] % COND_B[c]) == COND_A[c] - 1; }
    }
  }
  void commitWrap() {
    const bool changed = wrapPat != cur;
    cur = wrapPat; mloops = wrapLoops; wrapAt = -1;
    if (changed) { applyPatTempo(); fb.cur.store(cur, std::memory_order_relaxed); fb.cue.store(-1, std::memory_order_relaxed); }
  }
  void masterStep() {
    const double Tm = mNext;
    if (mcount > 0 && mstep == 0) {
      if (wrapAt >= 0) commitWrap();
      wrapAt = Tm; std::memset(tReset, 0, sizeof tReset);
      if (cue >= 0) { wrapPat = cue; wrapLoops = 0; wrapSwitch = true; cue = -1; }
      else { wrapPat = cur; wrapLoops = mloops + 1; wrapSwitch = false; }
    }
    if (Ev* e = allocEv()) { e->time = Tm; e->type = EV_MPOS; e->sp = (int)mstep; pushEv(e); }
    const Pattern& P = patterns[patIdxFor(Tm)];
    mstep = (mstep + 1) % std::max(1, P.mlen);
    mcount++; mNext += stepLen; mSched = mNext - 5 * stepLen; mIndexNext += 1;
  }
  void scheduleTrack(int tr) {
    double t = tNext[tr];
    if (wrapAt >= 0 && !tReset[tr] && t >= wrapAt - 1e-6) {
      t = wrapAt; tNext[tr] = t; tReset[tr] = 1;
      if (wrapSwitch) { tloop[tr] = 0; lastCond[tr] = 0; } else if (tpos[tr] != 0) tloop[tr]++;
      tpos[tr] = 0; tcount[tr] = 0;
    }
    const int pi = patIdxFor(t); const Pattern& P = patterns[pi];
    const int sc = P.scl[tr] < 0 || P.scl[tr] > 6 ? 4 : P.scl[tr];
    const double sl = stepLen / SPEED_X[sc]; const int len = std::max(1, P.len[tr]); const int sp = (int)(tpos[tr] % len);
    const double t0 = t + ((tcount[tr] & 1) ? (swing - 50) / 50 * sl : 0);
    const Step& st = P.steps[tr][sp];
    bool fired = false;
    if (st.on) {
      if (tr == 8) {
        if (Ev* e = allocEv()) { e->time = t0; e->type = EV_FX; e->tl = st.tl != 0; e->hasL = st.l.any(); if (e->hasL) e->L = st.l; pushEv(e); }
        fired = true;
      } else fired = trackStep(tr, st, P.steps[tr], sp, len, t0, sl);
    }
    if (Ev* e = allocEv()) { e->time = t0; e->type = EV_POS; e->tr = (int8_t)tr; e->sp = sp; e->fired = fired; e->pat = pi; pushEv(e); }
    int64_t np = sp + 1; if (np >= len) { np = 0; tloop[tr]++; }
    tpos[tr] = np; tcount[tr]++; tNext[tr] = t + sl; tSched[tr] = tNext[tr] - 0.5 * sl;
  }
  bool trackStep(int tr, const Step& st, const Step* row, int sp, int len, double t0, double sl) {
    const double* base = tracks[tr].p; const Locks* L0 = st.l.any() ? &st.l : nullptr;
    auto g = [&](int i) { return (L0 && L0->has(i)) ? L0->v[i] : base[i]; };
    const int c = (int)jsround(g(43)); const double prob = g(42);
    bool ok = true;
    if (c != 0) { ok = condOk(tr, c, t0); if (c < 3 || c > 6) lastCond[tr] = ok ? 1 : 0; }
    if (ok && prob < 100) { ok = (rnd() * 0.5 + 0.5) * 100 < prob; if (c == 0) lastCond[tr] = ok ? 1 : 0; }
    if (!ok) return false;
    Locks Lf; const Locks* L = L0;
    if (L0) {
      const int lc = (int)jsround(g(49)); const double lp = g(50);
      bool lok = true; if (lc != 0) lok = condOk(tr, lc, t0); if (lok && lp < 100) lok = (rnd() * 0.5 + 0.5) * 100 < lp;
      if (!lok) { Lf = *L0; for (int i = 0; i < NP; i++) if (!STRUCT(i)) Lf.del(i); L = &Lf; }
    }
    const double tt = t0 + g(44) / 48 * sl;
    Slide slide; bool hasSlide = false;
    if (g(48) >= 0.5) {
      int j = -1; for (int d = 1; d < len; d++) { const int q = (sp + d) % len; if (row[q].on) { j = q; break; } }
      if (j >= 0) {
        const Locks& nl = row[j].l; int n = 0;
        for (int i = 0; i <= 40; i++) {
          if (i == 22) continue;
          const bool inL = L && L->has(i), inN = nl.has(i);
          if (!inL && !inN) continue;
          const double a = inL ? L->v[i] : base[i], b = inN ? nl.v[i] : base[i];
          if (a != b) { slide.i[n] = (uint8_t)i; slide.a[n] = a; slide.b[n] = b; n++; }
        }
        const int dist = (j - sp + len) % len;
        if (n && dist > 0) { slide.n = n; slide.dur = dist * sl; slide.t0 = tt; slide.hasT0 = true; hasSlide = true; }
      }
    }
    const bool audible = !mutes[tr] && (solo < 0 || solo == tr);
    if (st.tl) {
      if (audible) if (Ev* e = allocEv()) {
        e->time = tt; e->type = EV_TLOCK; e->tr = (int8_t)tr; e->hasL = L && L->any(); if (e->hasL) e->L = *L;
        e->hasSlide = hasSlide; if (hasSlide) e->slide = slide; pushEv(e);
      }
      return true;
    }
    const double vel = g(41); const int rt = (int)jsround(g(45));
    const uint8_t type = audible ? EV_TRIG : (duckSrc == tr + 1 ? EV_DUCK : 255);
    if (type == 255) return true;
    Ev* e = allocEv(); if (!e) return true;
    e->type = type; e->tr = (int8_t)tr; e->vel = vel; e->base = tt;
    if (rt > 0 && rt < 8) {
      const double spacing = RTRG_ST[rt] * stepLen, span = RLEN_ST[(int)clampd(jsround(g(47)), 0, 5)] * sl;
      e->n = std::max(1, (int)std::floor(span / spacing + 1e-6)); e->spacing = spacing; e->fade = g(46) / 64; e->retrig = true;
    } else { e->n = 1; e->spacing = 0; e->fade = 0; e->retrig = false; }
    e->k = 0; e->time = tt;
    if (type == EV_TRIG) { e->hasL = L && L->any(); if (e->hasL) e->L = *L; e->hasSlide = hasSlide; if (hasSlide) e->slide = slide; }
    pushEv(e);
    return true;
  }
  static double hitVel(const Ev& e) {
    if (!e.retrig) return e.vel;
    const double u = e.n > 1 ? (double)e.k / (e.n - 1) : 0, fade = e.fade, vel = e.vel;
    return std::max(1.0, fade < 0 ? vel * (1 + fade * u) : vel * (1 - fade * (1 - u)));
  }
  void applyFx(const Ev& ev) {
    std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock);
    if (ev.hasL) for (int i = 0; i < NM && i < NP; i++) if (ev.L.has(i) && T().master[i].valid() && T().master[i].lock) { mEff[i] = ev.L.v[i]; mLock[i] = 1; }
    masterCtl();
    if (!ev.tl && duckSrc == 9) duckTrig();
  }
  void duckTrig() { duckFrom = duckG; duckT0 = (double)time; }
  void fire(Ev& ev) {
    switch (ev.type) {
      case EV_POS:
        fb.pos[ev.tr].store(ev.sp, std::memory_order_relaxed); fb.posTime[ev.tr].store((double)time, std::memory_order_relaxed);
        fb.posPat.store(ev.pat, std::memory_order_relaxed);
        if (ev.fired && ev.tr < 8) fb.fired[ev.tr].fetch_add(1, std::memory_order_relaxed);
        return;
      case EV_MPOS: return;
      case EV_DUCK: duckTrig(); return;
      case EV_FX: applyFx(ev); return;
      case EV_TLOCK: voices[ev.tr].applyLocks(*this, ev.hasL ? &ev.L : nullptr, ev.hasSlide ? &ev.slide : nullptr); return;
      case EV_TRIG: trigTrack(ev.tr, ev.hasL ? &ev.L : nullptr, hitVel(ev), ev.hasSlide ? &ev.slide : nullptr, ev.time); return;
    }
  }
  void trigTrack(int tr, const Locks* L, double vel, const Slide* sl = nullptr, double evTime = std::numeric_limits<double>::quiet_NaN()) {
    Voice& v = voices[tr]; const double* base = tracks[tr].p; const int grp = (int)jsround(base[22]);
    if (grp > 0) for (int o = 0; o < NT; o++) if (o != tr && (int)jsround(tracks[o].p[22]) == grp) voices[o].choke();
    v.trig(*this, base, L, vel, sl);
    if (dbgTrig) dbgTrig(tr, (double)time, vel);
    if (onTrig) onTrig(onTrigCtx, tr, std::isnan(evTime) ? (double)time : evTime, vel);
    if (duckSrc == tr + 1) duckTrig();
    fb.fired[tr].fetch_add(1, std::memory_order_relaxed);
  }

  // ---------- public API (mirrors E.msg) ----------
  void normPattern(Pattern& P) { P.mlen = std::max(1, std::min(64, P.mlen)); for (int t = 0; t < NTR; t++) { P.len[t] = std::max(1, std::min(64, P.len[t])); if (P.scl[t] < 0 || P.scl[t] > 6) P.scl[t] = 4; } }
  void loadTrack(int tr, int m, const double* p) { tracks[tr].m = m; std::memcpy(tracks[tr].p, p, sizeof(double) * NP); voices[tr].setMachine(m); }
  void loadMaster(const double* m) { std::memcpy(master, m, sizeof master); std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock); }
  void loadDone(int cur_, double bpm_, double swing_, const uint8_t* mutes_) {
    cur = cur_; if (!hostSync) bpm = bpm_; swing = swing_; if (mutes_) std::memcpy(mutes, mutes_, NT); setStepLen();
    fb.cur.store(cur, std::memory_order_relaxed);
  }
  void setParam(int tr, int i, double v) {
    if (tr == 8) { if (i < 0 || i >= NM) return; master[i] = v; if (!mLock[i]) mEff[i] = v; masterDirty = true; }
    else { if (i < 0 || i >= NP) return; tracks[tr].p[i] = v; voices[tr].liveParam(i, v); }
  }
  void setMachine(int tr, int m, const double* p) { loadTrack(tr, m, p); }
  void setTrackP(int tr, const double* p) { std::memcpy(tracks[tr].p, p, sizeof(double) * NP); }
  void setPattern(int i, const Pattern& P) { patterns[i] = P; normPattern(patterns[i]); }
  void setStep(int i, int tr, int s, const Step& st) { patterns[i].steps[tr][s] = st; }
  void setPlen(int i, int mlen, const int* len, const int* scl) {
    Pattern& P = patterns[i]; P.mlen = mlen; std::memcpy(P.len, len, sizeof P.len); if (scl) std::memcpy(P.scl, scl, sizeof P.scl); normPattern(P);
  }
  void msgBpm(double v) { if (hostSync) { patterns[cur].bpm = v; return; } bpm = v; patterns[cur].bpm = v; setStepLen(); }
  void msgSwing(double v) { swing = v; patterns[cur].swing = v; }
  void play(double delay = 64) {
    playing = true; clearQueue();
    const double t = (double)time + delay; origin = t;
    if (cue >= 0) { cur = cue; cue = -1; applyPatTempo(); fb.cur.store(cur); fb.cue.store(-1); }
    for (int tr = 0; tr < NTR; tr++) { tNext[tr] = t; tSched[tr] = t - 0.5 * stepLen; tpos[tr] = 0; tloop[tr] = 0; tcount[tr] = 0; tReset[tr] = 0; lastCond[tr] = 0; }
    mNext = t; mSched = t - 5 * stepLen; mstep = 0; mcount = 0; mloops = 0; wrapAt = -1;
    std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock); masterCtl();
    fb.playing.store(true);
  }
  void stopSeq() {
    playing = false; clearQueue(); wrapAt = -1;
    std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock); masterCtl();
    for (int t = 0; t < NTR; t++) fb.pos[t].store(-1, std::memory_order_relaxed);
    fb.playing.store(false);
  }
  void msgStop() { if (!playing) for (Voice& v : voices) v.choke(); stopSeq(); }
  void msgCue(int i) { if (i < 0 || i >= NPAT) return; if (playing) { cue = i; fb.cue.store(i); } else { cur = i; fb.cur.store(i); } }
  void msgMute(int tr, bool v) { mutes[tr] = v ? 1 : 0; }
  void audition(int tr, double vel, const Locks* L) { trigTrack(tr, L, vel > 0 ? vel : 110); }
  void roll(int tr, bool on, double st, double vel, const Locks* L) {
    if (on) {
      const double sp = std::max(16.0, st * stepLen);
      const double next = playing ? origin + std::ceil(((double)time - origin) / sp - 1e-9) * sp : (double)time;
      Roll& r = rolls[tr]; r.on = true; r.sp = sp; r.next = next; r.vel = vel > 0 ? vel : 110; r.hasL = L && L->any(); if (r.hasL) r.L = *L;
    } else rolls[tr].on = false;
  }
  void resetFx() {
    std::fill(dly.L.begin(), dly.L.end(), 0.f); std::fill(dly.R.begin(), dly.R.end(), 0.f);
    dly.hL = dly.hR = dly.hxL = dly.hxR = dly.lL = dly.lR = 0;
    for (int i = 0; i < 8; i++) { std::fill(fdn.bufs[i].begin(), fdn.bufs[i].end(), 0.f); fdn.lp[i] = fdn.o[i] = 0; }
    for (int i = 0; i < 4; i++) std::fill(fdn.ap[i].begin(), fdn.ap[i].end(), 0.f);
    std::fill(fdn.pre.begin(), fdn.pre.end(), 0.f); fdn.hp = fdn.hx = 0;
    djL = SVF(); djR = SVF(); cEnv = 0; dEnv = 0; dLoL = dLoR = 0; masterCtl();
    std::fill(laBuf0.begin(), laBuf0.end(), 0.f); std::fill(laBuf1.begin(), laBuf1.end(), 0.f);
  }
  // UI preview renders: one fresh voice on track 1, no sequencer, no master FX
  void previewReset(double bpm_) { time = 0; playing = false; clearQueue(); bpm = bpm_; for (int i = 0; i < NT; i++) voices[i].active = false; voices[0].init(sr, 0); skipMaster = true; setStepLen(); }
  void panic() { for (Voice& v : voices) { v.active = false; v.out = 0; v.dcl = 0; } for (Roll& r : rolls) r.on = false; }

  // ---------- host transport ----------
  // Start the sequencer so that master step index `s0` (an integer, counted in 16ths from the host's
  // song start) lands on sample time `tS`. Track positions inside the current pattern loop are derived
  // from the loop phase, so starting Live in the middle of a bar plays what the pattern has there.
  void relocate(int64_t s0, double tS) {
    playing = true; clearQueue(); hostSync = true;
    for (Roll& r : rolls) if (r.on) r.next = tS;
    const Pattern& P = patterns[cur];
    const int ml = std::max(1, P.mlen);
    int64_t loops = s0 / ml, m = s0 % ml; if (m < 0) { m += ml; loops--; }
    if (loops < 0) loops = 0;
    const double loopStart = tS - m * stepLen; origin = loopStart;
    const bool atWrap = (m == 0 && loops > 0);
    for (int tr = 0; tr < NTR; tr++) {
      const double sx = SPEED_X[P.scl[tr] < 0 || P.scl[tr] > 6 ? 4 : P.scl[tr]], sl = stepLen / sx; const int64_t len = std::max(1, P.len[tr]);
      const int64_t K = (int64_t)std::ceil(ml * sx - 1e-9);                     // track steps per master loop
      const int64_t perLoop = K / len + ((K % len) ? 1 : 0);                     // tloop increments per master loop
      lastCond[tr] = 0; tReset[tr] = 0;
      if (atWrap) {   // positioned at the end of the previous loop: the wrap logic resets the track
        tcount[tr] = K; tpos[tr] = K % len; tloop[tr] = (loops - 1) * perLoop + K / len; tNext[tr] = tS;
      } else {
        const int64_t k = (int64_t)std::ceil(m * sx - 1e-9);
        tcount[tr] = k; tpos[tr] = k % len; tloop[tr] = loops * perLoop + k / len; tNext[tr] = loopStart + k * sl;
      }
      tSched[tr] = tNext[tr] - 0.5 * sl;
    }
    mNext = tS; mSched = tS - 5 * stepLen; wrapAt = -1;
    if (atWrap) { mstep = 0; mcount = 1; mloops = loops - 1; }
    else { mstep = m; mcount = m; mloops = loops; }
    mIndexNext = (double)s0;
    std::memcpy(mEff, master, sizeof master); std::memset(mLock, 0, sizeof mLock); masterCtl();
    fb.playing.store(true);
  }
  // Shift every pending sequencer time by `d` samples (tempo automation / drift correction).
  void warp(double d) {
    mNext += d; mSched += d; origin += d; if (wrapAt >= 0) wrapAt += d;
    for (int t = 0; t < NTR; t++) { tNext[t] += d; tSched[t] += d; }
    for (int i = 0; i < nHeap; i++) { Ev& e = pool[heap[i]]; e.time += d; e.base += d; }   // uniform shift keeps heap order
    for (Roll& r : rolls) if (r.on) r.next += d;
  }
  // Scale every pending sequencer time around `now` by `r` (host tempo change: musical distances kept).
  void rescale(double r) {
    const double now = (double)time;
    auto sc = [&](double& t) { t = now + (t - now) * r; };
    sc(mNext); sc(mSched); sc(origin); if (wrapAt >= 0) sc(wrapAt);
    for (int t = 0; t < NTR; t++) { sc(tNext[t]); sc(tSched[t]); }
    for (int i = 0; i < nHeap; i++) { Ev& e = pool[heap[i]]; sc(e.time); sc(e.base); e.spacing *= r; if (e.hasSlide) { e.slide.dur *= r; sc(e.slide.t0); } }
    for (Roll& rl : rolls) if (rl.on) { sc(rl.next); rl.sp *= r; }
  }
  // Called once per block before process(): ppq = host position in quarter notes at the block start.
  void hostBlock(bool hostPlaying, double ppq, double hostBpm) {
    if (!hostPlaying) { if (hostSync) { hostSync = false; stopSeq(); } return; }
    if (hostBpm >= 20 && hostBpm <= 999 && std::fabs(hostBpm - bpm) > 1e-9) {
      const double old = stepLen; bpm = hostBpm; setStepLen();
      if (hostSync && playing && old > 0) rescale(stepLen / old);
    }
    const double stepPos = ppq * 4;   // 16ths
    if (!hostSync || !playing) {
      if (cue >= 0) { cur = cue; cue = -1; fb.cur.store(cur); fb.cue.store(-1); if (patterns[cur].swing > 0) swing = patterns[cur].swing; }
      const double s0 = std::ceil(stepPos - 1e-6);
      relocate((int64_t)s0, (double)time + (s0 - stepPos) * stepLen);
      return;
    }
    const double expected = (double)time + (mIndexNext - stepPos) * stepLen;   // where mNext should be
    const double diff = expected - mNext;
    if (std::fabs(diff) > 0.5 * stepLen) {       // jump / loop / relocation in the host
      const double s0 = std::ceil(stepPos - 1e-6);
      relocate((int64_t)s0, (double)time + (s0 - stepPos) * stepLen);
    } else if (std::fabs(diff) > 0.25) warp(diff);
  }
  void internalPlay() { hostSync = false; play(); }

  // ---------- audio ----------
  bool allReset() const { for (int tr = 0; tr < NTR; tr++) if (!tReset[tr]) return false; return true; }
  void process(float* OL, float* OR, int n);
};

// ---------------- voice impl ----------------
inline void Voice::control(const Engine& eng) {
  const double now = (double)eng.time;
  if (slideOn) {
    const Slide& s = slide; const double u = std::min(1.0, (now - s.t0) / s.dur);
    for (int k = 0; k < s.n; k++) { p[s.i[k]] = s.a[k] + (s.b[k] - s.a[k]) * u; lockMask[s.i[k]] = 1; }
    if (u >= 1) slideOn = false;
  }
  std::memcpy(e, p, sizeof e);
  const double dep = p[33]; double semis = p[40];
  const double cyc = LFO_SPD_ST[(int)clampd(jsround(p[32]), 0, 13)] * eng.stepLen; const int mode = (int)jsround(p[36]);
  if (mode == 0) lfoPh = jsmod1((now / cyc) + p[37] / 128);
  else if (lfoRun) {
    lfoPh += 16 / cyc;
    const double lim = mode == 2 ? 1 : mode == 3 ? 0.5 : 1e300;
    if (lfoPh >= lim) { lfoPh = lim - 1e-9; lfoRun = false; }
    else if (lfoPh >= 1) lfoPh -= 1;
  }
  if (lfoPh < lfoPrevPh) lfoRnd = rnd();
  lfoPrevPh = lfoPh;
  if (dep != 0) {
    lfoV = lfoWave(lfoPh);
    const int d = LFO_DEST_IDX[(int)clampd(jsround(p[34]), 0, 15)]; const double mod = lfoV * dep / 64;
    if (d == 40) semis += mod * 24;
    else e[d] = clampd(e[d] + mod * (rmax[d] - rmin[d]), rmin[d], rmax[d]);
  } else lfoV = 0;
  const double pm = exp2j(semis / 12);
  mach.ctl(e, pm, sr);
  const double type = e[10] / 63.5;
  wl = std::max(0.0, 1 - type); wb = std::max(0.0, 1 - std::fabs(type - 1)); wh = std::max(0.0, type - 1);
  fBy = e[8] >= 127 && type < 0.02 && e[11] == 0 && e[9] < 1;
  if (!fBy) {
    const double oct = e[11] / 64 * 7 * fLv + e[14] / 127 * semis / 12;
    flt.set(expm(e[8], 20, 20000) * exp2j(oct), 0.6 + mpow(e[9] / 127, 2) * 22, sr);
  }
  fInc = e[12] <= 0 ? 1 : 16 / (expm(e[12], 0.001, 2) * sr); fK = dk(expm(e[13], 0.005, 4), sr);
  aInc = e[16] <= 0 ? 1 : 1 / (expm(e[16], 0.0005, 2) * sr);
  aHoldN = e[17] <= 0 ? 0 : expm(e[17], 0.001, 2) * sr;
  aK = e[18] >= 127 ? 1 : dk(expm(e[18], 0.005, 8), sr);
  drvG = e[19] > 0 ? expm(e[19], 1, 24) : 1; drvC = e[19] > 0 ? 1 / (1 + e[19] / 127 * 0.6) : 1;
  const double pan = (e[20] + 64) / 127.5, vol = mpow(e[21] / 100, 2);
  gL = mcos(pan * PI / 2) * vol * 1.41421; gR = msin(pan * PI / 2) * vol * 1.41421;
  sD = mpow(e[24] / 127, 2); sR = mpow(e[25] / 127, 2); dkAmt = eng.duckSrc == tr + 1 ? 0 : e[23] / 127;
  const int bits = (int)jsround(e[26]); bitQ = bits >= 16 ? 0 : mpow(2.0, bits - 1);
  srrInc = e[27] <= 0 ? 1 : 1 / (1 + mpow(e[27] / 127, 2) * 80);
  cMix = e[28] / 127;
  if (cMix > 0) {
    cd = clampd(sr / (440 * exp2j((e[29] - 69 + semis) / 12)), 2, (double)comb.size() - 4);
    cfb = e[30] / 127 * 0.97; cNorm = std::sqrt(1 - cfb) * 1.2;
  }
}
inline void Voice::trig(const Engine& eng, const double* base, const Locks* locks, double vel, const Slide* sl) {
  dcl += out;
  std::memcpy(p, base, sizeof p); std::memset(lockMask, 0, sizeof lockMask);
  if (locks) for (int i = 0; i < NP; i++) if (locks->has(i)) { p[i] = locks->v[i]; lockMask[i] = 1; }
  if (sl) { slide = *sl; if (!sl->hasT0) slide.t0 = (double)eng.time; slideOn = true; } else slideOn = false;
  velG = mpow(clampd(vel, 0, 127) / 127, 1.2);
  aSt = p[16] <= 0 ? 2 : 1; aLv = aSt == 2 ? 1 : 0; aHold = 0;
  fSt = 1; fLv = p[12] <= 0 ? 1 : 0; if (p[12] <= 0) fSt = 2;
  if ((int)jsround(p[36]) != 0) { lfoPh = p[37] / 128; lfoRun = true; lfoPrevPh = lfoPh; }
  control(eng); mach.trig(sr, rnd); control(eng);
  active = true; quiet = 0;
}
inline void Voice::applyLocks(const Engine& eng, const Locks* L, const Slide* sl) {
  if (L) for (int i = 0; i <= 40; i++) if (i != 22 && L->has(i)) { p[i] = L->v[i]; lockMask[i] = 1; }
  if (sl) { slide = *sl; if (!sl->hasT0) slide.t0 = (double)eng.time; slideOn = true; }
  if (active) control(eng);
}

// ---------------- process ----------------
inline void Engine::process(float* OL, float* OR, int n) {
  Voice* V = voices;
  if (masterDirty) masterCtl();   // many automation points in one block -> one recompute
  for (int i = 0; i < n; i++) {
    const double now = (double)time;
    if (playing) {
      int guard = 0;
      while (now >= mSched && guard++ < 64) masterStep();
      for (int tr = 0; tr < NTR; tr++) {
        guard = 0;
        while ((now >= tSched[tr] || (wrapAt >= 0 && !tReset[tr] && tNext[tr] > wrapAt + 1e-6 && now >= wrapAt - 0.5 * stepLen)) && guard++ < 64) scheduleTrack(tr);
      }
      if (wrapAt >= 0 && now >= wrapAt && allReset()) commitWrap();
    }
    for (int tr = 0; tr < NT; tr++) { Roll& r = rolls[tr]; if (r.on && now >= r.next) { trigTrack(tr, r.hasL ? &r.L : nullptr, r.vel); r.next += r.sp; } }
    while (nHeap && pool[heap[0]].time <= now) {
      const uint16_t id = heapPop(); Ev& ev = pool[id];
      fire(ev);
      if ((ev.type == EV_TRIG || ev.type == EV_DUCK) && ev.k + 1 < ev.n) { ev.k++; ev.time = ev.base + ev.k * ev.spacing; heapPush(id); }   // keeps its seq
      else freeList[nFree++] = id;
    }
    if ((time & 15) == 0) {
      for (int t = 0; t < NT; t++) { Voice& v = V[t]; if (v.active) v.control(*this); }
    }
    double gd = 1;
    if (dDepth > 0) {
      if (dLenIdx == 0 && duckSrc <= 8) {
        const double x = std::fabs(V[duckSrc - 1].out);
        dEnv = x > dEnv ? x + (dEnv - x) * dAtk : x + (dEnv - x) * dRel;
        gd = 1 - dDepth * std::min(1.0, dEnv * 2.5);
      } else {
        const double lenS = (dLenIdx == 0 ? 2 : DUCK_LEN_ST[dLenIdx]) * stepLen, el = now - duckT0;
        if (el < lenS) { const double target = 1 - dDepth * mpow(1 - el / lenS, dK); gd = el < dAttN ? duckFrom + (target - duckFrom) * (el / dAttN) : target; }
      }
    }
    duckG = gd;
    double L = 0, R = 0, sD = 0, sR = 0, dL = 0, dR = 0; bool anyActive = false;
    for (int t = 0; t < NT; t++) {
      Voice& v = V[t];
      if (!v.active) { if (stems[t]) stems[t][i] = 0; continue; }
      anyActive = true;
      double x = v.mach.tick(v.sr, v.rnd);
      if (v.srrInc < 1) { v.srrPh += v.srrInc; if (v.srrPh >= 1) { v.srrPh -= 1; v.srrH = x; } x = v.srrH; }
      if (v.bitQ != 0) x = jsround(x * v.bitQ) / v.bitQ;
      if (v.cMix > 0) {
        float* c = v.comb.data(); const int N = (int)v.comb.size(); double r = v.cw - v.cd; if (r < 0) r += N;
        const int ri = (int)std::floor(r); const double f = r - ri; const int rj = ri + 1 >= N ? 0 : ri + 1;
        const double y = x + v.cfb * ((double)c[ri] + ((double)c[rj] - (double)c[ri]) * f); c[v.cw] = (float)y; v.cw = v.cw + 1 >= N ? 0 : v.cw + 1;
        x = x * (1 - v.cMix) + y * v.cMix * v.cNorm;
      }
      if (v.fSt == 1) { v.fLv += v.fInc / 16; if (v.fLv >= 1) { v.fLv = 1; v.fSt = 2; } } else if (v.fSt == 2) v.fLv *= v.fK;
      if (!v.fBy) { SVF& fl = v.flt; fl.run(x); x = fl.lp * v.wl + fl.bp * fl.k * v.wb + fl.hp * v.wh; }
      if (v.drvG > 1) x = sat(x * v.drvG) * v.drvC;
      switch (v.aSt) {
        case 1: v.aLv += v.aInc; if (v.aLv >= 1) { v.aLv = 1; v.aSt = 2; } break;
        case 2: v.aHold++; if (v.aHold >= v.aHoldN) v.aSt = 3; break;
        case 3: v.aLv *= v.aK; break;
        case 5: v.aLv *= v.relK; break;
      }
      x *= v.aLv * v.velG;
      v.dcy = x - v.dcx + 0.9993 * v.dcy; v.dcx = x; x = v.dcy;
      if (v.dcl != 0) { x += v.dcl; v.dcl *= v.dclk; if (std::fabs(v.dcl) < 1e-6) v.dcl = 0; }
      if (x != x || std::fabs(x) > 1e6) { x = 0; v.hardReset(); }
      v.out = x;
      const double xl = x * v.gL, xr = x * v.gR, kd = v.dkAmt;
      if (kd > 0) { L += xl * (1 - kd); R += xr * (1 - kd); dL += xl * kd; dR += xr * kd; } else { L += xl; R += xr; }
      sD += x * v.sD; sR += x * v.sR;
      if (stems[t]) stems[t][i] = (float)x;
      const double ax = std::fabs(x); if (ax > v.peak) v.peak = ax;
      if (ax < 2e-5) { if (++v.quiet > 4096 && !v.mach.alive()) v.active = false; } else v.quiet = 0;
      if (v.aSt == 5 && v.aLv < 1e-4) { v.active = false; v.dcl = 0; v.out = 0; }
    }
    if (skipMaster) { OL[i] = (float)L; OR[i] = (float)R; time++; continue; }
    // idle: no voice, no sends, tails decayed -> skip the FX chain (bit-exact zeros otherwise)
    if (!anyActive) { if (fxIdle) { OL[i] = 0; OR[i] = 0; time++; continue; } }
    else { idleN = 0; fxIdle = false; }
    if (dFull) { L += dL * gd; R += dR * gd; }
    else {
      dLoL += dXa * (dL - dLoL); dLoR += dXa * (dR - dLoR);
      const double gh = 1 - (1 - gd) * dHigh; L += dLoL * gd + (dL - dLoL) * gh; R += dLoR * gd + (dR - dLoR) * gh;
    }
    dly.d += (dly.target - dly.d) * 0.0004;
    const double rL = dly.read(dly.L, dly.d), rR = dly.read(dly.R, dly.d);
    const double fL = dly.wide * rR + (1 - dly.wide) * rL, fR = dly.wide * rL + (1 - dly.wide) * rR;
    dly.hL = dly.hpa * (dly.hL + fL - dly.hxL); dly.hxL = fL; dly.hR = dly.hpa * (dly.hR + fR - dly.hxR); dly.hxR = fR;
    dly.lL += dly.lpa * (dly.hL - dly.lL); dly.lR += dly.lpa * (dly.hR - dly.lR);
    const double wL = sD + sat(dly.lL * dly.fb * 1.05), wR = sD * (1 - dly.wide) + sat(dly.lR * dly.fb * 1.05);
    dly.L[dly.w] = (float)wL; dly.R[dly.w] = (float)wR;
    if (std::fabs(wL) < 1e-7 && std::fabs(wR) < 1e-7) dlyQuiet++; else dlyQuiet = 0;
    dly.w = dly.w + 1 >= dly.n ? 0 : dly.w + 1;
    fdn.run(sR, rv);
    const double gD = dly.rtn * (1 - (1 - gd) * dDly), gR = fdn.rtn * (1 - (1 - gd) * dRev);
    L += rL * gD + rv[0] * gR; R += rR * gD + rv[1] * gR;
    if (djOn) { djL.run(L); djR.run(R); if (djw < 0) { L = djL.lp; R = djR.lp; } else { L = djL.hp; R = djR.hp; } }
    L *= 0.5; R *= 0.5;
    if (glue > 0) {
      const double pk = std::max(std::fabs(L), std::fabs(R)); cEnv = pk > cEnv ? pk + (cEnv - pk) * cAtk : pk + (cEnv - pk) * cRel;
      const double db = 20 * mlog10(cEnv + 1e-9), over = db - cThr; double grd = 0;
      if (over > -3) { const double o2 = over < 3 ? (over + 3) * (over + 3) / 12 : over; grd = o2 * (1 - 1 / cRat); }
      const double g = mpow(10.0, -grd / 20) * cMake; L *= g; R *= g;
    }
    if (mDrv > 1) { L = sat(L * mDrv) * mDrvC; R = sat(R * mDrv) * mDrvC; }
    L *= mVol; R *= mVol;
    const double pk = std::max(std::fabs(L), std::fabs(R)), gr = pk > CEIL ? CEIL / pk : 1;
    const int QN = LA + 2;
    while (dqT > dqH && dqv[(dqT - 1) % QN] >= (float)gr) dqT--;
    dq[dqT % QN] = laN; dqv[dqT % QN] = (float)gr; dqT++;
    while (dq[dqH % QN] <= laN - LA - 1) dqH++;
    const double mm = dqv[dqH % QN]; laN++;
    laS = mm < laS ? mm : mm + (laS - mm) * laRel;
    laSum += laS - (double)laG[laW]; laG[laW] = (float)laS;
    const double xL = laBuf0[laW], xR = laBuf1[laW]; laBuf0[laW] = (float)L; laBuf1[laW] = (float)R; laW = laW + 1 > LA ? 0 : laW + 1;
    const double g = std::min(laSum / (LA + 1), 1.0);
    double oL = xL * g, oR = xR * g;
    if (oL > 0.99) oL = 0.99; else if (oL < -0.99) oL = -0.99; if (oR > 0.99) oR = 0.99; else if (oR < -0.99) oR = -0.99;
    if (oL != oL || oR != oR) { oL = oR = 0; resetFx(); laSum = LA + 1; std::fill(laG.begin(), laG.end(), 1.f); laS = 1; }
    OL[i] = (float)oL; OR[i] = (float)oR;
    const double ap = std::max(std::fabs(oL), std::fabs(oR)); if (ap > mpeak) mpeak = ap;
    if (!anyActive) {
      // tails: everything feeding the output is tiny -> after 1 s go idle
      bool q = ap < 1e-7 && dlyQuiet > dly.n;
      for (int k = 0; q && k < 8; k++) q = std::fabs(fdn.lp[k]) < 1e-7;
      if (q) { if (++idleN > (int64_t)sr) fxIdle = true; } else idleN = 0;
    }
    time++;
  }
  meterN += n;
  if (meterN >= 2048) {
    meterN = 0;
    for (int t = 0; t < NT; t++) { fb.peak[t].store((float)V[t].peak, std::memory_order_relaxed); V[t].peak = 0; }
    fb.mpeak.store((float)mpeak, std::memory_order_relaxed); fb.gr.store((float)laS, std::memory_order_relaxed); fb.duckG.store((float)duckG, std::memory_order_relaxed);
    fb.meterSeq.fetch_add(1, std::memory_order_release); mpeak = 0;
  }
  fb.now.store((double)time, std::memory_order_relaxed);
}

} // namespace hd
