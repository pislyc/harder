// Parameters exposed to the host (automation in Live). Everything else lives in the project state.
#pragma once
#include "Params.hpp"

namespace hd {

// per-track slots: SRC 1-8, then a fixed set of common parameters
static const int TP_IDX[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 18, 19, 20, 21, 24, 25, 33 };
constexpr int NTP = sizeof(TP_IDX) / sizeof(TP_IDX[0]);   // 18
static const int MP_IDX[] = { 1, 5, 9, 13, 16, 17, 18, 19, 26, 27 };
constexpr int NMP = sizeof(MP_IDX) / sizeof(MP_IDX[0]);   // 10
enum : int {
  HP_TRACK0 = 0,
  HP_MASTER0 = NT * NTP,            // 144
  HP_MUTE0 = HP_MASTER0 + NMP,      // 154
  HP_FILL = HP_MUTE0 + NT,          // 162
  HP_PATTERN = HP_FILL + 1,         // 163
  HP_COUNT = HP_PATTERN + 1         // 164
};

struct HostParamInfo { int kind; int tr; int i; };   // kind 0 track, 1 master, 2 mute, 3 fill, 4 pattern
inline HostParamInfo hostParamInfo(int idx) {
  if (idx < HP_MASTER0) return { 0, idx / NTP, TP_IDX[idx % NTP] };
  if (idx < HP_MUTE0) return { 1, 8, MP_IDX[idx - HP_MASTER0] };
  if (idx < HP_FILL) return { 2, idx - HP_MUTE0, 0 };
  if (idx == HP_FILL) return { 3, 0, 0 };
  return { 4, 0, 0 };
}
inline int hostIndexForTrack(int tr, int i) { for (int k = 0; k < NTP; k++) if (TP_IDX[k] == i) return HP_TRACK0 + tr * NTP + k; return -1; }
inline int hostIndexForMaster(int i) { for (int k = 0; k < NMP; k++) if (MP_IDX[k] == i) return HP_MASTER0 + k; return -1; }

// Host range for a track slot. SRC slots are 0..127 whatever the machine; engine value = machine range mapped linearly.
inline void hostRange(int idx, double& mn, double& mx, double& def) {
  const HostParamInfo h = hostParamInfo(idx);
  if (h.kind == 0) {
    if (h.i < 8) { mn = 0; mx = 127; def = 64; return; }
    const PDef& d = T().common[h.i]; mn = d.min; mx = d.max; def = d.def; return;
  }
  if (h.kind == 1) { const PDef& d = T().master[h.i]; mn = d.min; mx = d.max; def = d.def; return; }
  if (h.kind == 2 || h.kind == 3) { mn = 0; mx = 1; def = 0; return; }
  mn = 1; mx = 8; def = 1;
}
inline double srcToEngine(int mid, int i, double h) {
  const PDef& d = T().mach[mid][i]; double v = d.min + h * (d.max - d.min) / 127.0;   // exact for 0..127 and -64..63
  if (d.max - d.min < 20) v = jsround(v); return v;
}
inline double srcToHost(int mid, int i, double v) { const PDef& d = T().mach[mid][i]; return (v - d.min) * 127.0 / (d.max - d.min); }

} // namespace hd
