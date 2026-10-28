// Samples for the SAMPLE machine: WAV decoding, the in-memory format, content ids and the embedded form used in
// project files. Main thread / worker threads only; the audio thread only reads SampleData::d through a pointer
// table that the main thread keeps alive (HarderShared.hpp, Core::sendSamples).
//
// Storage: mono float, at the file's own rate (the voice resamples). A sample is quantized once, at import, to the
// form it is stored in (16-bit or 24-bit integer, or 32-bit float for float files), so the data in memory is exactly
// what a saved project reloads: a project renders the same before and after a save.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include "hmath/hmath.h"

namespace hd {

constexpr int NSLOT = 64;                 // sample slots per project (the SMP parameter of the SAMPLE machine)
constexpr double SAMPLE_MAX_SECONDS = 60;  // longer files are cut here at import
constexpr int OVERVIEW_N = 512;            // min/max pairs for the UI waveform

enum SampleFmt : uint8_t { SF_S16 = 0, SF_S24 = 1, SF_F32 = 2 };
inline const char* sampleFmtName(int f) { return f == SF_S16 ? "s16" : f == SF_S24 ? "s24" : "f32"; }
inline int sampleFmtFromName(const std::string& s) { return s == "s16" ? SF_S16 : s == "s24" ? SF_S24 : s == "f32" ? SF_F32 : -1; }

// ---------- base64 ----------
inline std::string b64encode(const uint8_t* d, size_t n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o; o.reserve((n + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < n; i += 3) { const uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2]; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63]; }
  if (i + 1 == n) { const uint32_t v = (uint32_t)d[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
  else if (i + 2 == n) { const uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
  return o;
}
inline bool b64decode(const std::string& s, std::vector<uint8_t>& out) {
  static int8_t D[256]; static bool init = false;
  if (!init) { std::memset(D, -1, sizeof D); const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) D[(uint8_t)T[i]] = (int8_t)i; init = true; }
  out.clear(); out.reserve(s.size() / 4 * 3);
  uint32_t acc = 0; int bits = 0;
  for (unsigned char c : s) {
    if (c == '=' ) break;
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    const int v = D[c]; if (v < 0) return false;
    acc = acc << 6 | (uint32_t)v; bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 255)); }
  }
  return true;
}

// ---------- the sample ----------
struct SampleData {
  std::string id;          // content id: FNV-1a 64 over rate, format, channels, stored bytes and slices (16 hex digits)
  std::string name;        // display name (file name without extension)
  double rate = 48000;     // the file's sample rate
  int len = 0;             // frames
  int fmt = SF_S24;
  int ch = 1;              // 1 mono, 2 stereo (d = left, dR = right)
  std::vector<float> d;    // len frames (mono, or the left channel)
  std::vector<float> dR;   // len frames, stereo only
  std::vector<int> slices; // slice start frames, ascending (transient detection or the WAV's cue points); may be empty
  float peak = 0;
  std::vector<float> ov;   // OVERVIEW_N x (min, max), both channels
  // stored bytes / base64 (built on first use, thread-safe: the export worker serializes projects too)
  const std::string& base64() const { std::call_once(b64once, [this] { const std::vector<uint8_t> b = bytes(); b64 = b64encode(b.data(), b.size()); }); return b64; }
  // stored form: frames of `ch` little-endian values (interleaved L R for stereo)
  std::vector<uint8_t> bytes() const {
    std::vector<uint8_t> o; const int bps = fmt == SF_S16 ? 2 : fmt == SF_S24 ? 3 : 4; o.resize((size_t)len * bps * (ch == 2 ? 2 : 1));
    uint8_t* p = o.data();
    auto put = [&](float x) {
      if (fmt == SF_F32) { uint32_t u; std::memcpy(&u, &x, 4); p[0] = u & 255; p[1] = (u >> 8) & 255; p[2] = (u >> 16) & 255; p[3] = u >> 24; p += 4; }
      else if (fmt == SF_S24) { const int32_t v = (int32_t)std::lround((double)x * 8388608.0); const uint32_t u = (uint32_t)v; p[0] = u & 255; p[1] = (u >> 8) & 255; p[2] = (u >> 16) & 255; p += 3; }
      else { const int32_t v = (int32_t)std::lround((double)x * 32768.0); const uint32_t u = (uint32_t)v; p[0] = u & 255; p[1] = (u >> 8) & 255; p += 2; }
    };
    for (int i = 0; i < len; i++) { put(d[i]); if (ch == 2) put(dR[i]); }
    return o;
  }
  double seconds() const { return rate > 0 ? len / rate : 0; }
  bool stereo() const { return ch == 2 && (int)dR.size() == len; }
  SampleData() {}
  SampleData(const SampleData& o) : id(o.id), name(o.name), rate(o.rate), len(o.len), fmt(o.fmt), ch(o.ch), d(o.d), dR(o.dR), slices(o.slices), peak(o.peak), ov(o.ov) {}
private:
  mutable std::once_flag b64once; mutable std::string b64;
};
using SampleRef = std::shared_ptr<const SampleData>;

inline uint64_t fnv1a(const uint8_t* d, size_t n, uint64_t h = 1469598103934665603ull) { for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 1099511628211ull; } return h; }

// quantize to the storage format, then fill id / peak / overview. Mono samples without slices keep the ids of 1.x.
inline void quantizeTo(std::vector<float>& v, int fmt) {
  for (float& x : v) {
    double y = std::isfinite(x) ? x : 0.0;
    if (fmt == SF_S24) { y = y < -1 ? -1 : y > 8388607.0 / 8388608.0 ? 8388607.0 / 8388608.0 : y; x = (float)(std::lround(y * 8388608.0) / 8388608.0); }
    else if (fmt == SF_S16) { y = y < -1 ? -1 : y > 32767.0 / 32768.0 ? 32767.0 / 32768.0 : y; x = (float)(std::lround(y * 32768.0) / 32768.0); }
    else x = (float)(y < -64 ? -64 : y > 64 ? 64 : y);   // float files may exceed full scale; keep them, within reason
  }
}
inline void finishSample(SampleData& s) {
  s.len = (int)s.d.size();
  if (s.ch == 2 && s.dR.size() != s.d.size()) s.dR.resize(s.d.size(), 0.f);
  if (s.ch != 2) { s.ch = 1; s.dR.clear(); }
  quantizeTo(s.d, s.fmt); if (s.ch == 2) quantizeTo(s.dR, s.fmt);
  { std::vector<int> sl; for (int x : s.slices) if (x >= 0 && x < s.len && (sl.empty() || x > sl.back())) sl.push_back(x); if (sl.size() > 64) sl.resize(64); s.slices = sl; }
  const std::vector<uint8_t> b = s.bytes();
  uint64_t h = 1469598103934665603ull; const uint32_t r = (uint32_t)std::lround(s.rate); const uint8_t hdr[6] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24), (uint8_t)s.fmt, 2 };
  h = fnv1a(hdr, s.ch == 2 ? 6 : 5, h); h = fnv1a(b.data(), b.size(), h);
  if (!s.slices.empty()) { h = fnv1a((const uint8_t*)"SLC", 3, h); for (int x : s.slices) { const uint8_t q[4] = { (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)(x >> 24) }; h = fnv1a(q, 4, h); } }
  char id[20]; std::snprintf(id, sizeof id, "%016llx", (unsigned long long)h); s.id = id;
  s.peak = 0; for (float x : s.d) s.peak = std::max(s.peak, std::fabs(x)); for (float x : s.dR) s.peak = std::max(s.peak, std::fabs(x));
  s.ov.assign(OVERVIEW_N * 2, 0.f);
  if (s.len > 0) for (int k = 0; k < OVERVIEW_N; k++) {
    const int a = (int)((int64_t)k * s.len / OVERVIEW_N), e = std::max(a + 1, (int)((int64_t)(k + 1) * s.len / OVERVIEW_N));
    float mn = 0, mx = 0;
    for (int i = a; i < e && i < s.len; i++) { mn = std::min(mn, s.d[i]); mx = std::max(mx, s.d[i]); if (s.ch == 2) { mn = std::min(mn, s.dR[i]); mx = std::max(mx, s.dR[i]); } }
    s.ov[k * 2] = mn; s.ov[k * 2 + 1] = mx;
  }
}
// a sample with other slice points (same audio, new id)
inline SampleRef withSlices(const SampleData& src, const std::vector<int>& slices) {
  std::shared_ptr<SampleData> s(new SampleData(src)); s->slices = slices; finishSample(*s); return s;
}

// rebuild a sample from its stored form (project files, sound files)
inline SampleRef sampleFromBytes(const std::string& name, double rate, int fmt, const std::vector<uint8_t>& b, std::string* err = nullptr, int ch = 1, const std::vector<int>* slices = nullptr) {
  if (fmt < 0 || !(rate >= 1000 && rate <= 768000) || (ch != 1 && ch != 2)) { if (err) *err = "bad sample format"; return nullptr; }
  const int bps = fmt == SF_S16 ? 2 : fmt == SF_S24 ? 3 : 4; const size_t n = b.size() / ((size_t)bps * ch);
  if (n > (size_t)(SAMPLE_MAX_SECONDS * rate) + 1) { if (err) *err = "sample too long"; return nullptr; }
  std::shared_ptr<SampleData> s(new SampleData()); s->name = name; s->rate = rate; s->fmt = fmt; s->ch = ch; s->d.resize(n); if (ch == 2) s->dR.resize(n);
  const uint8_t* p = b.data();
  auto get = [&]() -> float {
    float r;
    if (fmt == SF_S16) r = (float)((int16_t)(p[0] | p[1] << 8) / 32768.0);
    else if (fmt == SF_S24) { int32_t v = p[0] | p[1] << 8 | p[2] << 16; if (v & 0x800000) v -= 0x1000000; r = (float)(v / 8388608.0); }
    else { uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; std::memcpy(&r, &u, 4); }
    p += bps; return r;
  };
  for (size_t i = 0; i < n; i++) { s->d[i] = get(); if (ch == 2) s->dR[i] = get(); }
  if (slices) s->slices = *slices;
  finishSample(*s); return s;
}

// ---------- WAV (RIFF/WAVE, PCM 8/16/24/32-bit, float 32/64-bit, WAVE_FORMAT_EXTENSIBLE; any channel count) ----------
// Two channels stay stereo; other channel counts are mixed to mono. Cue points ('cue ' chunk) become slice points.
inline SampleRef decodeWav(const std::string& name, const uint8_t* d, size_t n, std::string* err = nullptr, bool* truncated = nullptr) {
  auto fail = [&](const char* m) -> SampleRef { if (err) *err = m; return nullptr; };
  auto u16 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8; };
  auto u32 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
  if (truncated) *truncated = false;
  if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return fail("not a WAV file");
  size_t o = 12; int fmtTag = 0, ch = 0, bits = 0, blockAlign = 0; uint32_t rate = 0; const uint8_t* data = nullptr; size_t dataN = 0;
  std::vector<int> cues;
  while (o + 8 <= n) {
    const uint32_t sz = u32(o + 4); const size_t body = o + 8; size_t avail = n - body; const size_t len = sz < avail ? sz : avail;
    if (!std::memcmp(d + o, "fmt ", 4) && len >= 16) {
      fmtTag = (int)u16(body); ch = (int)u16(body + 2); rate = u32(body + 4); blockAlign = (int)u16(body + 12); bits = (int)u16(body + 14);
      if (fmtTag == 0xFFFE && len >= 40) fmtTag = (int)u16(body + 24);   // extensible: the sub-format GUID starts with the format tag
    } else if (!std::memcmp(d + o, "data", 4)) { data = d + body; dataN = len; }
    else if (!std::memcmp(d + o, "cue ", 4) && len >= 4) {
      const uint32_t k = u32(body);
      for (uint32_t i = 0; i < k && i < 4096 && body + 4 + (size_t)(i + 1) * 24 <= body + len; i++) cues.push_back((int)std::min<uint32_t>(u32(body + 4 + (size_t)i * 24 + 20), 0x7fffffff));   // dwSampleOffset
    }
    o = body + sz + (sz & 1);
    if (sz > n) break;
  }
  if (!rate || !ch || !data) return fail("WAV file without audio data");
  if (fmtTag != 1 && fmtTag != 3) return fail("compressed WAV files are not supported");
  if (fmtTag == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail("unsupported WAV bit depth");
  if (fmtTag == 3 && bits != 32 && bits != 64) return fail("unsupported float WAV");
  if (rate < 1000 || rate > 768000) return fail("unsupported sample rate");
  const int bps = bits / 8; if (blockAlign < bps * ch) blockAlign = bps * ch;
  size_t frames = dataN / (size_t)blockAlign;
  const size_t maxF = (size_t)(SAMPLE_MAX_SECONDS * rate);
  if (frames > maxF) { frames = maxF; if (truncated) *truncated = true; }
  if (!frames) return fail("the WAV file is empty");
  std::shared_ptr<SampleData> s(new SampleData()); s->name = name; s->rate = rate;
  s->fmt = fmtTag == 3 ? SF_F32 : bits <= 16 ? SF_S16 : SF_S24;
  const bool st = ch == 2; s->ch = st ? 2 : 1;
  s->d.resize(frames); if (st) s->dR.resize(frames);
  auto val = [&](const uint8_t* f) -> double {
    double v;
    if (fmtTag == 3) { if (bits == 32) { uint32_t u = (uint32_t)f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16 | (uint32_t)f[3] << 24; float x; std::memcpy(&x, &u, 4); v = x; }
                       else { uint64_t u = 0; for (int k = 7; k >= 0; k--) u = u << 8 | f[k]; double x; std::memcpy(&x, &u, 8); v = x; } }
    else if (bits == 8) v = (f[0] - 128) / 128.0;
    else if (bits == 16) v = (int16_t)(f[0] | f[1] << 8) / 32768.0;
    else if (bits == 24) { int32_t x = f[0] | f[1] << 8 | f[2] << 16; if (x & 0x800000) x -= 0x1000000; v = x / 8388608.0; }
    else { int32_t x = (int32_t)((uint32_t)f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16 | (uint32_t)f[3] << 24); v = x / 2147483648.0; }
    return std::isfinite(v) ? v : 0;
  };
  for (size_t i = 0; i < frames; i++) {
    const uint8_t* f = data + i * blockAlign;
    if (st) { s->d[i] = (float)val(f); s->dR[i] = (float)val(f + bps); continue; }
    double sum = 0; for (int c = 0; c < ch; c++, f += bps) sum += val(f);
    s->d[i] = (float)(sum / ch);
  }
  std::sort(cues.begin(), cues.end()); s->slices = cues;
  finishSample(*s); return s;
}
// how many bytes of a WAV file decodeWav needs (header + at most SAMPLE_MAX_SECONDS of audio), from a prefix of the
// file; 0 if the prefix does not show where the audio starts
inline uint64_t wavBytesNeeded(const uint8_t* d, size_t n, bool* cut = nullptr) {
  if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return 0;
  auto u32 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
  uint64_t o = 12; uint32_t rate = 0, ba = 0, dataSz = 0; uint64_t dataAt = 0; bool fmt = false;
  while (o + 8 <= n) {
    const uint32_t sz = u32((size_t)o + 4);
    if (!std::memcmp(d + o, "fmt ", 4) && o + 8 + 16 <= n) { rate = u32((size_t)o + 12); ba = (uint32_t)(d[o + 20] | d[o + 21] << 8); fmt = true; }
    else if (!std::memcmp(d + o, "data", 4)) { dataAt = o + 8; dataSz = sz; if (fmt) break; }
    o += 8 + (uint64_t)sz + (sz & 1);
  }
  if (!fmt || !dataAt || !rate || !ba) return 0;
  const uint64_t maxData = (uint64_t)(SAMPLE_MAX_SECONDS * rate + 2) * ba;
  if (cut) *cut = dataSz > maxData;
  return dataAt + std::min<uint64_t>(dataSz, maxData);
}
// WAV of a sample in its stored format, mono or stereo; slices are written as cue points (library, other programs)
inline std::vector<uint8_t> encodeWav(const SampleData& s) {
  const std::vector<uint8_t> b = s.bytes(); const uint32_t nc = s.ch == 2 ? 2 : 1, bits = s.fmt == SF_S16 ? 16 : s.fmt == SF_S24 ? 24 : 32, ba = bits / 8 * nc, r = (uint32_t)std::lround(s.rate);
  const uint32_t cueN = (uint32_t)s.slices.size(), cueBytes = cueN ? 12 + 24 * cueN : 0;
  std::vector<uint8_t> o(44 + b.size() + (b.size() & 1) + cueBytes);
  auto ws = [&](size_t p, const char* t) { std::memcpy(&o[p], t, 4); };
  auto w32 = [&](size_t p, uint32_t x) { for (int i = 0; i < 4; i++) o[p + i] = (x >> (8 * i)) & 255; };
  auto w16 = [&](size_t p, uint32_t x) { o[p] = x & 255; o[p + 1] = (x >> 8) & 255; };
  ws(0, "RIFF"); w32(4, (uint32_t)(o.size() - 8)); ws(8, "WAVE"); ws(12, "fmt "); w32(16, 16); w16(20, s.fmt == SF_F32 ? 3 : 1); w16(22, nc);
  w32(24, r); w32(28, r * ba); w16(32, ba); w16(34, bits); ws(36, "data"); w32(40, (uint32_t)b.size());
  if (!b.empty()) std::memcpy(&o[44], b.data(), b.size());
  if (cueN) {
    size_t p = 44 + b.size() + (b.size() & 1); ws(p, "cue "); w32(p + 4, 4 + 24 * cueN); w32(p + 8, cueN); p += 12;
    for (uint32_t i = 0; i < cueN; i++, p += 24) { w32(p, i + 1); w32(p + 4, (uint32_t)s.slices[i]); ws(p + 8, "data"); w32(p + 12, 0); w32(p + 16, 0); w32(p + 20, (uint32_t)s.slices[i]); }
  }
  return o;
}

// detection uses the private libm too, so the slice points do not depend on the platform's C runtime
#ifndef HD_USE_CRT_MATH
inline double slcLog10(double x) { return hm_log10(x); }
#else
inline double slcLog10(double x) { return std::log10(x); }
#endif
inline double clampSens(double v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
// ---------- transient detection (slice points of a break) ----------
// Onsets = rises of the short-time energy of the first difference (it weights the attack of a hit over the body of
// the previous one) above a level-dependent threshold, at least 50 ms apart; each one is moved back to just before
// the attack. sens 0..100 (more = more slices). At most maxN, the strongest kept.
inline std::vector<int> detectSlices(const SampleData& s, double sens, int maxN = 64) {
  std::vector<int> out; const int n = s.len; if (n < 64) return out;
  const int H = std::max(16, (int)std::lround(s.rate * 0.004)), W = 2 * H, frames = (n - W) / H + 1; if (frames < 3) { out.push_back(0); return out; }
  auto m = [&](int i) { return s.ch == 2 ? 0.5 * ((double)s.d[i] + s.dR[i]) : (double)s.d[i]; };
  std::vector<double> e(frames), lv(frames); double emax = 1e-12;
  for (int k = 0; k < frames; k++) {
    double a = 0, b = 0; const int i0 = k * H;
    for (int i = i0; i < i0 + W; i++) { const double x = m(i), dx = x - (i > 0 ? m(i - 1) : 0); a += dx * dx; b += x * x; }
    e[k] = 10 * slcLog10(a / W + 1e-12); lv[k] = b / W; emax = std::max(emax, lv[k]);
  }
  const double rise = 12 - clampSens(sens) * 0.1, floorDb = 10 * slcLog10(emax) - 45;   // sens 0: 12 dB rise, 50: 7 dB, 100: 2 dB
  const int gap = std::max(1, (int)std::lround(s.rate * 0.05 / H));
  struct On { int k; double r; }; std::vector<On> on;
  for (int k = 2; k + 1 < frames; k++) {
    // frame k-2 ends where frame k starts (entirely before an onset in frame k); frame k+1 is entirely after it
    const double r = std::max(e[k], e[k + 1]) - e[k - 2];
    if (r >= rise && 10 * slcLog10(lv[k] + 1e-12) > floorDb) {
      if (!on.empty() && k - on.back().k < gap) { if (r > on.back().r && k - on.back().k < 2) on.back() = { k, r }; continue; }
      on.push_back({ k, r });
    }
  }
  if (on.size() > (size_t)maxN) { std::sort(on.begin(), on.end(), [](const On& a, const On& b) { return a.r > b.r; }); on.resize(maxN); std::sort(on.begin(), on.end(), [](const On& a, const On& b) { return a.k < b.k; }); }
  for (const On& o2 : on) {
    // the attack: where the energy of the next 64 samples over the previous 64 jumps most (also on the tail of a
    // louder hit), 1 ms earlier
    const int i0 = std::max(65, (o2.k - 2) * H), i1 = std::min(n - 65, (o2.k + 2) * H + H);
    int at = std::max(0, o2.k * H); double bestR = -1;
    if (i1 > i0) {
      std::vector<double> cs((size_t)(i1 - i0 + 129), 0.0);
      for (int i = i0 - 64; i < i1 + 64; i++) { const double x = m(i); cs[(size_t)(i - (i0 - 64) + 1)] = cs[(size_t)(i - (i0 - 64))] + x * x; }
      for (int i = i0; i < i1; i++) {
        const size_t c = (size_t)(i - (i0 - 64)); const double before = cs[c] - cs[c - 64], after = cs[c + 64] - cs[c];
        const double r = (after + 1e-12) / (before + 1e-9 * 64 + 1e-12); if (r > bestR) { bestR = r; at = i; }
      }
    }
    at = std::max(0, at - (int)std::lround(s.rate * 0.001));
    if (!out.empty() && at <= out.back()) continue;
    out.push_back(at);
  }
  // sound before the first onset (an upbeat, a hit already at 0) is a slice of its own
  if (out.empty() || (out[0] > (int)(s.rate * 0.02) && 10 * slcLog10(lv[0] + 1e-12) > floorDb)) out.insert(out.begin(), 0);
  else if (!out.empty() && out[0] <= (int)(s.rate * 0.02)) out[0] = 0;
  if ((int)out.size() > maxN) out.resize(maxN);
  return out;
}

// every sample seen this session, by id: undo snapshots and the library refer to samples by id only
struct SampleCache {
  std::map<std::string, SampleRef> m;
  SampleRef find(const std::string& id) const { auto it = m.find(id); return it == m.end() ? nullptr : it->second; }
  SampleRef add(const SampleRef& s) { if (!s) return s; auto it = m.find(s->id); if (it != m.end()) return it->second; m[s->id] = s; return s; }
};

} // namespace hd
