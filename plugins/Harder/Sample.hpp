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
  std::string id;          // content id: FNV-1a 64 over rate, format and stored bytes (16 hex digits)
  std::string name;        // display name (file name without extension)
  double rate = 48000;     // the file's sample rate
  int len = 0;             // frames
  int fmt = SF_S24;
  std::vector<float> d;    // len frames, mono
  float peak = 0;
  std::vector<float> ov;   // OVERVIEW_N x (min, max)
  // stored bytes / base64 (built on first use, thread-safe: the export worker serializes projects too)
  const std::string& base64() const { std::call_once(b64once, [this] { const std::vector<uint8_t> b = bytes(); b64 = b64encode(b.data(), b.size()); }); return b64; }
  std::vector<uint8_t> bytes() const {
    std::vector<uint8_t> o; const int bps = fmt == SF_S16 ? 2 : fmt == SF_S24 ? 3 : 4; o.resize((size_t)len * bps);
    uint8_t* p = o.data();
    for (int i = 0; i < len; i++) {
      if (fmt == SF_F32) { uint32_t u; std::memcpy(&u, &d[i], 4); p[0] = u & 255; p[1] = (u >> 8) & 255; p[2] = (u >> 16) & 255; p[3] = u >> 24; p += 4; }
      else if (fmt == SF_S24) { const int32_t v = (int32_t)std::lround((double)d[i] * 8388608.0); const uint32_t u = (uint32_t)v; p[0] = u & 255; p[1] = (u >> 8) & 255; p[2] = (u >> 16) & 255; p += 3; }
      else { const int32_t v = (int32_t)std::lround((double)d[i] * 32768.0); const uint32_t u = (uint32_t)v; p[0] = u & 255; p[1] = (u >> 8) & 255; p += 2; }
    }
    return o;
  }
  double seconds() const { return rate > 0 ? len / rate : 0; }
private:
  mutable std::once_flag b64once; mutable std::string b64;
};
using SampleRef = std::shared_ptr<const SampleData>;

inline uint64_t fnv1a(const uint8_t* d, size_t n, uint64_t h = 1469598103934665603ull) { for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 1099511628211ull; } return h; }

// quantize to the storage format, then fill id / peak / overview
inline void finishSample(SampleData& s) {
  s.len = (int)s.d.size();
  for (float& x : s.d) {
    double v = std::isfinite(x) ? x : 0.0;
    if (s.fmt == SF_S24) { v = v < -1 ? -1 : v > 8388607.0 / 8388608.0 ? 8388607.0 / 8388608.0 : v; x = (float)(std::lround(v * 8388608.0) / 8388608.0); }
    else if (s.fmt == SF_S16) { v = v < -1 ? -1 : v > 32767.0 / 32768.0 ? 32767.0 / 32768.0 : v; x = (float)(std::lround(v * 32768.0) / 32768.0); }
    else x = (float)(v < -64 ? -64 : v > 64 ? 64 : v);   // float files may exceed full scale; keep them, within reason
  }
  const std::vector<uint8_t> b = s.bytes();
  uint64_t h = 1469598103934665603ull; const uint32_t r = (uint32_t)std::lround(s.rate); const uint8_t hdr[5] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24), (uint8_t)s.fmt };
  h = fnv1a(hdr, 5, h); h = fnv1a(b.data(), b.size(), h);
  char id[20]; std::snprintf(id, sizeof id, "%016llx", (unsigned long long)h); s.id = id;
  s.peak = 0; for (float x : s.d) s.peak = std::max(s.peak, std::fabs(x));
  s.ov.assign(OVERVIEW_N * 2, 0.f);
  if (s.len > 0) for (int k = 0; k < OVERVIEW_N; k++) {
    const int a = (int)((int64_t)k * s.len / OVERVIEW_N), e = std::max(a + 1, (int)((int64_t)(k + 1) * s.len / OVERVIEW_N));
    float mn = 0, mx = 0; for (int i = a; i < e && i < s.len; i++) { mn = std::min(mn, s.d[i]); mx = std::max(mx, s.d[i]); }
    s.ov[k * 2] = mn; s.ov[k * 2 + 1] = mx;
  }
}

// rebuild a sample from its stored form (project files, sound files)
inline SampleRef sampleFromBytes(const std::string& name, double rate, int fmt, const std::vector<uint8_t>& b, std::string* err = nullptr) {
  if (fmt < 0 || !(rate >= 1000 && rate <= 768000)) { if (err) *err = "bad sample format"; return nullptr; }
  const int bps = fmt == SF_S16 ? 2 : fmt == SF_S24 ? 3 : 4; const size_t n = b.size() / bps;
  if (n > (size_t)(SAMPLE_MAX_SECONDS * rate) + 1) { if (err) *err = "sample too long"; return nullptr; }
  std::shared_ptr<SampleData> s(new SampleData()); s->name = name; s->rate = rate; s->fmt = fmt; s->d.resize(n);
  const uint8_t* p = b.data();
  for (size_t i = 0; i < n; i++, p += bps) {
    if (fmt == SF_S16) s->d[i] = (float)((int16_t)(p[0] | p[1] << 8) / 32768.0);
    else if (fmt == SF_S24) { int32_t v = p[0] | p[1] << 8 | p[2] << 16; if (v & 0x800000) v -= 0x1000000; s->d[i] = (float)(v / 8388608.0); }
    else { uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; float f; std::memcpy(&f, &u, 4); s->d[i] = f; }
  }
  finishSample(*s); return s;
}

// ---------- WAV (RIFF/WAVE, PCM 8/16/24/32-bit, float 32/64-bit, WAVE_FORMAT_EXTENSIBLE; any channel count) ----------
inline SampleRef decodeWav(const std::string& name, const uint8_t* d, size_t n, std::string* err = nullptr, bool* truncated = nullptr) {
  auto fail = [&](const char* m) -> SampleRef { if (err) *err = m; return nullptr; };
  auto u16 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8; };
  auto u32 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
  if (truncated) *truncated = false;
  if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return fail("not a WAV file");
  size_t o = 12; int fmtTag = 0, ch = 0, bits = 0, blockAlign = 0; uint32_t rate = 0; const uint8_t* data = nullptr; size_t dataN = 0;
  while (o + 8 <= n) {
    const uint32_t sz = u32(o + 4); const size_t body = o + 8; size_t avail = n - body; const size_t len = sz < avail ? sz : avail;
    if (!std::memcmp(d + o, "fmt ", 4) && len >= 16) {
      fmtTag = (int)u16(body); ch = (int)u16(body + 2); rate = u32(body + 4); blockAlign = (int)u16(body + 12); bits = (int)u16(body + 14);
      if (fmtTag == 0xFFFE && len >= 40) fmtTag = (int)u16(body + 24);   // extensible: the sub-format GUID starts with the format tag
    } else if (!std::memcmp(d + o, "data", 4)) { data = d + body; dataN = len; }
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
  s->d.resize(frames);
  for (size_t i = 0; i < frames; i++) {
    const uint8_t* f = data + i * blockAlign; double sum = 0;
    for (int c = 0; c < ch; c++, f += bps) {
      double v;
      if (fmtTag == 3) { if (bits == 32) { uint32_t u = (uint32_t)f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16 | (uint32_t)f[3] << 24; float x; std::memcpy(&x, &u, 4); v = x; }
                         else { uint64_t u = 0; for (int k = 7; k >= 0; k--) u = u << 8 | f[k]; double x; std::memcpy(&x, &u, 8); v = x; } }
      else if (bits == 8) v = (f[0] - 128) / 128.0;
      else if (bits == 16) v = (int16_t)(f[0] | f[1] << 8) / 32768.0;
      else if (bits == 24) { int32_t x = f[0] | f[1] << 8 | f[2] << 16; if (x & 0x800000) x -= 0x1000000; v = x / 8388608.0; }
      else { int32_t x = (int32_t)((uint32_t)f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16 | (uint32_t)f[3] << 24); v = x / 2147483648.0; }
      sum += std::isfinite(v) ? v : 0;
    }
    s->d[i] = (float)(sum / ch);   // mono: the voice path (filter, drive, pan) is mono, like Elektron sample tracks
  }
  finishSample(*s); return s;
}
// mono WAV of a sample in its stored format (saving to the library)
inline std::vector<uint8_t> encodeWav(const SampleData& s) {
  const std::vector<uint8_t> b = s.bytes(); const uint32_t bits = s.fmt == SF_S16 ? 16 : s.fmt == SF_S24 ? 24 : 32, ba = bits / 8, r = (uint32_t)std::lround(s.rate);
  std::vector<uint8_t> o(44 + b.size());
  auto ws = [&](size_t p, const char* t) { std::memcpy(&o[p], t, 4); };
  auto w32 = [&](size_t p, uint32_t x) { for (int i = 0; i < 4; i++) o[p + i] = (x >> (8 * i)) & 255; };
  auto w16 = [&](size_t p, uint32_t x) { o[p] = x & 255; o[p + 1] = (x >> 8) & 255; };
  ws(0, "RIFF"); w32(4, (uint32_t)(36 + b.size())); ws(8, "WAVE"); ws(12, "fmt "); w32(16, 16); w16(20, s.fmt == SF_F32 ? 3 : 1); w16(22, 1);
  w32(24, r); w32(28, r * ba); w16(32, ba); w16(34, bits); ws(36, "data"); w32(40, (uint32_t)b.size());
  if (!b.empty()) std::memcpy(&o[44], b.data(), b.size());
  return o;
}

// every sample seen this session, by id: undo snapshots and the library refer to samples by id only
struct SampleCache {
  std::map<std::string, SampleRef> m;
  SampleRef find(const std::string& id) const { auto it = m.find(id); return it == m.end() ? nullptr : it->second; }
  SampleRef add(const SampleRef& s) { if (!s) return s; auto it = m.find(s->id); if (it != m.end()) return it->second; m[s->id] = s; return s; }
};

} // namespace hd
