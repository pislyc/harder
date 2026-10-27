// HARDER parameter definitions. Port of the tables at the top of engine.js (StrobeCore).
// Shared by the DSP engine (ranges, defaults) and the UI (names, value formatting).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "hmath/hmath.h"

namespace hd {

constexpr int NP = 56;      // per-track parameter slots (7 pages of 8)
constexpr int NM = 32;      // master slots (DLY REV MIX DUCK)
constexpr int NT = 8;       // sound tracks
constexpr int NTR = 9;      // tracks incl. FX track
constexpr int NSTEP = 64;
constexpr int NPAT = 8;
constexpr int NMACH = 10;

// all transcendental math in the DSP goes through a private copy of musl's libm (hmath/): the speed and the
// results then do not depend on the host C runtime (msvcrt's exp/pow are 5-10x slower than these).
#ifndef HD_USE_CRT_MATH
inline double msin(double x) { return hm_sin(x); }
inline double mcos(double x) { return hm_cos(x); }
inline double mtan(double x) { return hm_tan(x); }
inline double mexp(double x) { return hm_exp(x); }
inline double mpow(double x, double y) { return hm_pow(x, y); }
inline double mlog10(double x) { return hm_log10(x); }
inline double mhypot(double x, double y) { return hm_hypot(x, y); }
#else   // comparison builds only
inline double msin(double x) { return std::sin(x); }
inline double mcos(double x) { return std::cos(x); }
inline double mtan(double x) { return std::tan(x); }
inline double mexp(double x) { return std::exp(x); }
inline double mpow(double x, double y) { return std::pow(x, y); }
inline double mlog10(double x) { return std::log10(x); }
inline double mhypot(double x, double y) { return std::hypot(x, y); }
#endif
inline double expm(double v, double lo, double hi) { return lo * mpow(hi / lo, v / 127.0); }
inline double jsround(double x) { return std::floor(x + 0.5); }   // Math.round

enum MachId { M_SUB, M_FMK, M_SNR, M_CLP, M_MTL, M_MDL, M_ZAP, M_GLT, M_DUO, M_SMP };
static const char* const MACH_IDS[NMACH] = { "SUB", "FMK", "SNR", "CLP", "MTL", "MDL", "ZAP", "GLT", "DUO", "SMP" };
static const char* const MACH_NAMES[NMACH] = { "BD SUB", "BD FM", "SD TONE", "CP GRID", "HH METAL", "PT MODAL", "FX ZAP", "NZ GLITCH", "PT DUO", "SAMPLE" };
inline int machineIndex(const char* id) { for (int i = 0; i < NMACH; i++) if (!std::strcmp(id, MACH_IDS[i])) return i; return -1; }

static const char* const COND[] = { "\xe2\x80\x94", "FILL", "!FIL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4" };
constexpr int NCOND = 18;
static const int COND_A[NCOND] = { 0,0,0,0,0,0,0,0,0, 1,2,1,2,3,1,2,3,4 };
static const int COND_B[NCOND] = { 0,0,0,0,0,0,0,0,0, 2,2,3,3,3,4,4,4,4 };
static const char* const RTRG[] = { "OFF", "1/8", "1/16", "1/32", "1/64", "1/12", "1/24", "1/48" };
static const double RTRG_ST[] = { 0, 2, 1, 0.5, 0.25, 4.0 / 3, 2.0 / 3, 1.0 / 3 };
static const char* const RLEN[] = { "1", "2", "3", "4", "8", "16" };
static const double RLEN_ST[] = { 1, 2, 3, 4, 8, 16 };
static const char* const LFO_SPD[] = { "64", "32", "16", "12", "8", "6", "4", "3", "2", "3/2", "1", "3/4", "1/2", "1/4" };
static const double LFO_SPD_ST[] = { 64, 32, 16, 12, 8, 6, 4, 3, 2, 1.5, 1, 0.75, 0.5, 0.25 };
static const char* const LFO_WAVE[] = { "SIN", "TRI", "SQR", "SAW", "RAMP", "RND", "EXP" };
static const char* const LFO_MODE[] = { "FREE", "TRIG", "ONE", "HALF" };
static const char* const LFO_DEST[] = { "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "FREQ", "RESO", "PTCH", "PAN", "VOL", "CTUN", "SRR", "DRV" };
static const int LFO_DEST_IDX[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 40, 20, 21, 29, 27, 19 };
static const char* const NOTE_NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const DLY_T[] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2" };
static const double DLY_ST[] = { 0.5, 2.0 / 3, 1, 4.0 / 3, 1.5, 2, 8.0 / 3, 3, 4, 8 };
static const char* const DUCK_SRC[] = { "OFF", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "FX" };
static const char* const DUCK_LEN[] = { "AUTO", "1/32", "1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "1/1" };
static const double DUCK_LEN_ST[] = { 0, 0.5, 1, 2, 3, 4, 6, 8, 16 };
static const char* const SPEEDS[] = { "1/8", "1/4", "1/2", "3/4", "1", "3/2", "2" };
static const double SPEED_X[] = { 0.125, 0.25, 0.5, 0.75, 1, 1.5, 2 };
static const char* const PAGES[] = { "TRIG", "STEP", "SRC", "FLTR", "AMP", "FX", "LFO" };
static const int PAGE_BASE[] = { 40, 48, 0, 8, 16, 24, 32 };
static const char* const MASTER_PAGES[] = { "DLY", "REV", "MIX", "DUCK" };
inline bool STRUCT(int i) { return i >= 42; }

// formatter ids
enum FmtId {
  FM_RAW, FM_BI, FM_HZ, FM_T, FM_PCT, FM_SEMI, FM_LIST,
  FM_FMK_RATI, FM_SNR_SPRD, FM_CLP_CNT, FM_MTL_SPRD, FM_MDL_MATR, FM_ZAP_SWEP, FM_ZAP_RATI, FM_GLT_SWEP, FM_DUO_RATI, FM_DUO_WAVE,
  FM_TYPE, FM_T0, FM_DECINF, FM_PAN, FM_BITS, FM_SRR, FM_CTUN, FM_LFOSPD, FM_DEG, FM_PCTRAW, FM_MICR, FM_RLEN,
  FM_DJF, FM_SHAP, FM_XOVR, FM_PREMS,
  FM_SEMIB, FM_CENT, FM_SLOT, FM_PCT1, FM_SLCE,
};

struct PDef {
  const char* n = nullptr;
  double min = 0, max = 0, def = 0;
  int fmt = FM_RAW;
  double lo = 0, hi = 0;               // for HZ/T/SEMI/T0
  const char* const* list = nullptr;   // for LIST
  int nlist = 0;
  bool lock = true, step = false;
  bool valid() const { return n != nullptr; }
};

inline PDef P(const char* n, double mn, double mx, double df, int fmt = FM_RAW, double lo = 0, double hi = 0) {
  PDef d; d.n = n; d.min = mn; d.max = mx; d.def = df; d.fmt = fmt; d.lo = lo; d.hi = hi; return d;
}
inline PDef PL(const char* n, double mn, double mx, double df, const char* const* list, int nlist, bool lock = true, bool step = false) {
  PDef d = P(n, mn, mx, df, FM_LIST); d.list = list; d.nlist = nlist; d.lock = lock; d.step = step; return d;
}

struct Tables {
  PDef mach[NMACH][8];
  PDef common[NP];
  PDef smpx[NP];   // SAMPLE machine only: slots that are free for the other machines (STEP page 51, 52: slices)
  PDef master[NM];
  Tables() {
    PDef (&m)[NMACH][8] = mach;
    // SUB
    m[M_SUB][0] = P("TUNE", 0, 127, 40, FM_HZ, 25, 160); m[M_SUB][1] = P("SWEP", 0, 127, 60, FM_SEMI, 60); m[M_SUB][2] = P("BEND", 0, 127, 44, FM_T, 0.002, 0.4);
    m[M_SUB][3] = P("DEC", 0, 127, 62, FM_T, 0.04, 4); m[M_SUB][4] = P("CLIK", 0, 127, 50, FM_PCT); m[M_SUB][5] = P("CTON", 0, 127, 64, FM_HZ, 300, 9000);
    m[M_SUB][6] = P("FOLD", 0, 127, 0, FM_PCT); m[M_SUB][7] = P("HARM", 0, 127, 0, FM_PCT);
    // FMK
    m[M_FMK][0] = P("TUNE", 0, 127, 38, FM_HZ, 25, 160); m[M_FMK][1] = P("SWEP", 0, 127, 52, FM_SEMI, 60); m[M_FMK][2] = P("BEND", 0, 127, 36, FM_T, 0.002, 0.4);
    m[M_FMK][3] = P("DEC", 0, 127, 58, FM_T, 0.04, 4); m[M_FMK][4] = P("RATI", 0, 127, 17, FM_FMK_RATI); m[M_FMK][5] = P("INDX", 0, 127, 44, FM_PCT);
    m[M_FMK][6] = P("IDEC", 0, 127, 34, FM_T, 0.004, 1.5); m[M_FMK][7] = P("FDBK", 0, 127, 0, FM_PCT);
    // SNR
    m[M_SNR][0] = P("TUNE", 0, 127, 50, FM_HZ, 80, 600); m[M_SNR][1] = P("SPRD", 0, 127, 50, FM_SNR_SPRD); m[M_SNR][2] = P("TDEC", 0, 127, 44, FM_T, 0.02, 1);
    m[M_SNR][3] = P("NDEC", 0, 127, 56, FM_T, 0.02, 1.2); m[M_SNR][4] = P("NOIS", 0, 127, 76, FM_PCT); m[M_SNR][5] = P("NTON", 0, 127, 78, FM_HZ, 800, 12000);
    m[M_SNR][6] = P("SNAP", 0, 127, 60, FM_PCT); m[M_SNR][7] = P("SWEP", 0, 127, 30, FM_SEMI, 24);
    // CLP
    m[M_CLP][0] = P("TONE", 0, 127, 70, FM_HZ, 400, 8000); m[M_CLP][1] = P("RESO", 0, 127, 36, FM_PCT); m[M_CLP][2] = P("CNT", 0, 127, 50, FM_CLP_CNT);
    m[M_CLP][3] = P("SPAC", 0, 127, 60, FM_T, 0.002, 0.04); m[M_CLP][4] = P("DEC", 0, 127, 52, FM_T, 0.03, 2); m[M_CLP][5] = P("SCTR", 0, 127, 12, FM_PCT);
    m[M_CLP][6] = P("RING", 0, 127, 0, FM_PCT); m[M_CLP][7] = P("RFRQ", 0, 127, 64, FM_HZ, 50, 4000);
    // MTL
    m[M_MTL][0] = P("TUNE", 0, 127, 64, FM_HZ, 100, 900); m[M_MTL][1] = P("DEC", 0, 127, 30, FM_T, 0.01, 2.5); m[M_MTL][2] = P("COLR", 0, 127, 88, FM_HZ, 1500, 16000);
    m[M_MTL][3] = P("RESO", 0, 127, 30, FM_PCT); m[M_MTL][4] = P("SPRD", 0, 127, 64, FM_MTL_SPRD); m[M_MTL][5] = P("NOIS", 0, 127, 20, FM_PCT);
    m[M_MTL][6] = P("RING", 0, 127, 0, FM_PCT); m[M_MTL][7] = P("BITE", 0, 127, 40, FM_PCT);
    // MDL
    m[M_MDL][0] = P("TUNE", 0, 127, 50, FM_HZ, 40, 2400); m[M_MDL][1] = P("DEC", 0, 127, 50, FM_T, 0.02, 4); m[M_MDL][2] = P("MATR", 0, 127, 30, FM_MDL_MATR);
    m[M_MDL][3] = P("DAMP", 0, 127, 50, FM_PCT); m[M_MDL][4] = P("STRK", 0, 127, 64, FM_HZ, 300, 16000); m[M_MDL][5] = P("POS", 0, 127, 30, FM_PCT);
    m[M_MDL][6] = P("SWEP", 0, 127, 8, FM_SEMI, 24); m[M_MDL][7] = P("NOIS", 0, 127, 20, FM_T, 0.0005, 0.03);
    // ZAP
    m[M_ZAP][0] = P("TUNE", 0, 127, 70, FM_HZ, 40, 5000); m[M_ZAP][1] = P("SWEP", -64, 63, 40, FM_ZAP_SWEP); m[M_ZAP][2] = P("STIM", 0, 127, 40, FM_T, 0.002, 0.5);
    m[M_ZAP][3] = P("DEC", 0, 127, 40, FM_T, 0.01, 2); m[M_ZAP][4] = P("RATI", 0, 127, 64, FM_ZAP_RATI); m[M_ZAP][5] = P("INDX", 0, 127, 50, FM_PCT);
    m[M_ZAP][6] = P("IDEC", 0, 127, 50, FM_T, 0.004, 1.5); m[M_ZAP][7] = P("FDBK", 0, 127, 20, FM_PCT);
    // GLT
    static const char* const GSRC[] = { "WHIT", "DUST", "BYTE", "LFSR" };
    m[M_GLT][0] = PL("SRC", 0, 3, 3, GSRC, 4); m[M_GLT][1] = P("RATE", 0, 127, 96, FM_HZ, 200, 48000); m[M_GLT][2] = P("ALGO", 0, 127, 40, FM_RAW);
    m[M_GLT][3] = P("TONE", 0, 127, 80, FM_HZ, 100, 16000); m[M_GLT][4] = P("RESO", 0, 127, 60, FM_PCT); m[M_GLT][5] = P("SWEP", -64, 63, 30, FM_GLT_SWEP);
    m[M_GLT][6] = P("STIM", 0, 127, 40, FM_T, 0.002, 1); m[M_GLT][7] = P("DEC", 0, 127, 40, FM_T, 0.01, 3);
    // DUO
    m[M_DUO][0] = P("TUNE", 0, 127, 76, FM_HZ, 40, 4000); m[M_DUO][1] = P("RATI", 0, 127, 21, FM_DUO_RATI); m[M_DUO][2] = P("WAVE", 0, 127, 127, FM_DUO_WAVE);
    m[M_DUO][3] = P("DEC", 0, 127, 50, FM_T, 0.01, 3); m[M_DUO][4] = P("BPF", 0, 127, 88, FM_HZ, 200, 12000); m[M_DUO][5] = P("RESO", 0, 127, 34, FM_PCT);
    m[M_DUO][6] = P("SWEP", 0, 127, 0, FM_SEMI, 24); m[M_DUO][7] = P("CLIK", 0, 127, 20, FM_PCT);
    // SMP (sample playback; not in the web version)
    static const char* const SMODE[] = { "FWD", "REV", "LOOP", "RLOOP" };
    m[M_SMP][0] = P("TUNE", -24, 24, 0, FM_SEMIB); m[M_SMP][1] = P("FINE", -64, 63, 0, FM_CENT); m[M_SMP][2] = P("SMP", 0, 63, 0, FM_SLOT);
    m[M_SMP][3] = P("STRT", 0, 127, 0, FM_PCT1); m[M_SMP][4] = P("END", 0, 127, 127, FM_PCT1); m[M_SMP][5] = PL("MODE", 0, 3, 0, SMODE, 4);
    m[M_SMP][6] = P("LOOP", 0, 127, 0, FM_PCT1); m[M_SMP][7] = P("LEV", 0, 127, 100, FM_RAW);
    static const char* const SLCS[] = { "OFF", "8", "16", "32", "64", "AUTO" };
    smpx[51] = PL("SLCS", 0, 5, 0, SLCS, 6); smpx[52] = P("SLCE", 0, 64, 0, FM_SLCE);

    PDef* c = common;
    static const char* const CHOK[] = { "OFF", "A", "B", "C", "D" };
    static const char* const SLID[] = { "OFF", "ON" };
    c[8] = P("FREQ", 0, 127, 127, FM_HZ, 20, 20000); c[9] = P("RESO", 0, 127, 0, FM_PCT);
    c[10] = P("TYPE", 0, 127, 0, FM_TYPE); c[11] = P("ENV", -64, 63, 0, FM_BI); c[12] = P("FATK", 0, 127, 0, FM_T0, 0.001, 2);
    c[13] = P("FDEC", 0, 127, 40, FM_T, 0.005, 4); c[14] = P("KTRK", 0, 127, 0, FM_PCT);
    c[16] = P("ATK", 0, 127, 0, FM_T0, 0.0005, 2); c[17] = P("HOLD", 0, 127, 0, FM_T0, 0.001, 2);
    c[18] = P("DEC", 0, 127, 127, FM_DECINF, 0.005, 8); c[19] = P("DRV", 0, 127, 0, FM_PCT);
    c[20] = P("PAN", -64, 63, 0, FM_PAN); c[21] = P("VOL", 0, 127, 100, FM_RAW);
    c[22] = PL("CHOK", 0, 4, 0, CHOK, 5, false); c[23] = P("DUCK", 0, 127, 0, FM_PCT);
    c[24] = P("DLY", 0, 127, 0, FM_RAW); c[25] = P("REV", 0, 127, 0, FM_RAW); c[26] = P("BITS", 1, 16, 16, FM_BITS);
    c[27] = P("SRR", 0, 127, 0, FM_SRR); c[28] = P("COMB", 0, 127, 0, FM_PCT);
    c[29] = P("CTUN", 24, 108, 60, FM_CTUN); c[30] = P("CFB", 0, 127, 96, FM_PCT);
    c[32] = P("SPD", 0, 13, 4, FM_LFOSPD); c[33] = P("DEP", -64, 63, 0, FM_BI);
    c[34] = PL("DEST", 0, 15, 8, LFO_DEST, 16); c[35] = PL("WAVE", 0, 6, 0, LFO_WAVE, 7);
    c[36] = PL("MODE", 0, 3, 1, LFO_MODE, 4); c[37] = P("PHAS", 0, 127, 0, FM_DEG);
    c[40] = P("NOTE", -24, 24, 0, FM_BI); c[41] = P("VEL", 1, 127, 100, FM_RAW); c[42] = P("PROB", 0, 100, 100, FM_PCTRAW);
    c[43] = PL("COND", 0, NCOND - 1, 0, COND, NCOND); c[44] = P("MICR", -23, 23, 0, FM_MICR);
    c[45] = PL("RTRG", 0, 7, 0, RTRG, 8); c[46] = P("RFAD", -64, 63, 0, FM_BI); c[47] = P("RLEN", 0, 5, 0, FM_RLEN);
    c[48] = PL("SLID", 0, 1, 0, SLID, 2, true, true); c[49] = PL("LCND", 0, NCOND - 1, 0, COND, NCOND, true, true);
    c[50] = P("LPRB", 0, 100, 100, FM_PCTRAW); c[50].step = true;

    PDef* M = master;
    M[0] = PL("TIME", 0, 9, 7, DLY_T, 10, false); M[1] = P("FDBK", 0, 127, 64, FM_PCT); M[2] = P("HPF", 0, 127, 30, FM_HZ, 20, 4000);
    M[3] = P("LPF", 0, 127, 92, FM_HZ, 400, 20000); M[4] = P("WIDE", 0, 127, 100, FM_PCT); M[5] = P("RTN", 0, 127, 100, FM_RAW);
    M[8] = P("SIZE", 0, 127, 80, FM_PCT); M[9] = P("DEC", 0, 127, 70, FM_T, 0.3, 20); M[10] = P("DAMP", 0, 127, 60, FM_PCT);
    M[11] = P("PRE", 0, 127, 20, FM_PREMS); M[12] = P("HPF", 0, 127, 24, FM_HZ, 20, 2000); M[13] = P("RTN", 0, 127, 100, FM_RAW);
    M[16] = P("GLUE", 0, 127, 36, FM_PCT); M[17] = P("DRIV", 0, 127, 0, FM_PCT); M[18] = P("DJF", -64, 63, 0, FM_DJF);
    M[19] = P("VOL", 0, 127, 100, FM_RAW);
    M[24] = PL("SRC", 0, 9, 1, DUCK_SRC, 10, false); M[25] = PL("LEN", 0, 8, 3, DUCK_LEN, 9, false);
    M[26] = P("SHAP", 0, 127, 64, FM_SHAP); M[27] = P("DEPT", 0, 127, 0, FM_PCT);
    M[28] = P("XOVR", 0, 127, 127, FM_XOVR); M[29] = P("HIGH", 0, 127, 127, FM_PCT);
    M[30] = P("DDLY", 0, 127, 0, FM_PCT); M[31] = P("DREV", 0, 127, 0, FM_PCT);
  }
  // trackDefs(mid)[i]
  const PDef& track(int mid, int i) const { return i < 8 ? mach[mid < 0 ? 0 : mid][i] : (mid == M_SMP && smpx[i].valid()) ? smpx[i] : common[i]; }
};

inline const Tables& T() { static Tables t; return t; }

inline void defaultTrackParams(int mid, double* p) {
  for (int i = 0; i < NP; i++) { const PDef& d = T().track(mid, i); p[i] = d.valid() ? d.def : 0; }
}
inline void defaultMaster(double* m) {
  for (int i = 0; i < NM; i++) { const PDef& d = T().master[i]; m[i] = d.valid() ? d.def : 0; }
}

// ---------- JS-compatible number formatting ----------
inline std::string toFixed(double v, int d) {
  char b[48];
  // JS toFixed rounds half away from zero on the decimal expansion; nudge by a tiny epsilon to match on ties like 0.125
  const double s = std::pow(10.0, d);
  double r = std::floor(std::fabs(v) * s + 0.5 + 1e-9) / s;
  if (v < 0 && r != 0) r = -r;
  std::snprintf(b, sizeof b, "%.*f", d, r);
  if (!std::strcmp(b, "-0") || !std::strcmp(b, "-0.0") || !std::strcmp(b, "-0.00")) return std::string(b + 1);
  return b;
}
inline std::string fHz(double f) {
  if (f >= 1000) return toFixed(f / 1000, f >= 10000 ? 1 : 2) + "k";
  return toFixed(f, f < 100 ? 1 : 0);
}
inline std::string fT(double s) {
  if (s >= 1) return toFixed(s, 2) + "s";
  if (s >= 0.1) return toFixed(s * 1000, 0) + "ms";
  return toFixed(s * 1000, 1) + "ms";
}
inline std::string istr(double v) { char b[24]; std::snprintf(b, sizeof b, "%d", (int)jsround(v)); return b; }

inline std::string formatValue(const PDef& d, double v) {
  switch (d.fmt) {
    case FM_RAW: return istr(v);
    case FM_BI: return (v > 0 ? "+" : "") + istr(v);
    case FM_HZ: return fHz(expm(v, d.lo, d.hi));
    case FM_T: return fT(expm(v, d.lo, d.hi));
    case FM_PCT: return istr(v / 127 * 100) + "%";
    case FM_SEMI: return "+" + toFixed(v / 127 * d.lo, 0) + "st";
    case FM_LIST: { int i = (int)jsround(v); return (i >= 0 && i < d.nlist) ? d.list[i] : "?"; }
    case FM_FMK_RATI: return toFixed(0.5 + v / 127 * 7.5, 2);
    case FM_SNR_SPRD: return toFixed(1.1 + v / 127 * 1.7, 2);
    case FM_CLP_CNT: return istr(1 + jsround(v / 127 * 7));
    case FM_MTL_SPRD: return toFixed(v / 63.5, 2);
    case FM_MDL_MATR: { static const char* const MT[] = { "MEMB", "WOOD", "GLAS", "METL", "HARM" }; int i = (int)jsround(v / 127 * 4); if (i > 4) i = 4; if (i < 0) i = 0; return MT[i]; }
    case FM_ZAP_SWEP: return (v > 0 ? "+" : "") + toFixed(v / 64 * 60, 0) + "st";
    case FM_ZAP_RATI: return toFixed(expm(v, 0.25, 12), 2);
    case FM_GLT_SWEP: return (v > 0 ? "+" : "") + toFixed(v / 64 * 5, 1) + "oc";
    case FM_DUO_RATI: return toFixed(1 + v / 127 * 3, 2);
    case FM_DUO_WAVE: return v < 8 ? "TRI" : v > 119 ? "SQR" : istr(v / 127 * 100) + "%";
    case FM_TYPE: return v < 21 ? "LP" : v < 43 ? "L>B" : v < 85 ? "BP" : v < 107 ? "B>H" : "HP";
    case FM_T0: return v == 0 ? "0" : fT(expm(v, d.lo, d.hi));
    case FM_DECINF: return v >= 127 ? "INF" : fT(expm(v, d.lo, d.hi));
    case FM_PAN: return v == 0 ? "C" : (v < 0 ? "L" + istr(-v) : "R" + istr(v));
    case FM_BITS: return v >= 16 ? "OFF" : istr(v);
    case FM_SRR: return v == 0 ? "OFF" : istr(v);
    case FM_CTUN: { int r = (int)jsround(v); return std::string(NOTE_NAMES[((r % 12) + 12) % 12]) + istr(std::floor(r / 12.0) - 1); }
    case FM_LFOSPD: { int i = (int)jsround(v); if (i < 0) i = 0; if (i > 13) i = 13; return std::string(LFO_SPD[i]) + "st"; }
    case FM_DEG: return istr(v / 128 * 360) + "\xc2\xb0";
    case FM_PCTRAW: return istr(v) + "%";
    case FM_MICR: return v == 0 ? "0" : (v > 0 ? "+" : "") + istr(v) + "/48";
    case FM_RLEN: { int i = (int)jsround(v); if (i < 0) i = 0; if (i > 5) i = 5; return std::string(RLEN[i]) + "st"; }
    case FM_DJF: return v == 0 ? "OFF" : v < 0 ? "LP" + istr(-v) : "HP" + istr(v);
    case FM_SHAP: return v < 43 ? "HOLD" : v < 86 ? "LIN" : "PUMP";
    case FM_XOVR: return v >= 127 ? "FULL" : fHz(expm(v, 40, 800));
    case FM_PREMS: return istr(v / 127 * 200) + "ms";
    case FM_SEMIB: return (v > 0 ? "+" : "") + istr(v) + "st";
    case FM_CENT: { const int c = (int)jsround(v / 64 * 100); return (c > 0 ? "+" : "") + std::to_string(c) + "c"; }
    case FM_SLOT: { char b[16]; std::snprintf(b, sizeof b, "S%02d", (int)jsround(v) + 1); return b; }
    case FM_PCT1: return toFixed(v / 127 * 100, 1) + "%";
    case FM_SLCE: { const int k = (int)jsround(v); return k <= 0 ? "STEP" : "#" + std::to_string(k); }
  }
  return istr(v);
}

} // namespace hd
