// HARDER — plugin UI (NanoVG). Mirrors the web groovebox: main face + fold-out LAB.
// Everything here runs on the host's UI thread; it edits the project model under core->mtx and sends
// commands to the audio thread. The waveform preview uses its own engine instance, never the audio one.
#include "DistrhoUI.hpp"
#include "DistrhoStandaloneUtils.hpp"
#include "HarderShared.hpp"
#include "Export.hpp"
#include "Library.hpp"
#include "Control.hpp"
#include "Fonts.hpp"
#include <chrono>
#include <deque>
#include <set>
#include <random>
#include <fstream>
#include <sstream>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

extern "C" int nvgAddFallbackFontId(NVGcontext* ctx, int baseFont, int fallbackFont);   // nanovg (compiled by DGL)

START_NAMESPACE_DISTRHO
using namespace hd;

static double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------------- theme ----------------
struct Theme {
  Color page, body, bodyHi, seam, print, printHi, cap, capHi, capLo, line, edge, shadow, hl, ledOff, selFg, rec;
  Color screwHi, screwLo, screwSlot, numEdge, numRing, bezel, bezelRing, onLock;
  Color knobA, knobB, knobC, knobR1, knobR2, grip1, grip2, knobSh, well, trigA, trigB, trigC, trigD, trigDrop, trigEdge;
  Color ledDim, phLed, pfA, pfB, pfLed, pf[4], preEdge, modBg, modEdge, chipEdge, range, led, lock, oledBg, oled, oledMid, oledDim, ok, warn;
};
static Color A(const Color& c, float a) { Color o = c; o.alpha = c.alpha * a; return o; }
static Color H(uint32_t rgb, float a = 1.f) { return Color((int)((rgb >> 16) & 255), (int)((rgb >> 8) & 255), (int)(rgb & 255), a); }
static Theme darkTheme() {
  Theme t;
  t.line = H(0x3a4048); t.edge = H(0x2c3239); t.shadow = H(0, .8f); t.hl = H(0xffffff, .05f); t.ledOff = H(0x3a2016); t.selFg = H(0xffffff); t.rec = H(0xff4d4d);
  t.screwHi = H(0x4b525b); t.screwLo = H(0x1a1d21); t.screwSlot = H(0x0e1013); t.numEdge = H(0x0a0d10); t.numRing = H(0x30363d);
  t.bezel = H(0x0b0d0f); t.bezelRing = H(0x33393f); t.onLock = H(0x0b0718);
  t.knobA = H(0x4a515a); t.knobB = H(0x262a30); t.knobC = H(0x1a1d21); t.knobR1 = H(0x15181b); t.knobR2 = H(0x343a41); t.grip1 = H(0x2e333a); t.grip2 = H(0x262a30); t.knobSh = H(0, .55f);
  t.well = H(0x15181b); t.trigA = H(0x3a4048); t.trigB = H(0x2e333a); t.trigC = H(0x444b54); t.trigD = H(0x373d45); t.trigDrop = H(0x1a1d21); t.trigEdge = H(0x464d56);
  t.ledDim = H(0x3a1e12); t.phLed = H(0x6a7480); t.pfA = H(0x33383f); t.pfB = H(0x2a2e35); t.pfLed = H(0x2a3038);
  t.pf[0] = H(0x5d3a22); t.pf[1] = H(0x2f4a3c); t.pf[2] = H(0x3b3460); t.pf[3] = H(0x2d4356);
  t.preEdge = H(0x30353c); t.modBg = H(0x15181c); t.modEdge = H(0x262b31); t.chipEdge = H(0x343a42); t.range = H(0x2b3037);
  t.page = H(0x0c0e11); t.body = H(0x1c2025); t.bodyHi = H(0x252a30); t.seam = H(0x121519);
  t.print = H(0x87919c); t.printHi = H(0xcfd6dd); t.cap = H(0x2b3037); t.capHi = H(0x343a42); t.capLo = H(0x1f2328);
  t.led = H(0xff6a1f); t.lock = H(0xa38cff); t.oledBg = H(0x030608); t.oled = H(0xdcf2f4); t.oledMid = H(0x7f9a9f); t.oledDim = H(0x2f4247);
  t.ok = H(0x45d39a); t.warn = H(0xffb547);
  return t;
}
static Theme lightTheme() {
  Theme t = darkTheme();
  t.page = H(0xe1e3e4); t.body = H(0xd3d6d8); t.bodyHi = H(0xe4e7e9); t.seam = H(0xb9bdc1);
  t.print = H(0x59626b); t.printHi = H(0x1d2329); t.cap = H(0xe8eaeb); t.capHi = H(0xf6f7f8); t.capLo = H(0xa5aaaf);
  t.ok = H(0x17905f); t.warn = H(0xa96500); t.rec = H(0xd23333);
  t.line = H(0xb3b8bd); t.edge = H(0xb6bbc0); t.shadow = H(0x283038, .32f); t.hl = H(0xffffff, .75f); t.ledOff = H(0xd6c0b3); t.selFg = H(0);
  t.screwHi = H(0xf4f5f6); t.screwLo = H(0x969ca2); t.screwSlot = H(0x7a8188); t.numRing = H(0xaeb3b8);
  t.bezel = H(0x22262c); t.bezelRing = H(0xa9afb4);
  t.knobA = H(0xffffff); t.knobB = H(0xdadde0); t.knobC = H(0xb6bbc0); t.knobR1 = H(0x989ea4); t.knobR2 = H(0xc7cbcf); t.grip1 = H(0xe6e8ea); t.grip2 = H(0xd4d7da); t.knobSh = H(0x283038, .3f);
  t.well = H(0xc1c5c9); t.trigA = H(0xf4f5f6); t.trigB = H(0xe1e4e6); t.trigC = H(0xffffff); t.trigD = H(0xeceef0); t.trigDrop = H(0xa5aaaf); t.trigEdge = H(0xc3c7cb);
  t.ledDim = H(0xe2cdc1); t.phLed = H(0x98a0a7); t.pfA = H(0xeceef0); t.pfB = H(0xdfe2e5); t.pfLed = H(0xced2d6);
  t.pf[0] = H(0xe9c6b0); t.pf[1] = H(0xbcd8c9); t.pf[2] = H(0xcec4ee); t.pf[3] = H(0xbccfdd);
  t.preEdge = H(0xc3c7cb); t.modBg = H(0xeceef0); t.modEdge = H(0xcbd0d4); t.chipEdge = H(0xbec3c8); t.range = H(0xc8ccd0);
  return t;
}

struct R {
  float x = 0, y = 0, w = 0, h = 0;
  R() {}
  R(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
  bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  float cx() const { return x + w / 2; } float cy() const { return y + h / 2; } float r() const { return x + w; } float b() const { return y + h; }
};

// ---------------- layout constants (logical px) ----------------
static constexpr float WW = 1180, HCLOSED = 640, LABH = 372;
static constexpr float PX = 20, PY = 62, PW = 1140, PH = 520;   // device panel
// ids
enum : int {
  ID_NONE = 0, ID_PAT = 100, ID_BPM = 120, ID_SWING, ID_LEN, ID_SPD, ID_MLEN, ID_MPREV, ID_MNEXT, ID_LOCKTAG,
  ID_CELL = 140, ID_ENC = 160, ID_PAGE = 180, ID_TRACK = 200, ID_FUNC = 220, ID_REC, ID_FILL, ID_ALL, ID_PLAY, ID_STOP, ID_PERF, ID_UNDO,
  ID_TRIG = 240, ID_SPAGE = 260, ID_COPY = 270, ID_PASTE, ID_CLEAR, ID_CLRLK, ID_PULL, ID_THEME,
  ID_TAB = 300, ID_STYLE = 320, ID_DEN = 350, ID_VAR, ID_CLOCKS, ID_CFILLS, ID_CHALF, ID_CTEMPO, ID_GEN, ID_GENTRK, ID_MUT, ID_EK, ID_ER, ID_EUC, ID_POLY,
  ID_ROLE = 380, ID_MACH = 390, ID_MAT = 400, ID_FN = 410, ID_PRESET = 1000, ID_RND = 430, ID_INIT, ID_PLIST,
  ID_KIT = 440, ID_PERFA = 460, ID_PERFB = 500, ID_SAVE = 540, ID_LOAD, ID_SCALE = 550,
  ID_EXLOOPS = 560, ID_EXSTEMS = 566, ID_EXLOOP, ID_EXKIT, ID_AUDDEF = 599, ID_AUDDEV = 600, ID_SR = 670, ID_BUF = 680, ID_MIDI = 700, ID_RESCAN = 740, ID_AUDSCROLL, ID_MUTE = 750, ID_SOLO, ID_TFADER, ID_OFADER,
  // user library
  ID_SSRC = 2000, ID_GSRC = 2010, ID_SCAT = 2100, ID_GCAT = 2300, ID_SITEM = 2500, ID_GITEM = 2600, ID_LBTN = 2700, ID_TEOK = 2790, ID_TECANCEL, ID_CATPREV, ID_CATNEXT, ID_SLOTCLR,
  // LAB > CONTROL (standalone)
  ID_CGROUP = 3000, ID_CKEY = 3020, ID_CMIDI = 3040, ID_CLEARN = 3060, ID_CFIXED, ID_CMODE = 3070, ID_CDEL = 3080, ID_CRESET = 3090,
  // 2.0: kits library, song / chain, sample tab, MIDI out and clock, outputs
  ID_KSRC = 3100, ID_KCAT = 3200, ID_KITEM = 3400, ID_SEQ = 3500, ID_CHCLR = 3505, ID_SONG = 3600, ID_SONGBTN = 4600,
  ID_SMPTAB = 4700, ID_MIDIOUT = 4800, ID_CLOCK = 4820, ID_OUTS = 4840,
};
// LAB tabs: ids are stored in ui.json, the order on screen is TAB_ORDER
enum { TAB_GROOVE = 0, TAB_SOUND = 1, TAB_KITS = 2, TAB_PERFORM = 3, TAB_PROJECT = 4, TAB_AUDIO = 5, TAB_CONTROL = 6, TAB_SONG = 7, TAB_SAMPLE = 8, NTABS = 9 };
static const char* const TAB_NAMES[NTABS] = { "GROOVE", "SOUND", "KITS", "PERFORM", "PROJECT", "AUDIO", "CONTROL", "SONG", "SAMPLE" };
static const int TAB_ORDER_SA[] = { TAB_GROOVE, TAB_SOUND, TAB_SAMPLE, TAB_KITS, TAB_SONG, TAB_PERFORM, TAB_PROJECT, TAB_AUDIO, TAB_CONTROL };
static const int TAB_ORDER_PL[] = { TAB_GROOVE, TAB_SOUND, TAB_SAMPLE, TAB_KITS, TAB_SONG, TAB_PERFORM, TAB_PROJECT };
static const int PERF_PARAMS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 18, 19, 20, 24, 25, 26, 27, 28, 29, 40 };
constexpr int NPERFP = sizeof(PERF_PARAMS) / sizeof(PERF_PARAMS[0]);
struct PerfPad { int g; const char* lab; double st; int rt; int vel; double q; };
static const PerfPad PERF[16] = {
  { 0, "1/16", 1, 2, 0, 0 }, { 0, "1/32", 0.5, 3, 0, 0 }, { 0, "1/12", 4.0 / 3, 5, 0, 0 }, { 0, "1/24", 2.0 / 3, 6, 0, 0 },
  { 1, "", 0, 0, 40, 0 }, { 1, "", 0, 0, 70, 0 }, { 1, "", 0, 0, 100, 0 }, { 1, "", 0, 0, 127, 0 },
  { 2, "", 0, 0, 0, 0.25 }, { 2, "", 0, 0, 0, 0.5 }, { 2, "", 0, 0, 0, 0.75 }, { 2, "", 0, 0, 0, 1 },
  { 3, "", 0, 0, 0, 0.25 }, { 3, "", 0, 0, 0, 0.5 }, { 3, "", 0, 0, 0, 0.75 }, { 3, "", 0, 0, 0, 1 } };
static const double UI_SCALES[] = { 0.75, 0.85, 1.0, 1.15, 1.3, 1.5 };
static const char* const UI_SCALE_NAMES[] = { "75%", "85%", "100%", "115%", "130%", "150%" };
static const char* const SMP_MODE_NAMES[4] = { "ONE-SHOT", "REVERSE", "LOOP", "REVERSE LOOP" };
static const int TUNE_LO_HI[NMACH][2] = { { 25, 160 }, { 25, 160 }, { 80, 600 }, { 0, 0 }, { 100, 900 }, { 40, 2400 }, { 40, 5000 }, { 0, 0 }, { 40, 4000 } };

class HarderUI : public UI {
  Core* core = nullptr;
  Theme th;
  FontId fMark = -1, fPrint = -1, fPrintB = -1, fOled = -1, fNum = -1;
  // ---- persisted UI state
  bool light = false, drawer = false; int scaleIdx = 2, labTab = 0;
  // ---- web UI state
  int tr = 0, page = 2, mpage = 0, spage = 0;
  std::set<int> latched; int heldStep = -1; bool edited = false, latchedByHold = false; double holdT = 0;
  bool funcLatch = false, allLatch = false, perfMode = false, recOn = false, fillLatch = false, fillHeld = false, shiftDown = false;
  int style = 1, mat = 6, fn = -1; GenOpts gopt; bool styleTempo = true; int eK = 5, eR = 0;
  std::vector<std::string> undoStack;
  std::unique_ptr<Pattern> clip; struct { Step steps[NSTEP]; int len = 16, scl = 4; bool has = false; } clipTrack;
  std::string toastMsg; double toastUntil = 0;
  int perfHeld = -1; int presetScroll = 0;
  // ---- engine feedback mirror
  int pos[NTR]; uint32_t firedSeen[NT] = { 0 }; double flashUntil[NT] = { 0 }; float trackPeak[NT] = { 0 }; float mpeak = 0, gr = 1, duckG = 1; uint32_t meterSeq = 0;
  std::deque<float> meterHist; bool playing = false; int cue = -1; uint32_t seenModelSeq = 0;
  // ---- preview
  std::unique_ptr<Engine> prev; std::vector<float> wave; float wavePeak = 0; int waveLast = 0; double waveDue = 0; bool waveDirty = true;
  static constexpr double PSR = 22050;
  // ---- immediate-mode input
  enum Phase { PH_DRAW, PH_PRESS, PH_RELEASE, PH_SCROLL };
  Phase ph = PH_DRAW; float mx = 0, my = 0; float wheel = 0; bool consumed = false; int pressedId = ID_NONE; uint mods = 0;
  int lastPressId = ID_NONE; double lastPressT = 0; bool dbl = false;
  struct Drag { int id = ID_NONE; float y = 0; double acc = 0; int kind = 0; int target = 0; } drag;   // kind 1 param, 2 number, 3 slider
  std::set<int> editingHost;
  double lastRepaint = 0; bool needRepaint = true;
  std::mt19937 rngDev{ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() };
  int fileOp = 0;   // 1 save, 2 load, 3 export loop, 4 export kit
  // ---- standalone
  bool standalone = false;
  std::map<int, int> keyHeld, keyPerf; std::set<int> keysDown;   // held step / perf keys by source (key or MIDI source id)
  std::string lastSavedProject, lastSavedUi; double nextAutosave = 0;
  bool settingsApplied = false, audioListed = false; std::string chosenDev, audioMsg; int devScroll = 0;
  std::vector<std::string> audioDevs, midiNames; std::vector<bool> midiOpen; std::string currentDev;
  std::vector<std::string> midiOutNames; std::vector<bool> midiOutOpen;
  // ---- export (worker thread)
  std::thread expThread; std::atomic<bool> expBusy{ false }, expCancel{ false }; std::mutex expMtx; std::string expStatus, expShown;
  int expLoopsIdx = 1; bool expStems = true;
  // ---- level strip
  float outDb = 0; int soloTr = -1; bool faderEdited = false; double nextAudioRetry = 0;
  float outHoldDb = -200, trkHoldDb = -200; double outHoldT = 0, trkHoldT = 0; int trkHoldTr = -1;
  struct FaderInfo { R r; double maxDb; };
  std::map<int, FaderInfo> faderRects;
  int cpuTick = 0;
  // ---- user library (Documents/HARDER/Library)
  int sndSrc = 0, grvSrc = 0, kitSrc = 0;         // 0 factory, 1 user
  LibIndex sndLib, grvLib, kitLib; double nextScan = 0; bool libEnsured = false;
  std::string sndCat, grvCat, kitCat;              // "" = INBOX / UNSORTED (the folder's root)
  std::string sndSel, grvSel, kitSel;              // selected item path ("" = the category is selected)
  int sndScroll = 0, grvScroll = 0, sndCatScroll = 0, grvCatScroll = 0, kitScroll = 0, kitCatScroll = 0;
  bool kitWithMaster = true; int sliceSens = 50;
  bool autoLoad = false, autoArmed = false; double nextAutoScan = 0;
  std::set<std::string> watchKnown; std::map<std::string, std::string> watchPending; std::set<std::string> selfWritten;
  std::string confirmKey; double confirmUntil = 0;
  // drag and drop of a library item onto a category
  std::string dragPath; int dragLib = 0; bool dragMoved = false; float dragX = 0, dragY = 0, dragX0 = 0, dragY0 = 0; std::string dragLabel;
  std::vector<std::pair<std::string, R>> catDrop;  // this frame's category chips (drop targets)
  // one-line text field (names)
  enum TeAct { TE_NONE, TE_NEWCAT_S, TE_RENCAT_S, TE_RENITEM_S, TE_SAVE_S, TE_NEWCAT_G, TE_RENCAT_G, TE_RENITEM_G, TE_SAVE_G, TE_NEWCAT_K, TE_RENCAT_K, TE_RENITEM_K, TE_SAVE_K };
  struct TextEdit { int act = TE_NONE; std::string text, title, arg; } te;
  double lastKeyT = 0; int lastKeyCode = -1; bool sawPuglText = false;
  FontId fCjk = -1; bool cjkTried = false; std::string cjkPath;
  // ---- keyboard / MIDI controller mapping (standalone)
  ControlMap cmap; int ctlGroup = 0, midiScroll = 0; Action keyLearn, learnAct; bool midiLearn = false, learnFixed = false;
  std::string lastMidi; std::set<int> funcSrc, allSrc, ccDown; int learnedSrc = -1; double learnedUntil = 0;

public:
  HarderUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
    core = harderCore(getPluginInstancePointer());
    standalone = harderIsStandalone();
    if (standalone) macStandaloneInit();
    for (int& p : pos) p = -1;
    fMark = createFontFromMemory("mark", hdfonts::font_mark, hdfonts::font_mark_size, false);
    fPrint = createFontFromMemory("print", hdfonts::font_print, hdfonts::font_print_size, false);
    fPrintB = createFontFromMemory("printb", hdfonts::font_printb, hdfonts::font_printb_size, false);
    fOled = createFontFromMemory("oled", hdfonts::font_oled, hdfonts::font_oled_size, false);
    fNum = createFontFromMemory("num", hdfonts::font_num, hdfonts::font_num_size, false);
    gopt.den = 0.55; gopt.vari = 0.40; gopt.locks = true; gopt.fills = true; gopt.half = false;
    prev.reset(new Engine()); prev->init(PSR); prev->previewReset(125);
    if (core) {
      std::lock_guard<std::recursive_mutex> g(core->mtx);
      loadUiState(); seenModelSeq = core->modelSeq.load();
      if (standalone) loadControl();
      if (S().tracks[0].pre.size()) { const int pi = presetIndex(S().tracks[0].pre); if (pi >= 0) mat = PRESETS[pi].mat; }
    }
    th = light ? lightTheme() : darkTheme();
    applySize();
  }

  ~HarderUI() override {
    expCancel = true; if (expThread.joinable()) expThread.join();
    if (core) { std::lock_guard<std::recursive_mutex> g(core->mtx); core->pinned.clear(); }   // the undo history goes with the UI
    if (standalone && core) { std::lock_guard<std::recursive_mutex> g(core->mtx); autosaveNow(true); }
    if (saveThread.joinable()) saveThread.join();
  }

protected:
  // ================= helpers: model access =================
  Project& S() { return *core->S; }
  Pattern& pat() { return S().patterns[S().cur]; }
  bool isMaster() const { return tr == 8; }
  bool func() const { return shiftDown || funcLatch || !funcSrc.empty(); }
  bool allMode() const { return allLatch || !allSrc.empty(); }
  const PDef& defOf(int i) const { return isMaster() ? T().master[i] : T().track(core->S->tracks[tr].m, i); }
  int pageBase() const { return isMaster() ? mpage * 8 : PAGE_BASE[page]; }
  std::vector<int> heldSteps() const { std::set<int> s = latched; if (heldStep >= 0) s.insert(heldStep); for (auto& k : keyHeld) s.insert(k.second); return std::vector<int>(s.begin(), s.end()); }
  const Step* heldStepPtr() { auto hs = heldSteps(); return hs.empty() ? nullptr : &pat().steps[tr][hs[0]]; }
  bool lockOf(int i, double& v) { const Step* st = heldStepPtr(); if (st && st->on && st->l.has(i)) { v = st->l.v[i]; return true; } return false; }
  double* baseArr() { return isMaster() ? S().master : S().tracks[tr].p; }
  double getVal(int i) { double v; if (lockOf(i, v)) return v; return baseArr()[i]; }
  void toast(const std::string& m) { toastMsg = m; toastUntil = nowMs() + 1100; needRepaint = true; }
  void dirty() { core->modelSeq.fetch_add(0); }
  // sample ids the undo snapshots refer to: their data stays in the session cache (Core::pruneCache)
  double nextPrune = 0;
  void pinUndoSamples() {
    core->pinned.clear();
    for (const std::string& u : undoStack) {
      const size_t s0 = u.find("\"samples\":["); if (s0 == std::string::npos) continue;
      for (size_t p = u.find("\"id\":\"", s0); p != std::string::npos; p = u.find("\"id\":\"", p + 6)) core->pinned.insert(u.substr(p + 6, 16));
    }
  }
  // samples by id only (the session cache keeps their data): an undo step stays small with samples in the project
  void pushUndo() { undoStack.push_back(projectToString(S(), false)); if (undoStack.size() > 60) undoStack.erase(undoStack.begin()); }
  void schedulePreview() { waveDirty = true; waveDue = nowMs() + 50; needRepaint = true; }

  // ---- host parameter notification for exposed values
  void hostEdit(int idx, bool cont = false) {
    if (idx < 0) return;
    const float v = (float)core->hostFromModel(idx); core->own(idx, v);   // before begin-edit: the host may send its old value then
    if (cont) { if (!editingHost.count(idx)) { editParameter(idx, true); editingHost.insert(idx); } setParameterValue(idx, v); }
    else { editParameter(idx, true); setParameterValue(idx, v); editParameter(idx, false); }
  }
  void endHostEdits() { for (int idx : editingHost) editParameter(idx, false); editingHost.clear(); }
  void notifyHostAll() { std::vector<int> ch; core->refreshHost(&ch); for (int idx : ch) { editParameter(idx, true); setParameterValue(idx, core->hostVal[idx].load()); editParameter(idx, false); } }
  void setBase(int t, int i, double v, bool cont) {
    if (t == 8) { S().master[i] = v; core->sendParam(8, i, v); hostEdit(hostIndexForMaster(i), cont); }
    else { S().tracks[t].p[i] = v; core->sendParam(t, i, v); hostEdit(hostIndexForTrack(t, i), cont); }
  }

  // ================= web actions =================
  void writeLocks(int i, double v) {
    if (!edited) pushUndo();
    edited = true;
    for (int s : heldSteps()) { Step& st = pat().steps[tr][s]; st.on = 1; st.l.set(i, v); core->sendStep(S().cur, tr, s); }
  }
  static double stepRound(double v, const PDef& d) { return (d.max - d.min) >= 50 && d.fmt != FM_SLOT && d.fmt != FM_SLCE ? jsround(v * 10) / 10 : jsround(v); }
  void setVal(int i, double v, bool cont) {
    const PDef& d = defOf(i); if (!d.valid()) return;
    v = clampd(v, d.min, d.max);
    const auto hs = heldSteps();
    if (d.step && hs.empty()) { toast("HOLD A STEP FIRST"); return; }
    if (!hs.empty() && d.lock) writeLocks(i, v);
    else if (isMaster()) setBase(8, i, v, cont);
    else if (allMode() && hs.empty()) {
      const double delta = v - S().tracks[tr].p[i]; const int mid = S().tracks[tr].m;
      for (int t = 0; t < NT; t++) {
        if (i < 8 && S().tracks[t].m != mid) continue;
        const PDef& dd = T().track(S().tracks[t].m, i); if (!dd.valid()) continue;
        const double nv = t == tr ? v : clampd(jsround((S().tracks[t].p[i] + delta) * 10) / 10, dd.min, dd.max);
        setBase(t, i, nv, cont);
      }
    } else {
      if (i == 22 || i == 23) { S().tracks[tr].p[i] = v; core->sendParam(tr, i, v); }
      else setBase(tr, i, v, cont);
      // AUTO slices on a sample without slice points: find them
      if (i == 51 && jsround(v) == 5 && S().tracks[tr].m == M_SMP) { const SampleData* sd = sampleOf(tr); if (sd && sd->slices.empty()) detectNow(); }
    }
    schedulePreview();
  }
  void resetVal(int i) {
    const PDef& d = defOf(i); if (!d.valid()) return;
    const auto hs = heldSteps(); double lv;
    if (!hs.empty() && lockOf(i, lv)) {
      pushUndo();
      for (int s : hs) { Step& st = pat().steps[tr][s]; if (st.on && st.l.has(i)) { st.l.del(i); core->sendStep(S().cur, tr, s); } }
      schedulePreview(); return;
    }
    if (d.step) return;
    setVal(i, d.def, false);
  }
  void toggleStep(int s) {
    if (s >= pat().len[tr]) { toast("BEYOND TRACK LENGTH"); return; }
    pushUndo(); Step& st = pat().steps[tr][s]; if (st.on) st = Step(); else { st = Step(); st.on = 1; }
    core->sendStep(S().cur, tr, s);
  }
  void toggleTrigless(int s) {
    if (s >= pat().len[tr]) { toast("BEYOND TRACK LENGTH"); return; }
    pushUndo(); Step& st = pat().steps[tr][s];
    if (st.on && st.tl) st = Step(); else { st.on = 1; st.tl = 1; }
    core->sendStep(S().cur, tr, s); toast(st.on ? "TRIGLESS LOCK" : "CLEARED");
  }
  void selectTrack(int t) {
    if (tr != t) latched.clear(); tr = t;
    if (t < 8) { const int pi = presetIndex(S().tracks[t].pre); if (pi >= 0) mat = PRESETS[pi].mat; }
    if (t == 8 && perfMode) perfMode = false;
    if (spage * 16 >= pat().len[tr]) spage = 0;
    schedulePreview();
  }
  void audition(int t) {
    const auto hs = t == tr ? heldSteps() : std::vector<int>();
    const Step* st = hs.empty() ? nullptr : &pat().steps[t][hs[0]];
    core->sendAud(t, 110, st && st->on ? &st->l : nullptr, hs.empty() ? -1 : hs[0]); flashUntil[t] = nowMs() + 90;
    if (recOn && playing) recPerf(t, nullptr);
  }
  void trackPress(int t) {
    if (t == 8) { selectTrack(8); return; }
    if (func()) { S().mutes[t] = S().mutes[t] ? 0 : 1; core->sendSimple(Cmd::MUTE, t, 0, 0, S().mutes[t]); hostEdit(HP_MUTE0 + t); return; }
    selectTrack(t); audition(t);
  }
  void recPerf(int t, const Locks* locks) {
    if (!recOn || !playing) return;
    Pattern& P = pat(); const int len = P.len[t]; if (pos[t] < 0) return;
    const double bpm = core->hostSynced.load() ? core->hostBpm.load() : S().bpm;
    const double stepS = 60.0 / bpm / 4 / SPEED_X[P.scl[t]];
    const double el = (core->eng->fb.now.load() - core->eng->fb.posTime[t].load()) / core->eng->sr;
    const int s = (((pos[t] + (int)jsround(el / stepS)) % len) + len) % len;
    Step& st = P.steps[t][s]; st.on = 1; st.tl = 0;
    if (locks) for (int i = 0; i < NP; i++) if (locks->has(i)) st.l.set(i, locks->v[i]);
    core->sendStep(S().cur, t, s);
  }
  // sequencing mode: pattern, chain (pattern keys append), song (LAB > SONG)
  void setSeq(int k) {
    if (S().seq == k) return;
    pushUndo(); S().seq = k; core->sendArr();
    if (k == ARR_CHAIN) toast(S().chain.empty() ? "CHAIN: PRESS PATTERNS IN ORDER" : "CHAIN " + chainText());
    else if (k == ARR_SONG) toast(S().song.n ? "SONG: " + std::to_string(S().song.n) + " ROWS" : "SONG IS EMPTY: LAB > SONG");
    else toast("PATTERN MODE");
  }
  std::string chainText(int maxN = 40) const { std::string o; for (size_t i = 0; i < core->S->chain.size() && (int)i < maxN; i++) { if (i) o += " "; o += std::to_string(core->S->chain[i] + 1); } if ((int)core->S->chain.size() > maxN) o += " \xe2\x80\xa6"; return o; }
  void patternPress(int p) {
    if (func()) { pushUndo(); S().patterns[p] = pat(); core->sendPattern(p); toast("P" + std::to_string(S().cur + 1) + " COPIED TO P" + std::to_string(p + 1)); return; }
    if (S().seq == ARR_CHAIN) {   // chain mode: the key appends the pattern
      if (S().chain.size() >= 64) { toast("CHAIN IS FULL (64)"); return; }
      pushUndo(); S().chain.push_back((int8_t)p); core->sendArr();
      if (!playing && S().chain.size() == 1) { S().cur = p; S().bpm = pat().bpm; S().swing = pat().swing; latched.clear(); core->sendSimple(Cmd::CUE, 0, p); core->sendSimple(Cmd::BPM, 0, 0, S().bpm); core->sendSimple(Cmd::SWING, 0, 0, S().swing); if (spage * 16 >= pat().len[tr]) spage = 0; schedulePreview(); }
      toast("CHAIN " + chainText(16)); return;
    }
    if (playing) { cue = p == S().cur ? -1 : p; core->sendSimple(Cmd::CUE, 0, p); }
    else {
      S().cur = p; S().bpm = pat().bpm; S().swing = pat().swing; latched.clear();
      core->sendSimple(Cmd::CUE, 0, p); core->sendSimple(Cmd::BPM, 0, 0, S().bpm); core->sendSimple(Cmd::SWING, 0, 0, S().swing);
      if (spage * 16 >= pat().len[tr]) spage = 0;
      schedulePreview();
    }
    hostEdit(HP_PATTERN);
  }
  void play() {
    if (core->hostPlaying.load()) { toast("FOLLOWING LIVE TRANSPORT"); return; }
    if (core->extClock.load()) { toast("FOLLOWING MIDI CLOCK"); return; }
    core->sendSimple(Cmd::BPM, 0, 0, S().bpm); core->sendSimple(Cmd::SWING, 0, 0, S().swing); core->sendSimple(Cmd::PLAY);
  }
  void stop() { core->sendSimple(Cmd::STOP); cue = -1; }
  void setFill(bool v) { fillHeld = v; core->sendSimple(Cmd::FILL, 0, 0, 0, v); core->own(HP_FILL, v ? 1.f : 0.f); editParameter(HP_FILL, true); setParameterValue(HP_FILL, v ? 1.f : 0.f); editParameter(HP_FILL, false); }
  // removes SRC locks of track t in every pattern; only the steps that changed are sent to the engine
  void stripSrcLocks(int t) {
    for (int p = 0; p < NPAT; p++) for (int s = 0; s < NSTEP; s++) {
      Step& st = S().patterns[p].steps[t][s]; if (!st.on || !(st.l.mask & 0xFF)) continue;
      for (int i = 0; i < 8; i++) st.l.del(i); core->sendStep(p, t, s);
    }
  }
  void setMachine(int mid) {
    if (isMaster()) { toast("SELECT A TRACK"); return; }
    if (S().tracks[tr].m == mid) return;
    pushUndo();
    TrackM& t = S().tracks[tr]; double p[NP]; defaultTrackParams(mid, p); for (int i = 8; i < NP; i++) p[i] = t.p[i];
    if (mid == M_SMP) { for (int i = 0; i < NSLOT; i++) if (S().smp[i]) { p[2] = i; break; } if (!slotsInUse()) toast("LOAD A SAMPLE: LAB > SOUND > USER"); }
    t.m = mid; std::memcpy(t.p, p, sizeof p); t.pre.clear(); core->sendMachine(tr); stripSrcLocks(tr);
    notifyHostAll(); schedulePreview(); audition(tr);
  }
  void cycleMachine(int dir) { if (isMaster()) return; setMachine((S().tracks[tr].m + dir + NMACH) % NMACH); }
  void loadPreset(int pi) {
    if (isMaster()) { toast("SELECT A TRACK"); return; }
    pushUndo(); TrackM& t = S().tracks[tr];
    const double k22 = t.p[22], k23 = t.p[23]; double p[NP]; presetParams(pi, p); p[22] = k22; p[23] = k23;
    const bool changed = t.m != PRESETS[pi].m;
    t.m = PRESETS[pi].m; std::memcpy(t.p, p, sizeof p); t.pre = PRESETS[pi].n;
    core->sendMachine(tr); if (changed) stripSrcLocks(tr);
    notifyHostAll(); schedulePreview(); audition(tr);
  }
  // ================= samples =================
  // is a slot played by a SAMPLE track (its base value, or a lock in any pattern)? exceptTr: ignore that track's base
  bool slotUsed(int slot, int exceptTr = -1) {
    for (int t = 0; t < NT; t++) {
      if (S().tracks[t].m != M_SMP) continue;
      if (t != exceptTr && (int)jsround(S().tracks[t].p[2]) == slot) return true;
      for (int p = 0; p < NPAT; p++) for (int s = 0; s < NSTEP; s++) { const Step& st = S().patterns[p].steps[t][s]; if (st.on && st.l.has(2) && (int)jsround(st.l.v[2]) == slot) return true; }
    }
    return false;
  }
  // the slot that already holds this sample, else an empty one, else this track's own slot if nothing else plays it,
  // else any slot nothing plays; -1 when all 64 are in use
  int slotFor(const SampleRef& s, int t) {
    for (int i = 0; i < NSLOT; i++) if (S().smp[i] && S().smp[i]->id == s->id) return i;
    for (int i = 0; i < NSLOT; i++) if (!S().smp[i]) return i;
    if (t >= 0 && t < NT && S().tracks[t].m == M_SMP) { const int c = (int)clampd(jsround(S().tracks[t].p[2]), 0, NSLOT - 1); if (!slotUsed(c, t)) return c; }
    for (int i = 0; i < NSLOT; i++) if (!slotUsed(i)) return i;
    return -1;
  }
  int slotOf(int t, const Step* st = nullptr) { const double v = st && st->on && st->l.has(2) ? st->l.v[2] : S().tracks[t].p[2]; return (int)clampd(jsround(v), 0, NSLOT - 1); }
  const SampleData* sampleOf(int t, const Step* st = nullptr) { return S().tracks[t].m == M_SMP ? S().smp[slotOf(t, st)].get() : nullptr; }
  int slotsInUse() { int n = 0; for (auto& x : S().smp) if (x) n++; return n; }
  // a library sound or WAV onto the selected track; like a factory preset it keeps choke and duck routing
  bool applySound(const SoundDef& d, const std::string& label) {
    if (isMaster()) { toast("SELECT A TRACK"); return false; }
    int slot = -1;
    if (d.m == M_SMP) { if (!d.smp) return false; slot = slotFor(d.smp, tr); if (slot < 0) { toast("ALL 64 SAMPLE SLOTS IN USE"); return false; } }
    pushUndo(); TrackM& t = S().tracks[tr];
    const double k22 = t.p[22], k23 = t.p[23]; double p[NP]; std::memcpy(p, d.p, sizeof p); p[22] = k22; p[23] = k23;
    const bool changed = t.m != d.m;
    if (slot >= 0) { S().smp[slot] = core->cache.add(d.smp); p[2] = slot; core->sendSamples(); }
    t.m = d.m; std::memcpy(t.p, p, sizeof p); t.pre = label;
    core->sendMachine(tr); if (changed) stripSrcLocks(tr);
    notifyHostAll(); schedulePreview(); audition(tr);
    return true;
  }
  // slots nothing plays any more are emptied (they would be saved with the project otherwise)
  void purgeSlots() {
    int n = 0; pushUndo();
    for (int i = 0; i < NSLOT; i++) if (S().smp[i] && !slotUsed(i)) { S().smp[i].reset(); n++; }
    if (!n) { undoStack.pop_back(); toast("NO UNUSED SAMPLES"); return; }
    core->sendSamples(); toast(std::to_string(n) + (n == 1 ? " SAMPLE REMOVED" : " SAMPLES REMOVED"));
  }

  // ================= user library =================
  std::string sndDir(const std::string& cat) { return cat.empty() ? libSoundsDir() : joinPath(libSoundsDir(), cat); }
  std::string grvDir(const std::string& cat) { return cat.empty() ? libGroovesDir() : joinPath(libGroovesDir(), cat); }
  std::string kitDir(const std::string& cat) { return cat.empty() ? libKitsDir() : joinPath(libKitsDir(), cat); }
  // one of the three user libraries (1 sounds, 2 grooves, 3 kits) and its view state
  struct LibView { LibIndex& L; std::string& cat; std::string& sel; int& scroll; int& catScroll; int idCat, idItem, idArrow, teNewCat, teRenCat, teRenItem; const char* root; };
  LibView lv(int lib) {
    if (lib == 1) return { sndLib, sndCat, sndSel, sndScroll, sndCatScroll, ID_SCAT, ID_SITEM, 0, TE_NEWCAT_S, TE_RENCAT_S, TE_RENITEM_S, "INBOX" };
    if (lib == 2) return { grvLib, grvCat, grvSel, grvScroll, grvCatScroll, ID_GCAT, ID_GITEM, 100, TE_NEWCAT_G, TE_RENCAT_G, TE_RENITEM_G, "UNSORTED" };
    return { kitLib, kitCat, kitSel, kitScroll, kitCatScroll, ID_KCAT, ID_KITEM, 200, TE_NEWCAT_K, TE_RENCAT_K, TE_RENITEM_K, "UNSORTED" };
  }
  std::string libDir(int lib, const std::string& cat) { return lib == 1 ? sndDir(cat) : lib == 2 ? grvDir(cat) : kitDir(cat); }
  std::string libRootDir(int lib) { return lib == 1 ? libSoundsDir() : lib == 2 ? libGroovesDir() : libKitsDir(); }
  void scanLib(bool force = false) {
    const double now = nowMs(); if (!force && now < nextScan) return;
    nextScan = now + 1500;
    if (!libEnsured) { libEnsured = true; if (!ensureLibrary()) toast("LIBRARY FOLDER UNAVAILABLE"); }
    const std::string s1 = sndLib.sig, s2 = grvLib.sig, s3 = kitLib.sig;
    scanLibrary(libSoundsDir(), true, sndLib); scanLibrary(libGroovesDir(), false, grvLib); scanLibrary(libKitsDir(), false, kitLib, true);
    for (int lib = 1; lib <= 3; lib++) { LibView v = lv(lib); if (!v.cat.empty() && std::find(v.L.cats.begin(), v.L.cats.end(), v.cat) == v.L.cats.end()) { v.cat.clear(); v.scroll = 0; } }
    if (sndLib.sig != s1 || grvLib.sig != s2 || kitLib.sig != s3) needRepaint = true;
  }
  const LibItem* findItem(const LibIndex& L, const std::string& path) { for (const LibItem& it : L.items) if (it.path == path) return &it; return nullptr; }
  void loadLibSound(const LibItem& it) {
    SoundDef d; std::string err; bool cut = false;
    if (!loadSoundItem(it, d, &core->cache, err, &cut)) { toast(upper(err)); return; }
    if (applySound(d, d.name)) toast(cut ? "LONGER THAN 60 S: CUT" : upper(d.name));
  }
  void loadLibGroove(const LibItem& it) {
    std::unique_ptr<Pattern> P(new Pattern()); std::string name, err;
    if (!loadGrooveItem(it, *P, name, err)) { toast(upper(err)); return; }
    pushUndo(); applyPattern(*P, "GROOVE " + upper(name));
  }
  // automatic loading of new or changed sounds (another program renders into the library): a file loads once its
  // size and time have stayed the same over two scans, onto the selected track. A file is known by its name, size and
  // modification time, not by its folder: renaming or moving a category (here or in Explorer) loads nothing.
  static std::string watchSig(const LibItem& it) { return fileName(it.path) + '\x1f' + std::to_string(it.size) + '\x1f' + std::to_string(it.mtime); }
  void autoLoadScan() {
    const double now = nowMs(); if (now < nextAutoScan) return; nextAutoScan = now + 700;
    LibIndex L; scanLibrary(libSoundsDir(), true, L);
    if (!autoArmed) { watchKnown.clear(); watchPending.clear(); for (const LibItem& it : L.items) watchKnown.insert(watchSig(it)); autoArmed = true; return; }
    const LibItem* pick = nullptr;
    for (const LibItem& it : L.items) {
      const std::string sig = watchSig(it);
      if (watchKnown.count(sig)) continue;
      if (selfWritten.count(it.path)) { watchKnown.insert(sig); continue; }
      auto p = watchPending.find(it.path);
      if (p != watchPending.end() && p->second == sig) { watchKnown.insert(sig); watchPending.erase(p); if (!pick || it.mtime > pick->mtime) pick = &it; }
      else watchPending[it.path] = sig;
    }
    if (pick) {
      if (isMaster()) { toast("NEW SOUND: SELECT A TRACK"); return; }
      const LibItem it = *pick; loadLibSound(it); sndSel = it.path; sndCat = it.cat; toast("NEW \xc2\xb7 " + upper(it.name));
    }
  }
  void markSelf(const std::string& path) { if (path.empty()) return; selfWritten.insert(path); const std::string w = joinPath(dirName(path), stemOf(fileName(path)) + ".wav"); selfWritten.insert(w); }
  bool confirm(const std::string& key) {   // second press within 3 s
    const double now = nowMs();
    if (confirmKey == key && now < confirmUntil) { confirmKey.clear(); return true; }
    confirmKey = key; confirmUntil = now + 3000; toast("PRESS AGAIN TO CONFIRM"); return false;
  }
  void startEdit(int act, const std::string& title, const std::string& text, const std::string& arg = "") { te.act = act; te.title = title; te.text = text; te.arg = arg; needRepaint = true; }
  void commitEdit() {
    const int act = te.act; std::string name = te.text; const std::string arg = te.arg; te = TextEdit();
    while (!name.empty() && name.back() == ' ') name.pop_back(); while (!name.empty() && name[0] == ' ') name.erase(0, 1);
    if (name.empty()) { toast("NAME IS EMPTY"); return; }
    std::string err;
    switch (act) {
      case TE_NEWCAT_S: case TE_NEWCAT_G: case TE_NEWCAT_K: {
        const int lib = act == TE_NEWCAT_S ? 1 : act == TE_NEWCAT_G ? 2 : 3; LibView v = lv(lib);
        const std::string dir = libDir(lib, safeFileName(name));
        if (pathExists(dir)) { toast("THAT CATEGORY EXISTS"); break; }
        if (!makeDir(dir)) { toast("COULD NOT CREATE THE FOLDER"); break; }
        v.cat = fileName(dir); v.sel.clear(); v.scroll = 0;
        toast("CATEGORY " + upper(fileName(dir))); break;
      }
      case TE_RENCAT_S: case TE_RENCAT_G: case TE_RENCAT_K: {
        const int lib = act == TE_RENCAT_S ? 1 : act == TE_RENCAT_G ? 2 : 3; LibView v = lv(lib);
        const std::string root = libRootDir(lib), from = joinPath(root, arg), to = joinPath(root, safeFileName(name));
        if (from == to) break;
        if (pathExists(to)) { toast("THAT CATEGORY EXISTS"); break; }
        if (!movePath(from, to)) { toast("COULD NOT RENAME THE FOLDER"); break; }
        v.cat = fileName(to); v.sel.clear();
        toast("RENAMED"); break;
      }
      case TE_RENITEM_S: case TE_RENITEM_G: case TE_RENITEM_K: {
        const int lib = act == TE_RENITEM_S ? 1 : act == TE_RENITEM_G ? 2 : 3; LibView v = lv(lib);
        const LibItem* it = findItem(v.L, arg); if (!it) break;
        const LibItem copy = *it; const std::string np = moveItem(copy, dirName(copy.path), err, name);
        if (np.empty()) { toast(upper(err)); break; }
        markSelf(np); v.sel = np; toast("RENAMED"); break;
      }
      case TE_SAVE_S: {
        if (isMaster()) { toast("SELECT A TRACK"); break; }
        const TrackM& t = S().tracks[tr]; SampleRef smp; if (t.m == M_SMP) smp = S().smp[slotOf(tr)];
        const std::string jp = saveSound(sndDir(sndCat), name, t.m, t.p, smp, err);
        if (jp.empty()) { toast(upper(err)); break; }
        markSelf(jp); sndSel = jp; S().tracks[tr].pre = name; toast("SAVED " + upper(name)); break;
      }
      case TE_SAVE_G: {
        const std::string jp = saveGroove(grvDir(grvCat), name, pat(), err);
        if (jp.empty()) { toast(upper(err)); break; }
        markSelf(jp); grvSel = jp; toast("SAVED " + upper(name)); break;
      }
      case TE_SAVE_K: {
        const std::string jp = saveKit(kitDir(kitCat), name, S(), kitWithMaster, err);
        if (jp.empty()) { toast(upper(err)); break; }
        kitSel = jp; toast("KIT SAVED " + upper(name)); break;
      }
    }
    scanLib(true);
  }
  void moveToCat(int lib, const std::string& path, const std::string& cat) {
    LibView v = lv(lib); const LibItem* it = findItem(v.L, path); if (!it || it->cat == cat) return;
    const LibItem copy = *it; std::string err; const std::string np = moveItem(copy, libDir(lib, cat), err);
    if (np.empty()) { toast(upper(err)); return; }
    markSelf(np); v.sel = np;
    toast("MOVED TO " + (cat.empty() ? std::string(v.root) : upper(cat))); scanLib(true);
  }
  void deleteSelected(int lib) {
    LibView v = lv(lib); std::string err;
    if (!v.sel.empty()) {
      const LibItem* it = findItem(v.L, v.sel); if (!it) { v.sel.clear(); return; }
      if (!confirm("del:" + v.sel)) return;
      const LibItem copy = *it; if (!trashItem(copy, err)) { toast(upper(err)); return; }
      v.sel.clear(); toast("MOVED TO LIBRARY/TRASH");
    } else if (!v.cat.empty()) {
      int n = 0; for (const LibItem& it : v.L.items) if (it.cat == v.cat) n++;
      if (n && !confirm("delcat:" + v.cat)) return;
      if (!trashFolder(libDir(lib, v.cat), err)) { toast(upper(err)); return; }
      v.cat.clear(); toast(n ? "CATEGORY MOVED TO LIBRARY/TRASH" : "CATEGORY REMOVED");
    } else { toast(std::string(v.root) + " CANNOT BE REMOVED"); return; }
    scanLib(true);
  }
  void importWav(const std::string& fn) {
    SampleRef s; std::string err; bool cut = false;
    if (!loadWavFile(fn, s, err, &cut)) { toast(upper(err)); return; }
    s = core->cache.add(s);
    // a copy of the original file (all channels, any format) goes into the library
    const std::string dir = sndDir(sndCat); std::string dst;
    if (makeDir(dir)) { dst = uniquePath(dir, stemOf(fileName(fn)), ".wav"); if (copyFile(fn, dst)) markSelf(dst); else dst.clear(); }
    SoundDef d; wavSound(s, d); d.name = stemOf(fileName(fn));
    if (applySound(d, d.name)) toast(cut ? "LONGER THAN 60 S: CUT" : dst.empty() ? "LOADED (NOT COPIED TO THE LIBRARY)" : "IMPORTED " + upper(d.name));
    if (!dst.empty()) sndSel = dst;
    scanLib(true);
  }
  void loadKit(int k) {
    pushUndo(); S().kit = k; kitTracks(k, S()); for (int i = 0; i < NT; i++) S().roles[i] = i;
    double m[NM]; kitMaster(k, m, S().master); std::memcpy(S().master, m, sizeof m);
    for (int t = 0; t < NT; t++) core->sendMachine(t);
    core->sendMaster(); for (int t = 0; t < NT; t++) stripSrcLocks(t);
    notifyHostAll(); schedulePreview(); toast(std::string("KIT ") + KITS[k].name);
  }
  // a user kit: the eight sounds (samples into free slots), the roles, the master FX if the kit has them (volume kept)
  void loadLibKit(const LibItem& it) {
    UserKit k; std::string err;
    if (!loadKitItem(it, k, &core->cache, err)) { toast(upper(err)); return; }
    pushUndo();
    int oldM[NT]; for (int t = 0; t < NT; t++) oldM[t] = S().tracks[t].m;
    // samples go into slots that hold the same sample, empty slots, or slots the replaced tracks used
    for (int t = 0; t < NT; t++) { TrackM& tk = S().tracks[t]; tk.m = M_SUB; defaultTrackParams(M_SUB, tk.p); }   // so slotFor sees the old samples as unused
    bool full = false;
    for (int t = 0; t < NT; t++) {
      const SoundDef& d = k.t[t]; TrackM& tk = S().tracks[t]; double p[NP]; std::memcpy(p, d.p, sizeof p);
      if (d.m == M_SMP) {
        if (!d.smp) { tk.m = M_SUB; defaultTrackParams(M_SUB, tk.p); tk.pre.clear(); continue; }
        const int slot = slotFor(d.smp, -1); if (slot < 0) { full = true; tk.m = M_SUB; defaultTrackParams(M_SUB, tk.p); tk.pre.clear(); continue; }
        S().smp[slot] = core->cache.add(d.smp); p[2] = slot;
      }
      tk.m = d.m; std::memcpy(tk.p, p, sizeof p); tk.pre = d.name; S().roles[t] = k.roles[t];
    }
    if (k.hasMaster) { const double vol = S().master[19]; std::memcpy(S().master, k.master, sizeof k.master); S().master[19] = vol; core->sendMaster(); }
    core->sendSamples();
    for (int t = 0; t < NT; t++) { core->sendMachine(t); if (S().tracks[t].m != oldM[t]) stripSrcLocks(t); }
    notifyHostAll(); schedulePreview(); kitSel = it.path;
    toast(full ? "ALL 64 SAMPLE SLOTS IN USE: CLEAN UP SLOTS" : "KIT " + upper(k.name));
  }
  void randomizeSound() {
    if (isMaster()) return; pushUndo();
    TrackM& t = S().tracks[tr]; std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < 8; i++) {
      if (t.m == M_SMP && (i == 2 || i == 7)) continue;   // keep the sample and its level
      const PDef& d = T().mach[t.m][i]; const double range = d.max - d.min;
      double v = d.def + (U(rngDev) * 2 - 1) * range * 0.5; if (range < 10) v = d.min + std::floor(U(rngDev) * (range + 1));
      t.p[i] = clampd(jsround(v), d.min, d.max);
    }
    t.pre.clear(); core->sendTrackP(tr); notifyHostAll(); schedulePreview(); audition(tr);
  }
  void initTrack() { if (isMaster()) return; pushUndo(); TrackM& t = S().tracks[tr]; defaultTrackParams(t.m, t.p); t.pre.clear(); core->sendTrackP(tr); notifyHostAll(); schedulePreview(); audition(tr); }
  void applyPattern(const Pattern& P, const std::string& msg) {
    S().patterns[S().cur] = P; if (!core->hostSynced.load()) { S().bpm = P.bpm; } S().swing = P.swing;
    core->sendPattern(S().cur); core->sendSimple(Cmd::BPM, 0, 0, P.bpm); core->sendSimple(Cmd::SWING, 0, 0, S().swing);
    latched.clear(); spage = 0; toast(msg);
    if (!playing && !core->hostPlaying.load()) play();
  }
  uint32_t seed() { return (uint32_t)(rngDev() % 1000000000u); }
  void genAll() {
    pushUndo(); Mulberry r(seed()); double keep[2] = { S().bpm, S().swing };
    std::unique_ptr<Pattern> P(new Pattern()); generatePattern(r, style, gopt, styleTempo ? nullptr : keep, S(), *P);
    applyPattern(*P, std::string(STYLES[style].name) + " GROOVE");
  }
  void genPoly() { pushUndo(); Mulberry r(seed()); std::unique_ptr<Pattern> P(new Pattern()); polyPattern(r, gopt, S().bpm, S().swing, S(), *P); applyPattern(*P, "POLYMETER"); }
  void genTrk() {
    if (isMaster()) { toast("SELECT A TRACK"); return; }
    pushUndo(); Mulberry r(seed()); const StyleDef& st = STYLES[style]; const SeedDef& sd = st.seeds[(int)std::floor(r() * st.nseeds)];
    Pattern& P = pat(); genRow(r, style, sd, tr, gopt, S(), P.steps[tr], P.len[tr], P.scl[tr]); core->sendPattern(S().cur);
    toast("TRACK " + std::to_string(tr + 1) + " \xc2\xb7 " + st.name);
  }
  void mutate() { pushUndo(); std::uniform_real_distribution<double> U(0, 1); auto r = [&]() { return U(rngDev); }; mutatePattern(pat(), r, gopt.vari, S()); core->sendPattern(S().cur); toast("MUTATED"); }
  void applyEuclid() {
    if (isMaster()) { toast("SELECT A TRACK"); return; }
    pushUndo(); Pattern& P = pat(); const int n = P.len[tr]; const int k = (int)clampd(eK, 1, n), rot = eR % n; bool e[NSTEP];
    euclid(k, n, rot, e);
    for (int s = 0; s < NSTEP; s++) { Step& st = P.steps[tr][s]; if (s < n && e[s]) { if (!st.on || st.tl) { st = Step(); st.on = 1; } } else st = Step(); }
    core->sendPattern(S().cur); toast("EUCLID " + std::to_string(k) + "/" + std::to_string(n));
  }
  void copyBtn() {
    if (func()) { std::memcpy(clipTrack.steps, pat().steps[tr], sizeof clipTrack.steps); clipTrack.len = pat().len[tr]; clipTrack.scl = pat().scl[tr]; clipTrack.has = true; toast("TRACK COPIED"); }
    else { clip.reset(new Pattern(pat())); toast("PATTERN COPIED"); }
  }
  void pasteBtn() {
    if (func()) { if (!clipTrack.has) { toast("NO TRACK COPIED"); return; } pushUndo(); std::memcpy(pat().steps[tr], clipTrack.steps, sizeof clipTrack.steps); pat().len[tr] = clipTrack.len; pat().scl[tr] = clipTrack.scl; }
    else { if (!clip) { toast("NO PATTERN COPIED"); return; } pushUndo(); pat() = *clip; }
    core->sendPattern(S().cur); toast("PASTED");
  }
  void clearBtn() {
    pushUndo();
    if (func()) { const double b = pat().bpm, w = pat().swing; emptyPattern(pat(), b, w); toast("PATTERN CLEARED"); }
    else { for (auto& st : pat().steps[tr]) st = Step(); toast(isMaster() ? "FX TRACK CLEARED" : "TRACK CLEARED"); }
    core->sendPattern(S().cur); latched.clear();
  }
  void clearLocks() {
    pushUndo(); for (auto& st : pat().steps[tr]) { if (st.on && !st.tl) { st = Step(); st.on = 1; } else st = Step(); }
    core->sendPattern(S().cur); schedulePreview(); toast("LOCKS CLEARED");
  }
  void undo() {
    if (undoStack.empty()) { toast("NOTHING TO UNDO"); return; }
    std::unique_ptr<Project> p(new Project()); if (projectFromString(undoStack.back(), *p, &core->cache)) {
      const int keepCur = S().cur; uint8_t mu[NT]; std::memcpy(mu, S().mutes, NT);
      S() = *p; S().cur = keepCur; std::memcpy(S().mutes, mu, NT);
      core->sendAll(); notifyHostAll(); schedulePreview(); toast("UNDO");
    }
    undoStack.pop_back();
  }
  void sendLen() { const Pattern& P = pat(); core->sendPlen(S().cur); if (spage * 16 >= P.len[tr]) spage = 0; }
  void perfValue(int k, int& pi, double& v, const PDef*& d) {
    pi = PERF[k].g == 2 ? S().perfA : S().perfB; d = &T().track(S().tracks[tr].m, pi);
    v = d->valid() ? jsround(d->min + (d->max - d->min) * PERF[k].q) : 0;
  }
  void perfDown(int k) {
    if (isMaster()) return; const PerfPad& P = PERF[k];
    if (P.g == 0) { core->sendRoll(tr, true, P.st, 110, nullptr); Locks L; L.set(45, P.rt); L.set(47, 0); recPerf(tr, &L); }
    else if (P.g == 1) { core->sendAud(tr, P.vel, nullptr); Locks L; L.set(41, P.vel); recPerf(tr, &L); }
    else { int pi; double v; const PDef* d; perfValue(k, pi, v, d); Locks L; L.set(pi, v); core->sendAud(tr, 110, &L); recPerf(tr, &L); }
    perfHeld = k; flashUntil[tr] = nowMs() + 90;
  }
  void perfUp(int k) { if (k >= 0 && PERF[k].g == 0) core->sendRoll(tr, false, 1, 110, nullptr); perfHeld = -1; }

  // ================= files / ui state =================
  void loadUiState() {
    JVal j; if (!parseJson(core->uiState, j)) return;
    light = j["light"].truthy(); drawer = j["drawer"].truthy(); scaleIdx = (int)clampd(j["scale"].num(2), 0, 5); labTab = (int)clampd(j["tab"].num(0), 0, NTABS - 1); if (!standalone && (labTab == TAB_AUDIO || labTab == TAB_CONTROL)) labTab = TAB_PROJECT;
    outDb = (float)clampd(j["outDb"].num(0), -80, 0); core->outDb.store(outDb);
    sndSrc = j["sndSrc"].num(0) == 1 ? 1 : 0; grvSrc = j["grvSrc"].num(0) == 1 ? 1 : 0; const bool al = j["autoLoad"].truthy(); if (al != autoLoad) { autoLoad = al; autoArmed = false; }
    sndCat = j["sndCat"].str(); grvCat = j["grvCat"].str();
    kitSrc = j["kitSrc"].num(0) == 1 ? 1 : 0; kitCat = j["kitCat"].str(); kitWithMaster = !j.has("kitFx") || j["kitFx"].truthy();
    sliceSens = (int)clampd(j["sliceSens"].num(50), 0, 100);
  }
  void saveUiState() {
    JVal j = JVal::Obj(); j.set("light", JVal::Bool(light)); j.set("drawer", JVal::Bool(drawer)); j.set("scale", JVal::Num(scaleIdx)); j.set("tab", JVal::Num(labTab)); j.set("outDb", JVal::Num(jsround(outDb * 10) / 10));
    j.set("sndSrc", JVal::Num(sndSrc)); j.set("grvSrc", JVal::Num(grvSrc)); j.set("autoLoad", JVal::Bool(autoLoad)); j.set("sndCat", JVal::Str(sndCat)); j.set("grvCat", JVal::Str(grvCat));
    j.set("kitSrc", JVal::Num(kitSrc)); j.set("kitCat", JVal::Str(kitCat)); j.set("kitFx", JVal::Bool(kitWithMaster)); j.set("sliceSens", JVal::Num(sliceSens));
    std::string s; writeJson(s, j); core->uiState = s;
  }
  void uiFileBrowserSelected(const char* filename) override {
    if (!filename || !core) { fileOp = 0; return; }
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    std::string fn = filename;
    auto ensureExt = [&fn](const char* ext) { const size_t n = std::strlen(ext); if (fn.size() < n || lower(fn.substr(fn.size() - n)) != ext) fn += ext; };
    if (fileOp == 1) {
      ensureExt(".json"); toast(writeFileAtomic(fn, projectToString(S())) ? "PROJECT SAVED" : "SAVE FAILED");
    } else if (fileOp == 2) {
      std::string txt; std::unique_ptr<Project> p(new Project());
      if (readTextFile(fn, txt) && projectFromString(txt, *p, &core->cache)) { pushUndo(); S() = *p; latched.clear(); core->sendAll(); notifyHostAll(); schedulePreview(); toast("PROJECT LOADED"); }
      else toast("NOT A HARDER PROJECT");
    } else if (fileOp == 3 || fileOp == 4) {
      ensureExt(".zip"); startExport(fileOp, fn);
    } else if (fileOp == 5) importWav(fn);
    fileOp = 0; needRepaint = true;
  }
  // op: 1 save project, 2 load project, 3 export loop, 4 export kit, 5 import WAV
  void openFile(int op) {
    if (op >= 3 && expBusy.load()) { toast("EXPORT RUNNING"); return; }
    FileBrowserOptions o; o.saving = op == 1 || op == 3 || op == 4;
    static std::string defName;
    if (op == 1) { o.title = "Save HARDER project"; defName = "harder-project.json"; }
    else if (op == 2) { o.title = "Load HARDER project (web or plugin .json)"; defName = "harder-project.json"; }
    else if (op == 3) { o.title = "Export loop (.zip: WAV mix, stems, MIDI, project)"; defName = lower(loopBaseName(S().cur, exportBpm())) + ".zip"; }
    else if (op == 4) { o.title = "Export kit (.zip: eight one-shot WAVs)"; defName = "harder-kit.zip"; }
    else { o.title = "Import a WAV file onto the selected track (copied into the library)"; defName = ""; }
    if (!defName.empty()) o.defaultName = defName.c_str();
    if (standalone && op != 5) { static std::string dir; dir = harderConfigDir(); if (!dir.empty()) o.startDir = dir.c_str(); }
    fileOp = op;
    if (!openFileBrowser(o)) { fileOp = 0; toast("FILE DIALOG UNAVAILABLE"); }
  }

  // ================= export =================
  static constexpr int EXP_LOOPS[4] = { 1, 2, 4, 8 };
  // the project as it sounds: in Live, at Live's tempo
  double exportBpm() { const double hb = core->hostBpm.load(); return (!standalone && core->hostSynced.load() && hb >= 20) ? hb : S().bpm; }
  void startExport(int kind, const std::string& path) {
    if (expBusy.load()) return;
    if (expThread.joinable()) expThread.join();
    std::shared_ptr<Project> snap(new Project(S())); snap->bpm = exportBpm();
    const int loops = EXP_LOOPS[expLoopsIdx]; const bool stems = expStems;
    { std::lock_guard<std::mutex> g(expMtx); expStatus = kind == 3 ? "RENDERING MIX" : "RENDERING ONE-SHOTS"; }
    expBusy = true; expCancel = false;
    expThread = std::thread([this, snap, path, kind, loops, stems] {
      auto st = [this](const std::string& m) { std::lock_guard<std::mutex> g(expMtx); expStatus = m; };
      std::vector<uint8_t> z;
      const bool ok = kind == 3 ? exportLoopZip(*snap, loops, stems, z, st, &expCancel) : exportKitZip(*snap, z, st, &expCancel);
      if (!ok) st("EXPORT CANCELLED");
      else if (writeFileAtomic(path, z.data(), z.size())) {
        const size_t sl = path.find_last_of("/\\"); char mb[32]; std::snprintf(mb, sizeof mb, " (%.1f MB)", z.size() / 1048576.0);
        st("SAVED " + (sl == std::string::npos ? path : path.substr(sl + 1)) + mb);
      } else st("COULD NOT WRITE THE FILE");
      expBusy = false;
    });
  }

  // ================= standalone: autosave and audio settings =================
  // the audio driver's values (UI::getSampleRate would be the plugin's)
  static uint audioRate() { return DISTRHO_NAMESPACE::getSampleRate(); }
  static uint audioBuffer() { return DISTRHO_NAMESPACE::getBufferSize(); }
  // Compared without sample data (fast), written with it. With large samples the full file takes 100+ ms to build and
  // tens of MB to write, so that happens on a worker thread from a copy of the project; `sync` (on exit) writes here.
  std::thread saveThread; std::atomic<bool> saveBusy{ false }, saveFailed{ false };
  void autosaveNow(bool sync = false) {
    const std::string dir = harderConfigDir(); if (dir.empty()) return;
    if (sync && saveThread.joinable()) saveThread.join();   // on exit: let a running write finish and see whether it failed
    if (saveFailed.exchange(false)) lastSavedProject.clear();   // the last background write failed: try again
    const std::string p = projectToString(S(), false);
    if (p != lastSavedProject) {
      if (sync) {
        if (saveThread.joinable()) saveThread.join();
        if (writeFileAtomic(dir + "autosave.json", projectToString(S()))) lastSavedProject = p;
      } else if (!saveBusy.load()) {   // a save still running: this change is picked up at the next tick
        if (saveThread.joinable()) saveThread.join();
        std::shared_ptr<Project> snap(new Project(S()));   // samples are shared, not copied
        const std::string path = dir + "autosave.json";
        saveBusy = true; lastSavedProject = p;
        saveThread = std::thread([this, snap, path] { if (!writeFileAtomic(path, projectToString(*snap))) saveFailed = true; saveBusy = false; });
      }
    }
    if (core->uiState != lastSavedUi && writeFileAtomic(dir + "ui.json", core->uiState)) lastSavedUi = core->uiState;
  }
  void saveSettings() {
    JVal j = JVal::Obj(); j.set("device", JVal::Str(chosenDev)); j.set("sampleRate", JVal::Num(audioRate())); j.set("bufferSize", JVal::Num(audioBuffer()));
    JVal m = JVal::Arr(); for (size_t i = 0; i < midiNames.size(); i++) if (midiOpen[i]) m.push(JVal::Str(midiNames[i])); j.set("midi", m);
    JVal mo = JVal::Arr(); for (size_t i = 0; i < midiOutNames.size(); i++) if (midiOutOpen[i]) mo.push(JVal::Str(midiOutNames[i])); j.set("midiOut", mo);
    j.set("clockIn", JVal::Bool(core->clockIn.load())); j.set("clockOut", JVal::Bool(core->clockOut.load())); j.set("clockOffset", JVal::Num(core->clockOffsetMs.load()));
    std::string out; writeJson(out, j); writeFileAtomic(harderConfigDir() + "settings.json", out);
  }
  void refreshAudioLists() {
    audioListed = true; getAudioDevices(audioDevs, currentDev); getMidiInputs(midiNames, midiOpen); getMidiOutputs(midiOutNames, midiOutOpen);
  }
  // first idle of the standalone: the audio driver is running with its defaults; switch to the saved settings
  void applySavedSettings() {
    settingsApplied = true;
    std::string txt; JVal j; const bool has = readTextFile(harderConfigDir() + "settings.json", txt) && parseJson(txt, j) && j.isObj();
    if (has) {
      chosenDev = j["device"].str();
      const uint sr = (uint)j["sampleRate"].num(0), buf = (uint)j["bufferSize"].num(0);
      if (!chosenDev.empty() || (sr && sr != audioRate()) || (buf && buf != audioBuffer()) || !isAudioDeviceOpen()) {
        const std::string want = chosenDev;
        if (!requestAudioConfig(chosenDev.c_str(), sr, buf) && !want.empty()) { chosenDev.clear(); toast("SAVED AUDIO DEVICE NOT FOUND"); }
      }
    }
    std::vector<std::string> names, want; std::vector<bool> open; getMidiInputs(names, open);
    if (has && j["midi"].isArr()) { for (size_t i = 0; i < j["midi"].size(); i++) want.push_back(j["midi"][i].str()); }
    else want = names;   // first start: every MIDI input
    setMidiInputs(want);
    { std::vector<std::string> outs; if (has) for (size_t i = 0; i < j["midiOut"].size(); i++) outs.push_back(j["midiOut"][i].str()); setMidiOutputs(outs); }
    core->clockIn.store(has && j["clockIn"].truthy()); core->clockOut.store(has && j["clockOut"].truthy()); core->clockOffsetMs.store((int)clampd(has ? j["clockOffset"].num(0) : 0, -50, 100));
    refreshAudioLists();
  }
  void applyAudio(const std::string& dev, uint sr, uint buf) {
    const std::string old = chosenDev; chosenDev = dev;
    const bool ok = requestAudioConfig(dev.c_str(), sr, buf);
    if (!ok) chosenDev = old;
    audioMsg = ok ? "" : "THAT SETTING DID NOT OPEN \xc2\xb7 THE PREVIOUS ONE IS KEPT";
    toast(ok ? "AUDIO SETTINGS APPLIED" : "AUDIO SETTING FAILED");
    if (soloTr >= 0) core->sendSimple(Cmd::SOLO, soloTr);   // the engine was re-initialised if the rate changed
    refreshAudioLists(); saveSettings();
  }
  void reconnectAudio() {
    const bool ok = requestAudioConfig(chosenDev.c_str(), 0, 0);
    if (!isAudioDeviceOpen()) return;
    audioMsg = ok || chosenDev.empty() ? "" : "THE CHOSEN DEVICE IS MISSING \xc2\xb7 USING THE SYSTEM DEFAULT";
    toast(ok ? "AUDIO RECONNECTED" : "AUDIO ON SYSTEM DEFAULT");
    if (soloTr >= 0) core->sendSimple(Cmd::SOLO, soloTr);
    refreshAudioLists(); needRepaint = true;
  }
  void toggleMidi(size_t k) {
    if (k >= midiNames.size()) return;
    std::vector<std::string> want; for (size_t i = 0; i < midiNames.size(); i++) if (i == k ? !midiOpen[i] : midiOpen[i]) want.push_back(midiNames[i]);
    setMidiInputs(want); refreshAudioLists(); saveSettings();
  }
  void toggleMidiOut(size_t k) {
    if (k >= midiOutNames.size()) return;
    std::vector<std::string> want; for (size_t i = 0; i < midiOutNames.size(); i++) if (i == k ? !midiOutOpen[i] : midiOutOpen[i]) want.push_back(midiOutNames[i]);
    setMidiOutputs(want); refreshAudioLists(); saveSettings();
  }
  double totalScale() const { return getScaleFactor() * UI_SCALES[scaleIdx]; }
  void applySize() {
    const double s = totalScale(); const uint w = (uint)std::lround(WW * s), h = (uint)std::lround((HCLOSED + (drawer ? LABH : 0)) * s);
    if (getWidth() != w || getHeight() != h) setSize(w, h);
    needRepaint = true;
  }
  void uiScaleFactorChanged(double) override { applySize(); }

  // ================= preview =================
  void renderPreview() {
    waveDirty = false; wave.clear();
    if (isMaster() || page == 6) return;
    const TrackM& t = S().tracks[tr]; const Step* st = heldStepPtr();
    const double vel = st && st->on && st->l.has(41) ? st->l.v[41] : t.p[41];
    Engine& E = *prev; for (int i = 0; i < NSLOT; i++) E.setSample(i, S().smp[i].get());   // S() keeps them alive while rendering
    E.previewReset(S().bpm); E.loadTrack(0, t.m, t.p);
    const int N = (int)(PSR * 2.4); wave.assign(N, 0.f); E.stems[0] = wave.data();
    { const auto hs = heldSteps(); E.audition(0, vel > 0 ? vel : 120, st && st->on && st->l.any() ? &st->l : nullptr, hs.empty() ? -1 : hs[0]); }
    float dl[512], dr[512];
    for (int p = 0; p < N; p += 512) { const int n = std::min(512, N - p); E.stems[0] = wave.data() + p; E.process(dl, dr, n); }
    E.stems[0] = nullptr;
    wavePeak = 0; waveLast = 0; for (int i = 0; i < N; i++) { const float a = std::fabs(wave[i]); if (a > wavePeak) wavePeak = a; if (a > 0.001f) waveLast = i; }
  }

  // ================= unattended self-test (CI) =================
  // HARDER_SELFTEST=<file> (any build): at 3 s Space is pressed by its key position (the OS key code, through the same
  // mapping as a real key) and <file>.ready is written (a screenshot can be taken then), at 6 s a JSON report goes
  // to <file>, Space is pressed again and the program quits the normal way (macOS: NSApp terminate:, the Cmd+Q path).
  // Nothing happens without the variable.
  int stPhase = -1; double stT0 = 0; std::string stPath; bool stPlayed = false;
  bool stWantPixels = false; std::string stGl = "\"glRenderer\":\"\",\"glViewport\":\"\",\"glDistinctColors\":-1";
  // what the GPU actually drew (independent of screenshots): read back the finished frame, count the distinct colours
  // on a 64 x 32 grid (a blank frame has 1)
  void selfTestReadPixels() {
    GLint vp[4] = { 0, 0, 0, 0 }; glGetIntegerv(GL_VIEWPORT, vp);
    const char* rn = (const char*)glGetString(GL_RENDERER);
    std::set<uint32_t> cols;
    if (vp[2] > 0 && vp[3] > 0) {
      std::vector<uint8_t> px((size_t)vp[2] * vp[3] * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(vp[0], vp[1], vp[2], vp[3], GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      for (int gy = 0; gy < 32; gy++) for (int gx = 0; gx < 64; gx++) {
        const size_t i = ((size_t)(gy * vp[3] / 32) * vp[2] + (size_t)(gx * vp[2] / 64)) * 4;
        cols.insert((uint32_t)px[i] << 16 | (uint32_t)px[i + 1] << 8 | px[i + 2]);
      }
    }
    char b[400]; std::snprintf(b, sizeof b, "\"glRenderer\":\"%s\",\"glViewport\":\"%dx%d\",\"glDistinctColors\":%d", jsonSafe(rn ? rn : "").c_str(), vp[2], vp[3], (int)cols.size());
    stGl = b;
  }
  static int spaceOsKeycode() {
#ifdef _WIN32
    return 0x39;
#elif defined(__APPLE__)
    return 0x31;   // kVK_Space
#else
    return 0x39 + 8;
#endif
  }
  void selfTestKey(bool press) { KeyboardEvent ev; ev.press = press; ev.key = ' '; ev.keycode = (uint)spaceOsKeycode(); ev.mod = 0; onKeyboard(ev); }
  void selfTestStep(double now) {
    if (stPhase == -1) { const char* p = std::getenv("HARDER_SELFTEST"); stPhase = p && *p ? 0 : 99; if (p) stPath = p; stT0 = now; return; }
    if (stPhase == 0 && now - stT0 > 3000) { stPhase = 1; selfTestKey(true); selfTestKey(false); writeFileAtomic(stPath + ".ready", std::string("1")); stWantPixels = true; repaint(); }
    if (stPhase == 1) toast("SELFTEST \xe8\x87\xaa\xe6\xa3\x80 \xe4\xb8\xad\xe6\x96\x87");   // shown until the report (CJK font check)
    if (stPhase == 1 && now - stT0 > 6000) {
      stPhase = 2; stPlayed = core->eng->fb.playing.load();
      const char* api = getAudioBackendName(); char b[2048];
      std::snprintf(b, sizeof b, "{\"version\":\"%u.%u.%u\",\"backend\":\"%s\",\"deviceOpen\":%s,\"device\":\"%s\",\"sampleRate\":%u,\"bufferSize\":%u,"
        "\"midiIn\":%d,\"midiOut\":%d,\"spaceKeyStartedPlay\":%s,\"spaceScancode\":%d,\"keyNameQ\":\"%s\",\"cjkFont\":\"%s\",\"scale\":%.2f,\"window\":\"%ux%u\",\"dspLoadPermille\":%u,%s}\n",
        (unsigned)HARDER_VER_MAJOR, (unsigned)HARDER_VER_MINOR, (unsigned)HARDER_VER_PATCH, api ? api : "", isAudioDeviceOpen() ? "true" : "false", jsonSafe(currentDev).c_str(),
        audioRate(), audioBuffer(), (int)midiNames.size(), (int)midiOutNames.size(), stPlayed ? "true" : "false",
        physKeyFromOs(spaceOsKeycode()), jsonSafe(keyNameForScancode(0x10)).c_str(), jsonSafe(cjkPath).c_str(), getScaleFactor(), getWidth(), getHeight(), core->cpuPermille.load(), stGl.c_str());
      writeFileAtomic(stPath, std::string(b));
      if (stPlayed) { selfTestKey(true); selfTestKey(false); }
      if (!requestAppQuit()) getWindow().close();
    }
  }
  static std::string jsonSafe(const std::string& v) { std::string o; for (char c : v) { if (c == '"' || c == '\\') o += '\\'; if ((unsigned char)c >= 0x20) o += c; } return o; }

  // ================= idle =================
  void uiIdle() override {
    if (!core) return;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    const double now = nowMs();
    core->syncAllIfOverflow();
    if (standalone && !settingsApplied) applySavedSettings();
    // the driver stopped (device unplugged, taken by another program): try again every 3 s
    if (standalone && settingsApplied && !isAudioDeviceOpen() && now >= nextAudioRetry) { nextAudioRetry = now + 3000; reconnectAudio(); }
    if (standalone && now >= nextAutosave) { nextAutosave = now + 2000; autosaveNow(); }
    if (standalone) selfTestStep(now);
    if (drawer && ((labTab == TAB_SOUND && sndSrc == 1) || (labTab == TAB_GROOVE && grvSrc == 1) || (labTab == TAB_KITS && kitSrc == 1))) scanLib();
    if (autoLoad) autoLoadScan();
    if (standalone) { pollMidi();
#ifdef HARDER_TEST_MIDI_INJECT
      injectTestMidi();
#endif
    }
    if (te.act != TE_NONE && std::fmod(now, 500) < 30) needRepaint = true;   // caret
    if (midiLearn || keyLearn.k != AC_NONE) needRepaint = true;   // blinking learn state
    core->gcSamples(); recPoll();
    if (now >= nextPrune) { nextPrune = now + 5000; pinUndoSamples(); core->pruneCache(); }
    { std::lock_guard<std::mutex> eg(expMtx); if (expStatus != expShown) { expShown = expStatus; needRepaint = true; } }
    if (core->modelSeq.load() != seenModelSeq) {   // project replaced by the host (state load)
      seenModelSeq = core->modelSeq.load(); latched.clear(); undoStack.clear();
      const bool oldLight = light; const int oldScale = scaleIdx; const bool oldDrawer = drawer; loadUiState();
      if (light != oldLight) th = light ? lightTheme() : darkTheme();
      if (scaleIdx != oldScale || drawer != oldDrawer) applySize();
      schedulePreview();
    }
    if (core->pullHost()) { schedulePreview(); needRepaint = true; }
    const Feedback& fb = core->eng->fb;
    const bool pl = fb.playing.load();
    if (pl != playing) { playing = pl; needRepaint = true; if (!pl) cue = -1; }
    const int fc = fb.cur.load();
    if (fc >= 0 && fc < NPAT && fc != S().cur) { S().cur = fc; if (!core->hostSynced.load()) S().bpm = pat().bpm; S().swing = pat().swing; cue = -1; latched.clear(); if (spage * 16 >= pat().len[tr]) spage = 0; schedulePreview(); }
    const int fcue = fb.cue.load(); if (playing && fcue != cue && fcue >= -1) { cue = fcue; needRepaint = true; }
    for (int t = 0; t < NTR; t++) { const int p = playing ? fb.pos[t].load() : -1; if (p != pos[t]) { pos[t] = p; if (t == tr) needRepaint = true; } }
    for (int t = 0; t < NT; t++) { const uint32_t f = fb.fired[t].load(); if (f != firedSeen[t]) { firedSeen[t] = f; flashUntil[t] = now + 90; needRepaint = true; } }
    const uint32_t ms = fb.meterSeq.load(std::memory_order_acquire);
    if (ms != meterSeq) {
      meterSeq = ms; for (int t = 0; t < NT; t++) trackPeak[t] = fb.peak[t].load();
      mpeak = fb.mpeak.load(); gr = fb.gr.load(); duckG = fb.duckG.load();
      meterHist.push_back(mpeak); while (meterHist.size() > 160) meterHist.pop_front();
      needRepaint = true;
    }
    if (heldStep >= 0 && !edited && !latchedByHold && now - holdT > 450) { latched.insert(heldStep); latchedByHold = true; schedulePreview(); }
    if (toastUntil > 0 && now > toastUntil) { toastUntil = 0; needRepaint = true; }
    for (int t = 0; t < NT; t++) if (flashUntil[t] > 0 && now > flashUntil[t]) { flashUntil[t] = 0; needRepaint = true; }
    if (waveDirty && now >= waveDue) { renderPreview(); needRepaint = true; }
    if (++cpuTick % 15 == 0) needRepaint = true;   // refresh CPU readout
    if (needRepaint && now - lastRepaint > 25) { needRepaint = false; lastRepaint = now; repaint(); }
  }
  void parameterChanged(uint32_t, float) override { needRepaint = true; }
  void stateChanged(const char*, const char*) override { needRepaint = true; }

  // ================= drawing primitives =================
  bool D() const { return ph == PH_DRAW; }
  // fontstash sizes by ascent-descent, CSS by em: convert so a CSS px size renders the same here
  float emFactor(FontId f) const { return f == fMark ? 1.4219f : (f == fPrint || f == fPrintB) ? 1.574f : f == fOled ? 1.28f : f == fNum ? 1.3f : 1.f; }
  void font(FontId f, float size, float spacing = 0) { fontFaceId(f); fontSize(size * emFactor(f)); textLetterSpacing(spacing); }
  float txt(float x, float y, const std::string& s, Color c, int align = ALIGN_LEFT | ALIGN_BASELINE) { cjk(s); fillColor(c); textAlign(align); return text(x, y, s.c_str(), nullptr); }
  float txtW(const std::string& s) { cjk(s); Rectangle<float> b; return textBounds(0, 0, s.c_str(), nullptr, b); }
  // names typed in Chinese / Japanese / Korean: the panel fonts have no such glyphs, so a system font is added as a
  // fallback the first time one is drawn (Microsoft YaHei on Windows, PingFang / STHeiti on macOS, Noto CJK /
  // WenQuanYi on Linux)
  void cjk(const std::string& s) {
    if (cjkTried) return;
    bool need = false;
    for (size_t i = 0; i < s.size() && !need; i++) { const unsigned char c = (unsigned char)s[i]; if (c >= 0xF0) need = true; else if (c >= 0xE0 && i + 1 < s.size()) { const unsigned cp = (c & 0x0F) << 12 | ((unsigned char)s[i + 1] & 0x3F) << 6; if (cp >= 0x2E80) need = true; } }
    if (!need) return;
    cjkTried = true;
#ifdef _WIN32
    const char* w = std::getenv("WINDIR"); const std::string fd = std::string(w && *w ? w : "C:\\Windows") + "\\Fonts\\";
    static const char* const F[] = { "msyh.ttc", "msyh.ttf", "simhei.ttf", "simsun.ttc", "YuGothM.ttc", "meiryo.ttc", "malgun.ttf" };
    for (const char* f : F) { const std::string p = fd + f; if (pathExists(p)) { fCjk = createFontFromFile("cjk", p.c_str()); if (fCjk >= 0) { cjkPath = p; break; } } }
#elif defined(__APPLE__)
    static const char* const F[] = { "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Medium.ttc", "/System/Library/Fonts/STHeiti Light.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
      "/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf" };
    for (const char* f : F) if (pathExists(f)) { fCjk = createFontFromFile("cjk", f); if (fCjk >= 0) { cjkPath = f; break; } }
#else
    static const char* const F[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf" };
    for (const char* f : F) if (pathExists(f)) { fCjk = createFontFromFile("cjk", f); if (fCjk >= 0) { cjkPath = f; break; } }
#endif
    if (fCjk >= 0) for (FontId f : { fMark, fPrint, fPrintB, fOled, fNum }) if (f >= 0) nvgAddFallbackFontId(getContext(), f, fCjk);
  }
  static void popChar(std::string& o) { while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back(); if (!o.empty()) o.pop_back(); }   // whole UTF-8 characters
  std::string fit(const std::string& s, float w) { if (txtW(s) <= w) return s; std::string o = s; while (!o.empty() && txtW(o + "\xe2\x80\xa6") > w) popChar(o); return o + "\xe2\x80\xa6"; }
  void rrect(R r, float rad, Color c) { beginPath(); roundedRect(r.x, r.y, r.w, r.h, rad); fillColor(c); fill(); }
  void rstroke(R r, float rad, Color c, float w = 1) { beginPath(); roundedRect(r.x + w / 2, r.y + w / 2, r.w - w, r.h - w, rad); strokeColor(c); strokeWidth(w); stroke(); }
  void vgrad(R r, float rad, Color a, Color b) { beginPath(); roundedRect(r.x, r.y, r.w, r.h, rad); fillPaint(linearGradient(r.x, r.y, r.x, r.b(), a, b)); fill(); }
  static std::string upper(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32); return s; }
  void label(float x, float y, const std::string& s) { if (!D()) return; font(fPrint, 11, 1.5f); txt(x, y, upper(s), th.print); }
  void ledDot(float x, float y, bool on, Color c) {
    if (on) { beginPath(); circle(x, y, 6); fillPaint(radialGradient(x, y, 1, 6, A(c, .55f), A(c, 0.f))); fill(); }
    beginPath(); circle(x, y, 2.5f); fillColor(on ? c : th.ledOff); fill();
  }

  // ---------- input helpers ----------
  bool press(int id, R r) {
    if (ph == PH_PRESS && !consumed && r.in(mx, my)) {
      if (midiLearn) { Action a; if (learnActionForId(id, a)) { consumed = true; setLearnTarget(a); return false; } }   // MIDI learn: the control becomes the target
      consumed = true; pressedId = id; dbl = (lastPressId == id && nowMs() - lastPressT < 330); lastPressId = id; lastPressT = nowMs(); return true;
    }
    return false;
  }
  bool clicked(int id, R r) { press(id, r); return ph == PH_RELEASE && pressedId == id && r.in(mx, my); }
  bool scrolled(R r) { if (ph == PH_SCROLL && !consumed && r.in(mx, my)) { consumed = true; return true; } return false; }

  // ---------- widgets ----------
  enum { K_SEL = 1, K_ON = 2, K_LIT = 4, K_DIM = 8, K_REC = 16, K_HAS = 32, K_CUE = 64, K_DOT = 128, K_ICPLAY = 256, K_ICSTOP = 512 };
  bool key(int id, R r, const std::string& lab, int flags = 0, float fs = 12) {
    const bool c = clicked(id, r);
    if (D()) {
      const bool down = pressedId == id && ph == PH_DRAW && dragActive();
      R k = r; if (down) k.y += 1;
      if (flags & K_DIM) globalAlpha(.35f);
      rrect(R(k.x, k.y + (down ? 1 : 2), k.w, k.h), 6, th.capLo);
      vgrad(k, 6, th.capHi, th.cap);
      Color edge = (flags & K_REC) && (flags & K_ON) ? th.rec : (flags & (K_SEL | K_ON)) ? th.led : th.line;
      if ((flags & K_CUE) && std::fmod(nowMs(), 500) < 250) edge = th.led;
      rstroke(k, 6, edge, 1);
      Color fc = (flags & K_ON) ? ((flags & K_REC) ? th.rec : th.led) : (flags & K_SEL) ? th.selFg : th.printHi;
      const float ly = (flags & (K_ICPLAY | K_ICSTOP)) ? k.cy() + 12 : k.cy() + fs * 0.36f;
      if (flags & K_ICPLAY) { beginPath(); moveTo(k.cx() - 4, k.cy() - 11); lineTo(k.cx() + 5, k.cy() - 6); lineTo(k.cx() - 4, k.cy() - 1); closePath(); fillColor(fc); fill(); }
      if (flags & K_ICSTOP) { beginPath(); rect(k.cx() - 4, k.cy() - 10, 8, 8); fillColor(fc); fill(); }
      font(fPrint, fs, fs * 0.1f); txt(k.cx(), ly, upper(lab), fc, ALIGN_CENTER | ALIGN_BASELINE);
      if (flags & K_DOT) ledDot(k.r() - 7, k.y + 7, (flags & K_LIT) != 0, th.led);
      if (flags & K_HAS) { beginPath(); roundedRect(k.cx() - 5, k.b() - 5, 10, 2, 1); fillColor(A(th.print, .6f)); fill(); }
      if (flags & K_DIM) globalAlpha(1);
    }
    return c;
  }
  bool dragActive() const { return true; }
  bool chip(int id, R r, const std::string& lab, bool sel, bool small = false, bool disabled = false) {
    const bool c = !disabled && clicked(id, r);
    if (D()) {
      if (disabled) globalAlpha(.3f);
      beginPath(); roundedRect(r.x + .5f, r.y + .5f, r.w - 1, r.h - 1, r.h / 2);
      if (sel) { fillColor(A(th.led, .08f)); fill(); }
      strokeColor(sel ? th.led : th.chipEdge); strokeWidth(1); stroke();
      font(fPrint, small ? 11 : 12, 1.1f); txt(r.cx(), r.cy() + 4.2f, upper(lab), sel ? th.led : (small ? th.print : th.printHi), ALIGN_CENTER | ALIGN_BASELINE);
      if (disabled) globalAlpha(1);
    }
    return c;
  }
  float chipW(const std::string& lab, bool small = false) { font(fPrint, small ? 11 : 12, 1.1f); return txtW(upper(lab)) + (small ? 20 : 24); }
  bool btn(int id, R r, const std::string& lab, bool pri = false) {
    const bool c = clicked(id, r);
    if (D()) {
      const bool hot = pressedId == id;
      rrect(r, 8, pri ? th.led : (hot ? th.capHi : th.cap)); rstroke(r, 8, pri ? th.led : th.line);
      font(fPrint, 12, 1.4f); txt(r.cx(), r.cy() + 4.3f, upper(lab), pri ? H(0x160903) : th.printHi, ALIGN_CENTER | ALIGN_BASELINE);
    }
    return c;
  }
  bool check(int id, R r, const std::string& lab, bool& v) {
    const bool c = clicked(id, r); if (c) v = !v;
    if (D()) {
      R b(r.x, r.cy() - 7, 14, 14); rrect(b, 3, v ? th.led : th.cap); rstroke(b, 3, v ? th.led : th.line);
      if (v) { beginPath(); moveTo(b.x + 3.5f, b.y + 7.5f); lineTo(b.x + 6, b.y + 10); lineTo(b.x + 10.5f, b.y + 4); strokeColor(H(0x160903)); strokeWidth(1.8f); stroke(); }
      font(fPrint, 13, 0.3f); txt(r.x + 22, r.cy() + 4.5f, lab, th.printHi);
    }
    return c;
  }
  // horizontal slider; value range [mn, mx], integer
  void slider(int id, R r, const std::string& lab, int& v, int mn, int mxv) {
    R track(r.x + 86, r.y, r.w - 86 - 40, r.h);
    if (press(id, track)) { drag.id = id; drag.kind = 3; drag.target = 0; sliderSet(id, track, mn, mxv, v); }
    if (D()) {
      font(fPrint, 11, 1.5f); txt(r.x, r.cy() + 4, upper(lab), th.print);
      rrect(R(track.x, track.cy() - 2, track.w, 4), 2, th.range);
      const float u = (float)(v - mn) / (float)std::max(1, mxv - mn), kx = track.x + 7 + u * (track.w - 14);
      beginPath(); circle(kx, track.cy(), 7); fillColor(th.modBg); fill(); beginPath(); circle(kx, track.cy(), 5.5f); fillColor(th.printHi); fill();
      font(fNum, 12); txt(r.r(), r.cy() + 4, std::to_string(v), th.printHi, ALIGN_RIGHT | ALIGN_BASELINE);
    }
    sliderRects[id] = { track, mn, mxv };
  }
  struct SlInfo { R r; int mn, mx; };
  std::map<int, SlInfo> sliderRects;
  int* sliderVar(int id) { switch (id) { case ID_DEN: return &denPct; case ID_VAR: return &varPct; case ID_EK: return &eK; case ID_ER: return &eR; case ID_SMPTAB + 10: return &sliceSens; } return nullptr; }
  int denPct = 55, varPct = 40;
  void sliderSet(int id, R track, int mn, int mxv, int& v) {
    const float u = clampd((mx - track.x - 7) / (track.w - 14), 0, 1); v = (int)jsround(mn + u * (mxv - mn));
    if (id == ID_DEN) gopt.den = v / 100.0; if (id == ID_VAR) gopt.vari = v / 100.0;
  }
  void knob(float cx, float cy, float rad, double norm, bool locked, bool empty) {
    if (empty) globalAlpha(.28f);
    beginPath(); circle(cx, cy + 3, rad + 1); fillColor(th.knobSh); fill();
    beginPath(); circle(cx, cy, rad + 3); fillColor(th.knobR2); fill();
    beginPath(); circle(cx, cy, rad + 2); fillColor(th.knobR1); fill();
    beginPath(); circle(cx, cy, rad); fillPaint(radialGradient(cx, cy - rad * 0.24f, rad * 0.1f, rad * 1.05f, th.knobA, th.knobC)); fill();
    const float ir = rad - 9;
    for (int k = 0; k < 30; k++) { const float a0 = k * (float)TAU / 30, a1 = a0 + (float)TAU / 60; beginPath(); moveTo(cx, cy); arc(cx, cy, ir, a0, a1, CW); closePath(); fillColor(th.grip1); fill(); }
    beginPath(); circle(cx, cy, ir * 0.35f); fillColor(A(th.grip2, .9f)); fill();
    if (!empty) {
      const float ang = (float)((-135 + norm * 270) * PI / 180);
      save(); translate(cx, cy); rotate(ang);
      if (locked) { beginPath(); roundedRect(-4, -rad + 1, 8, 16, 3); fillColor(A(th.lock, .35f)); fill(); }
      beginPath(); roundedRect(-1.5f, -rad + 4, 3, 12, 1.5f); fillColor(locked ? th.lock : th.printHi); fill(); restore();
    }
    if (empty) globalAlpha(1);
  }
  void numBox(int id, R r, const std::string& lab, const std::string& val, bool ro = false) {
    if (!ro && press(id, r)) { drag.id = id; drag.kind = 2; drag.y = my; drag.acc = numGet(id); }
    if (!ro && scrolled(r)) numSet(id, numGet(id) + (wheel > 0 ? 1 : -1) * ((mods & kModifierShift) ? numFine(id) : numCoarse(id)));
    if (D()) {
      rrect(r, 6, th.oledBg); rstroke(r, 6, th.numEdge); rstroke(R(r.x - 1, r.y - 1, r.w + 2, r.h + 2), 7, th.numRing);
      font(fPrint, 10, 1.4f); txt(r.x + 8, r.y + 15, upper(lab), th.oledMid);
      font(fNum, 18); txt(r.x + 8, r.y + 37, val, ro ? th.oledMid : th.oled);
    }
  }
  double numGet(int id) {
    switch (id) { case ID_BPM: return S().bpm; case ID_SWING: return S().swing; case ID_LEN: return pat().len[tr]; case ID_SPD: return pat().scl[tr]; case ID_MLEN: return pat().mlen; }
    return 0;
  }
  double numFine(int id) { return id == ID_BPM ? 0.1 : 1; }
  double numCoarse(int id) { return 1; (void)id; }
  void numSet(int id, double v) {
    switch (id) {
      case ID_BPM: v = clampd(jsround(v * 10) / 10, 40, 300); S().bpm = v; pat().bpm = v; core->sendSimple(Cmd::BPM, 0, 0, v); break;
      case ID_SWING: v = clampd(jsround(v), 50, 80); S().swing = v; pat().swing = v; core->sendSimple(Cmd::SWING, 0, 0, v); break;
      case ID_LEN: pat().len[tr] = (int)clampd(jsround(v), 1, 64); sendLen(); break;
      case ID_SPD: pat().scl[tr] = (int)clampd(jsround(v), 0, 6); sendLen(); break;
      case ID_MLEN: pat().mlen = (int)clampd(jsround(v), 1, 64); sendLen(); break;
    }
    needRepaint = true;
  }
  // param control shared by encoders and OLED cells
  void paramCtl(int id, R r, int slot) {
    const int i = pageBase() + slot; const PDef& d = defOf(i);
    if (!d.valid()) { press(id, r); return; }
    if (press(id, r)) {
      if (dbl) { resetVal(i); dbl = false; }
      else { drag.id = id; drag.kind = 1; drag.y = my; drag.acc = getVal(i); drag.target = i; }
    }
    if (scrolled(r)) { const double range = d.max - d.min; setVal(i, stepRound(getVal(i) + (wheel > 0 ? 1 : -1) * (range >= 50 ? ((mods & kModifierShift) ? 0.2 : 1) : 1), d), false); }
  }

  // ================= frame =================
  void frame() {
    sliderRects.clear(); faderRects.clear(); catDrop.clear();
    if (keyLearn.k != AC_NONE && !(drawer && labTab == TAB_CONTROL)) keyLearn = Action();
    if (te.act != TE_NONE && !(drawer && ((labTab == TAB_SOUND && sndSrc == 1 && teActive(1)) || (labTab == TAB_GROOVE && grvSrc == 1 && teActive(2)) || (labTab == TAB_KITS && kitSrc == 1 && teActive(3))))) te = TextEdit();   // field no longer visible
    if (D()) {
      beginPath(); rect(0, 0, WW, HCLOSED + (drawer ? LABH : 0)); fillColor(th.page); fill();
    }
    header();
    device();
    pullRow();
    if (drawer) lab();
  }
  void header() {
    if (!D()) return;
    font(fMark, 26, 26 * 0.32f); const float w = txt(24, 44, "HARDER", th.printHi);
    font(fPrint, 12, 12 * 0.22f); txt(24 + w + 18, 42, "SYNTH PERCUSSION & GROOVE BOX", th.print);
    // status pill: transport source and CPU
    const bool hs = core->hostSynced.load(), hp = core->hostPlaying.load();
    char b[96];
    if (standalone && midiLearn) std::snprintf(b, sizeof b, "MIDI LEARN  \xc2\xb7  %s", learnAct.k == AC_NONE ? "CLICK A CONTROL" : "MOVE THE CONTROLLER");
    else if (standalone) {
      if (!isAudioDeviceOpen()) std::snprintf(b, sizeof b, "NO AUDIO OUTPUT  \xc2\xb7  LAB > AUDIO");
      else if (core->extClock.load()) std::snprintf(b, sizeof b, "MIDI CLOCK %.1f BPM  \xc2\xb7  %u HZ", core->extBpm.load(), audioRate());
      else std::snprintf(b, sizeof b, "%s  \xc2\xb7  %u HZ  \xc2\xb7  %u", playing ? "PLAYING" : "STANDALONE", audioRate(), audioBuffer());
    } else if (hs || hp) std::snprintf(b, sizeof b, "LIVE SYNC  %.1f BPM", core->hostBpm.load()); else std::snprintf(b, sizeof b, playing ? "INTERNAL CLOCK" : "WAITING FOR LIVE  \xc2\xb7  PLAY = INTERNAL");
    char c[32]; std::snprintf(c, sizeof c, "DSP %.1f%%", core->cpuPermille.load() / 10.0);   // plugin audio time / buffer time, 0.5 s average
    font(fPrint, 11, 1.6f); const float tw = txtW(b) + txtW(c) + 64;
    R pill(WW - 24 - tw, 24, tw, 30); rrect(pill, 15, th.cap); rstroke(pill, 15, th.line);
    if (standalone && midiLearn) ledDot(pill.x + 16, pill.cy(), std::fmod(nowMs(), 600) < 350, th.led);
    else if (standalone) ledDot(pill.x + 16, pill.cy(), playing || !isAudioDeviceOpen(), isAudioDeviceOpen() ? th.ok : th.rec);
    else ledDot(pill.x + 16, pill.cy(), hs && playing, hs ? th.ok : th.led);
    txt(pill.x + 30, pill.cy() + 4, b, th.printHi);
    txt(pill.r() - 14, pill.cy() + 4, c, th.print, ALIGN_RIGHT | ALIGN_BASELINE);
  }
  void device() {
    const float X = PX, Y = PY;
    if (D()) {
      beginPath(); roundedRect(X, Y + 6, PW, PH, 16); fillColor(th.shadow); fill();
      vgrad(R(X, Y, PW, PH), 16, th.bodyHi, th.body); rstroke(R(X, Y, PW, PH), 16, th.edge);
      beginPath(); moveTo(X + 16, Y + 1.5f); lineTo(X + PW - 16, Y + 1.5f); strokeColor(th.hl); strokeWidth(1); stroke();
      for (auto& sc : { std::pair<float, float>{ X + 13, Y + 13 }, { X + PW - 13, Y + 13 }, { X + 13, Y + PH - 13 }, { X + PW - 13, Y + PH - 13 } }) {
        beginPath(); circle(sc.first, sc.second, 4.5f); fillPaint(radialGradient(sc.first - 1.5f, sc.second - 1.5f, 0.5f, 5, th.screwHi, th.screwLo)); fill();
        save(); translate(sc.first, sc.second); rotate(0.61f); beginPath(); rect(-3.5f, -0.5f, 7, 1); fillColor(th.screwSlot); fill(); restore();
      }
    }
    const float x0 = X + 22, y0 = Y;
    // patterns, and the sequencing mode: PTN / CHN (chain) / SNG (song)
    label(x0, y0 + 28, "PATTERN");
    { static const char* const SQ[3] = { "PTN", "CHN", "SNG" };
      for (int k = 0; k < 3; k++) if (chip(ID_SEQ + k, R(x0 + 87 + k * 35, y0 + 15, 33, 17), SQ[k], S().seq == k, true)) setSeq(k); }
    if (S().seq != ARR_OFF) {
      const int ar = playing ? core->eng->fb.arrRow.load() : -1;
      std::string line; Color col = th.print;
      if (S().seq == ARR_CHAIN) {
        if (S().chain.empty()) line = "CHAIN EMPTY \xc2\xb7 PRESS PATTERNS";
        else { const int n = (int)S().chain.size(); line = std::to_string(ar >= 0 && ar < n ? ar + 1 : 0) + "/" + std::to_string(n) + "  " + chainText(14); if (ar >= 0) col = th.printHi; }
        if (chip(ID_CHCLR, R(x0 + 171, y0 + 110, 18, 15), "\xc3\x97", false, true, S().chain.empty())) { pushUndo(); S().chain.clear(); core->sendArr(); toast("CHAIN CLEARED"); }
      } else {
        if (!S().song.n) line = "SONG EMPTY \xc2\xb7 LAB > SONG";
        else if (ar >= 0) { line = "ROW " + std::to_string(ar + 1) + "/" + std::to_string(S().song.n) + "  \xc3\x97" + std::to_string(core->eng->fb.arrRep.load() + 1) + "/" + std::to_string(S().song.rows[std::min(ar, S().song.n - 1)].reps); col = th.printHi; }
        else line = "SONG \xc2\xb7 " + std::to_string(S().song.n) + " ROWS \xc2\xb7 FROM ROW " + std::to_string(S().song.start + 1);
      }
      if (D()) { font(fPrint, 10.5f, 1.0f); txt(x0, y0 + 122, fit(line, S().seq == ARR_CHAIN ? 166 : 189), col); }
    }
    for (int p = 0; p < NPAT; p++) {
      bool has = false; for (int t = 0; t < NTR && !has; t++) for (int s = 0; s < NSTEP; s++) if (S().patterns[p].steps[t][s].on) { has = true; break; }
      R r(x0 + (p % 4) * 49, y0 + 38 + (p / 4) * 34, 43, 27);
      if (key(ID_PAT + p, r, "P" + std::to_string(p + 1), (p == S().cur ? K_SEL : 0) | (has ? K_HAS : 0) | (p == cue ? K_CUE : 0) | K_DOT | (p == S().cur && playing ? K_LIT : 0))) patternPress(p);
    }
    // tempo, swing, length
    if (S().seq == ARR_OFF) label(x0, y0 + 122, "TEMPO / SWING");   // chain / song: their readout is drawn there instead
    const bool hs = core->hostSynced.load() || core->hostPlaying.load(), ec = core->extClock.load();
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", ec ? core->extBpm.load() : hs ? core->hostBpm.load() : S().bpm); numBox(ID_BPM, R(x0, y0 + 132, 91, 46), ec ? "BPM \xc2\xb7 CLOCK" : hs ? "BPM \xc2\xb7 LIVE" : "BPM", b, hs || ec);
    std::snprintf(b, sizeof b, "%d%%", (int)jsround(S().swing)); numBox(ID_SWING, R(x0 + 98, y0 + 132, 91, 46), "SWING", b);
    label(x0, y0 + 198, "LENGTH \xc2\xb7 SPEED");
    numBox(ID_LEN, R(x0, y0 + 208, 59, 46), "LEN", std::to_string(pat().len[tr]));
    numBox(ID_SPD, R(x0 + 65, y0 + 208, 59, 46), "SPEED", SPEEDS[pat().scl[tr]]);
    numBox(ID_MLEN, R(x0 + 130, y0 + 208, 59, 46), "PTN", std::to_string(pat().mlen));
    oled(R(X + 232, Y + 22, 574, 256));
    // encoders
    for (int k = 0; k < 8; k++) {
      const float cx = X + 862 + (k % 4) * 77, cy = Y + 96 + (k / 4) * 90;
      R r(cx - 36, cy - 34, 72, 84); paramCtl(ID_ENC + k, r, k);
      if (D()) {
        const int i = pageBase() + k; const PDef& d = defOf(i); double lv; const bool lk = d.valid() && !heldSteps().empty() && lockOf(i, lv);
        const double v = d.valid() ? getVal(i) : 0, norm = d.valid() ? (v - d.min) / (d.max - d.min) : 0;
        const bool dim = d.valid() && ((d.step && heldSteps().empty()) || (!heldSteps().empty() && !d.lock));
        if (dim) globalAlpha(.45f);
        knob(cx, cy, 26, norm, lk, !d.valid());
        font(fPrint, 11, 1.5f); txt(cx, cy + 46, d.valid() ? d.n : "\xe2\x80\x94", th.print, ALIGN_CENTER | ALIGN_BASELINE);
        if (dim) globalAlpha(1);
      }
    }
    // pages
    label(x0, y0 + 318, "PAGE");
    { const int n = isMaster() ? 4 : 7;
      for (int k = 0; k < n; k++) { R r(x0 + k * 60, y0 + 328, 54, 27); const int cur = isMaster() ? mpage : page;
        if (key(ID_PAGE + k, r, isMaster() ? MASTER_PAGES[k] : PAGES[k], k == cur ? K_SEL : 0)) { if (isMaster()) mpage = k; else page = k; schedulePreview(); } } }
    // tracks
    label(X + 456, y0 + 306, "TRACK");
    for (int t = 0; t < NTR; t++) {
      R r(X + 456 + t * 75.5f, y0 + 314, 69, 41);
      if (press(ID_TRACK + t, r)) trackPress(t);
      if (D()) {
        const bool rowMuted = playing && t < 8 && ((core->eng->fb.rowMute.load() >> t) & 1);   // muted by the song row
        const bool sel = tr == t, mu = t < 8 && (S().mutes[t] || rowMuted), so = soloTr >= 0 && t < 8 && t != soloTr;   // silenced by mute or by another track's solo
        if (mu || so) globalAlpha(.45f);
        rrect(R(r.x, r.y + 2, r.w, r.h), 6, th.capLo); vgrad(r, 6, th.capHi, th.cap); rstroke(r, 6, sel ? th.led : th.line);
        font(fNum, 13); txt(r.x + 7, r.y + 16, t < 8 ? std::to_string(t + 1) : "FX", sel ? th.selFg : th.printHi);
        if (mu) { font(fNum, 10); txt(r.x + 18, r.y + 16, "M", th.warn); }
        if (t == soloTr) { font(fNum, 10); txt(r.x + (mu ? 28 : 18), r.y + 16, "S", th.led); }
        if (!standalone && t < 8 && S().outs[t]) { font(fNum, 9); txt(r.r() - 14, r.y + 16, "O", th.ok, ALIGN_RIGHT | ALIGN_BASELINE); }
        std::string nm = t < 8 ? (S().tracks[t].pre.size() ? upper(S().tracks[t].pre) : MACH_NAMES[S().tracks[t].m]) : "MASTER";
        font(fPrint, 10, 0.8f); txt(r.x + 7, r.y + 29, fit(nm, r.w - 14), th.print);
        ledDot(r.r() - 7, r.y + 7, t < 8 && flashUntil[t] > 0, th.led);
        float mw = 0;
        if (t < 8) { const float db = 20 * std::log10(trackPeak[t] + 1e-9f); mw = (float)clampd((db + 48) / 48, 0, 1); } else mw = 1 - duckG;
        rrect(R(r.x + 7, r.b() - 5, r.w - 14, 2), 1, th.well); if (mw > 0) rrect(R(r.x + 7, r.b() - 5, (r.w - 14) * mw, 2), 1, th.printHi);
        if (mu || so) globalAlpha(1);
      }
    }
    // function keys
    {
      struct FK { int id; const char* l; int fl; } fk[8] = {
        { ID_FUNC, "FUNC", funcLatch || shiftDown ? K_ON : 0 }, { ID_REC, "REC", (recOn ? K_ON : 0) | K_REC }, { ID_FILL, "FILL", fillHeld || fillLatch ? K_ON : 0 }, { ID_ALL, "ALL", allLatch ? K_ON : 0 },
        { ID_PLAY, "PLAY", (playing ? K_ON : 0) | K_ICPLAY }, { ID_STOP, "STOP", K_ICSTOP }, { ID_PERF, "PERF", perfMode ? K_ON : 0 }, { ID_UNDO, "UNDO", 0 } };
      for (int k = 0; k < 8; k++) {
        R r(x0 + (k % 4) * 58, y0 + 372 + (k / 4) * 46, 52, 40);
        if (fk[k].id == ID_FILL) {   // momentary
          if (press(ID_FILL, r)) { if (func()) { fillLatch = !fillLatch; setFill(fillLatch); } else setFill(true); }
          if (ph == PH_RELEASE && pressedId == ID_FILL && !fillLatch) setFill(false);
          key(-1, r, fk[k].l, fk[k].fl, 11);
          continue;
        }
        if (key(fk[k].id, r, fk[k].l, fk[k].fl, 11)) {
          switch (fk[k].id) {
            case ID_FUNC: funcLatch = !funcLatch; break;
            case ID_REC: recOn = !recOn; if (recOn) pushUndo(); toast(recOn ? "LIVE REC ON" : "LIVE REC OFF"); break;
            case ID_ALL: allLatch = !allLatch; break;
            case ID_PLAY: play(); break;
            case ID_STOP: stop(); break;
            case ID_PERF: if (isMaster()) toast("SELECT A TRACK"); else { perfMode = !perfMode; latched.clear(); schedulePreview(); } break;
            case ID_UNDO: undo(); break;
          }
        }
      }
    }
    trigs(X + 268, y0 + 387);
    // step pages
    { const int len = pat().len[tr], pages = (len + 15) / 16, playPage = pos[tr] >= 0 ? pos[tr] / 16 : -1;
      for (int p = 0; p < 4; p++) {
        R r(X + 1038 + (p % 2) * 46, y0 + 387 + (p / 2) * 31, 40, 25);
        char l[16]; std::snprintf(l, sizeof l, "%d\xe2\x80\x93%d", p * 16 + 1, p * 16 + 16);
        if (key(ID_SPAGE + p, r, l, (p == spage ? K_SEL : 0) | (p >= pages ? K_DIM : 0) | K_DOT | (p == playPage ? K_LIT : 0), 9.5f)) spage = p;
      } }
    // edit row
    { float x = x0; const char* L[4] = { "COPY", "PASTE", "CLEAR", "CLEAR LOCKS" }; const float W4[4] = { 62, 68, 70, 124 };
      for (int k = 0; k < 4; k++) { R r(x, y0 + 476, W4[k], 27); if (key(ID_COPY + k, r, L[k], 0, 11.5f)) { if (k == 0) copyBtn(); else if (k == 1) pasteBtn(); else if (k == 2) clearBtn(); else clearLocks(); } x += W4[k] + 7; } }
    levelStrip(X, y0);
  }

  // ================= level strip: selected track (M, S, volume) and output level =================
  // IEC 60268-18 style scale: more room near 0 dB. 0 dB = 100, +6 dB = 115
  static double iec(double db) {
    if (db < -70) return 0; if (db < -60) return (db + 70) * 0.25; if (db < -50) return 2.5 + (db + 60) * 0.5;
    if (db < -40) return 7.5 + (db + 50) * 0.75; if (db < -30) return 15 + (db + 40) * 1.5; if (db < -20) return 30 + (db + 30) * 2;
    return 50 + (db + 20) * 2.5;
  }
  static double iecInv(double p) {
    if (p <= 0) return -200; if (p < 2.5) return p / 0.25 - 70; if (p < 7.5) return (p - 2.5) / 0.5 - 60;
    if (p < 15) return (p - 7.5) / 0.75 - 50; if (p < 30) return (p - 15) / 1.5 - 40; if (p < 50) return (p - 30) / 2 - 30;
    return (p - 50) / 2.5 - 20;
  }
  static double volToDb(double v) { return v <= 0 ? -200 : 40 * std::log10(v / 100); }         // track VOL: gain (v/100)^2
  static double dbToVol(double db) { return db <= -69.9 ? 0 : 100 * std::pow(10.0, db / 40); }
  static std::string dbText(double db) { if (db <= -69.9) return "\xe2\x88\x92\xe2\x88\x9e dB"; char b[24]; std::snprintf(b, sizeof b, "%+.1f dB", std::fabs(db) < 0.05 ? 0.0 : db); return b; }
  float faderX(const R& r, double db, double maxDb) const { return r.x + (float)(clampd(iec(std::min(db, maxDb)) / iec(maxDb), 0, 1)) * r.w; }
  double faderDbAt(const R& r, float x, double maxDb) const { return std::min(maxDb, iecInv(clampd((x - r.x) / r.w, 0, 1) * iec(maxDb))); }
  // meter well + level + peak hold + fader cap; value changes go through applyFader
  void fader(int id, R r, double db, double maxDb, double levelDb, double holdDb, bool ticks, bool enabled) {
    faderRects[id] = { r, maxDb };
    const R hit(r.x - 8, r.y - 12, r.w + 16, 26);
    if (enabled && press(id, hit)) {
      faderEdited = false; drag = Drag(); drag.id = id; drag.kind = 4; drag.y = mx;
      if (dbl) applyFader(id, 0); else applyFader(id, faderDbAt(r, mx, maxDb));
    }
    if (enabled && scrolled(hit)) applyFader(id, clampd((db < -69.9 ? -70 : db) + (wheel > 0 ? 1 : -1) * ((mods & kModifierShift) ? 0.1 : 0.5), -70, maxDb), true);
    if (!D()) return;
    if (!enabled) globalAlpha(.35f);
    const R well(r.x, r.y - 4, r.w, 8); rrect(well, 4, th.well);
    if (levelDb > -70) { const float w = faderX(r, levelDb, maxDb) - r.x - 2; rrect(R(r.x + 2, well.y + 2, std::max(2.f, w), well.h - 4), 2, levelDb > -1 ? th.warn : A(th.printHi, .62f)); }
    if (holdDb > -70) { const float hx = faderX(r, holdDb, maxDb); beginPath(); rect(hx - 1, well.y, 2, well.h); fillColor(holdDb > -1 ? th.warn : th.printHi); fill(); }
    // scale
    const float z = faderX(r, 0, maxDb);
    beginPath(); moveTo(z - 3, r.y + 7); lineTo(z + 3, r.y + 7); lineTo(z, r.y + 4); closePath(); fillColor(th.print); fill();   // 0 dB mark
    if (ticks) {
      static const int TK[] = { -40, -30, -20, -12, -9, -6, -3, 0 }; static const bool LB[] = { true, false, true, true, false, true, true, true };
      font(fNum, 8.5f);
      for (int k = 0; k < 8; k++) {
        const float tx = faderX(r, TK[k], maxDb);
        beginPath(); rect(tx - .5f, r.y + 6, 1, LB[k] ? 4 : 2.5f); fillColor(A(th.print, .8f)); fill();
        if (LB[k] && TK[k] != 0) txt(tx, r.y + 19, std::to_string(TK[k]), th.print, ALIGN_CENTER | ALIGN_BASELINE);
      }
      txt(z, r.y + 19, "0", th.print, ALIGN_CENTER | ALIGN_BASELINE);
    }
    // cap
    const float cxp = faderX(r, db, maxDb); const R cap(cxp - 5, r.y - 11, 10, 22); const bool hot = drag.kind == 4 && drag.id == id;
    rrect(R(cap.x, cap.y + 1.5f, cap.w, cap.h), 3, th.capLo); vgrad(cap, 3, th.capHi, th.cap); rstroke(cap, 3, hot ? th.led : th.line);
    beginPath(); rect(cap.cx() - 0.75f, cap.y + 4, 1.5f, cap.h - 8); fillColor(hot ? th.led : th.printHi); fill();
    if (!enabled) globalAlpha(1);
  }
  void applyFader(int id, double db, bool oneShot = false) {
    if (id == ID_OFADER) {
      const float v = (float)(db <= -69.9 ? -80 : clampd(jsround(db * 10) / 10, -70, 0));
      if (v != outDb) { outDb = v; core->outDb.store(v); saveUiState(); }
    } else if (id == ID_TFADER && tr < 8) {
      const double v = clampd(jsround(dbToVol(db) * 10) / 10, 0, 127);
      if (v != S().tracks[tr].p[21]) {
        if (!faderEdited) { pushUndo(); faderEdited = true; }
        setBase(tr, 21, v, !oneShot); schedulePreview();
      }
      if (oneShot) faderEdited = false;
    }
    needRepaint = true;
  }
  void toggleMute(int t) { if (t >= 8) return; S().mutes[t] = S().mutes[t] ? 0 : 1; core->sendSimple(Cmd::MUTE, t, 0, 0, S().mutes[t]); hostEdit(HP_MUTE0 + t); }
  void toggleSolo(int t) { if (t >= 8) return; soloTr = soloTr == t ? -1 : t; core->sendSimple(Cmd::SOLO, soloTr); toast(soloTr >= 0 ? "SOLO TRACK " + std::to_string(t + 1) : "SOLO OFF"); }
  void levelStrip(float X, float y0) {
    const double now = nowMs(); const bool isTrk = tr < 8;
    // ---- selected track
    const float tx = X + 392;
    if (D()) {
      font(fNum, 13); txt(tx, y0 + 488, isTrk ? "T" + std::to_string(tr + 1) : "FX", th.printHi);
      const std::string nm = isTrk ? (S().tracks[tr].pre.size() ? upper(S().tracks[tr].pre) : MACH_NAMES[S().tracks[tr].m]) : "NO AUDIO";
      font(fPrint, 9.5f, 0.6f); txt(tx, y0 + 501, fit(nm, 60), th.print);
    }
    if (key(ID_MUTE, R(tx + 64, y0 + 476, 28, 27), "M", (isTrk && S().mutes[tr] ? K_ON : 0) | (isTrk ? 0 : K_DIM), 11.5f) && isTrk) toggleMute(tr);
    if (key(ID_SOLO, R(tx + 96, y0 + 476, 28, 27), "S", (isTrk && soloTr == tr ? K_ON : 0) | (isTrk ? 0 : K_DIM), 11.5f) && isTrk) toggleSolo(tr);
    double tdb = -200, tlev = -200;
    if (isTrk) {
      const double* p = S().tracks[tr].p; tdb = volToDb(p[21]);
      const double pan = (p[20] + 64) / 127.5, pg = std::max(std::cos(pan * PI / 2), std::sin(pan * PI / 2)) * 1.41421;
      const double lin = trackPeak[tr] * std::pow(p[21] / 100, 2) * pg * (S().mutes[tr] ? 0 : 1);   // after the track fader and pan
      tlev = lin > 1e-9 ? 20 * std::log10(lin) : -200;
      if (trkHoldTr != tr) { trkHoldTr = tr; trkHoldDb = -200; }
      if (tlev > trkHoldDb || now - trkHoldT > 1500) { trkHoldDb = (float)tlev; trkHoldT = now; }
    }
    fader(ID_TFADER, R(tx + 136, y0 + 486, 164, 0), tdb, 6, tlev, isTrk ? trkHoldDb : -200, false, isTrk);
    if (D()) { font(fNum, 11); txt(tx + 356, y0 + 490, isTrk ? dbText(tdb) : "", th.printHi, ALIGN_RIGHT | ALIGN_BASELINE); }
    if (D()) { beginPath(); rect(X + 764, y0 + 474, 1, 32); fillColor(th.line); fill(); }
    // ---- output
    const float ox = X + 812;
    if (D()) { font(fPrint, 11, 1.6f); txt(X + 778, y0 + 490, "OUT", th.print); }
    const double og = outDb <= -80 ? 0 : std::pow(10.0, outDb / 20.0), olin = mpeak * og, olev = olin > 1e-9 ? 20 * std::log10(olin) : -200;
    if (olev > outHoldDb || now - outHoldT > 1500) { outHoldDb = (float)olev; outHoldT = now; }
    fader(ID_OFADER, R(ox, y0 + 486, 226, 0), outDb <= -80 ? -200 : outDb, 0, olev, outHoldDb, true, true);
    if (D()) {
      font(fNum, 11); txt(X + PW - 22, y0 + 490, dbText(outDb <= -80 ? -200 : outDb), th.printHi, ALIGN_RIGHT | ALIGN_BASELINE);
      const double grDb = 20 * std::log10(gr > 0 ? gr : 1); char b2[32]; std::snprintf(b2, sizeof b2, "LIMIT %.1f", std::fabs(grDb) < 0.05 ? 0.0 : grDb);
      font(fPrint, 9.5f, 1.0f); txt(X + PW - 22, y0 + 503, b2, grDb < -0.5 ? th.warn : th.print, ALIGN_RIGHT | ALIGN_BASELINE);
    }
  }
  void trigs(float x, float y) {
    const Pattern& P = pat(); const auto hsv = heldSteps(); std::set<int> hs(hsv.begin(), hsv.end());
    for (int k = 0; k < 16; k++) {
      R r(x + k * 47.2f, y, 41, 56);
      const int s = spage * 16 + k;
      if (perfMode && !isMaster()) {
        if (press(ID_TRIG + k, r)) perfDown(k);
        if (ph == PH_RELEASE && pressedId == ID_TRIG + k) perfUp(k);
        if (D()) {
          const PerfPad& pp = PERF[k]; R b = r; if (perfHeld == k) b.y += 2;
          rrect(R(r.x, r.y + 3, r.w, r.h), 7, th.trigDrop); vgrad(b, 7, th.pfA, th.pfB); rstroke(b, 7, th.trigEdge);
          rrect(R(b.x + r.w * .22f, b.y + 9, r.w * .56f, 7), 2, perfHeld == k ? th.led : th.pf[pp.g]);
          std::string lab = pp.lab, cap;
          if (pp.g == 1) { lab = "VEL"; cap = std::to_string(pp.vel); }
          else if (pp.g >= 2) { int pi; double v; const PDef* d; perfValue(k, pi, v, d); lab = d->valid() ? d->n : "\xe2\x80\x94"; cap = d->valid() ? formatValue(*d, v) : ""; }
          else cap = "ROLL";
          font(fPrint, 11, .7f); txt(b.cx(), b.y + 34, fit(lab, r.w - 4), th.printHi, ALIGN_CENTER | ALIGN_BASELINE);
          font(fNum, 9); txt(b.cx(), b.b() - 6, fit(cap, r.w - 4), th.print, ALIGN_CENTER | ALIGN_BASELINE);
        }
        continue;
      }
      if (press(ID_TRIG + k, r)) {
        if ((mods & kModifierShift) || funcLatch) { /* trigless on release */ }
        heldStep = s; edited = false; latchedByHold = false; holdT = nowMs(); schedulePreview();
      }
      if (ph == PH_RELEASE && pressedId == ID_TRIG + k && heldStep >= 0) {
        const int hsS = heldStep; heldStep = -1;
        if (!edited && !latchedByHold) {
          if (latched.count(hsS)) latched.erase(hsS);
          else if (func()) toggleTrigless(hsS);
          else toggleStep(hsS);
        }
        schedulePreview();
      }
      if (D()) {
        const Step& st = P.steps[tr][s];
        bool locks = false, cond = false, lc = false, sl = false;
        if (st.on) for (int i = 0; i < NP; i++) if (st.l.has(i)) {
          if (isMaster() || i <= 40 || i == 49 || i == 50) locks = true;
          if ((i == 43 && st.l.v[i] != 0) || (i == 42 && st.l.v[i] < 100)) cond = true;
          if ((i == 49 && st.l.v[i] != 0) || (i == 50 && st.l.v[i] < 100)) lc = true;
          if (i == 48 && st.l.v[i] >= 0.5) sl = true;
        }
        const bool out = s >= P.len[tr], held = hs.count(s) > 0, phd = pos[tr] == s;
        if (out) globalAlpha(.32f);
        R b = r; if (held) b.y += 2;
        rrect(R(r.x, r.y + (held ? 1 : 3), r.w, r.h), 7, th.trigDrop);
        if (k % 4 == 0) vgrad(b, 7, th.trigC, th.trigD); else vgrad(b, 7, th.trigA, th.trigB);
        rstroke(b, 7, held ? th.lock : phd ? th.printHi : th.trigEdge, held || phd ? 2 : 1);
        R led(b.x + r.w * .22f, b.y + 9, r.w * .56f, 7);
        if (st.on && st.tl) rstroke(led, 2, th.lock, 2);
        else if (st.on) {
          const Color c = locks ? th.lock : th.led;
          beginPath(); roundedRect(led.x - 5, led.y - 5, led.w + 10, led.h + 10, 6); fillPaint(boxGradient(led.x, led.y, led.w, led.h, 3, 8, A(c, .45f), A(c, 0.f))); fill();
          rrect(led, 2, c);
        } else rrect(led, 2, phd ? th.phLed : th.ledDim);
        if (sl) { beginPath(); moveTo(led.r() + 3, led.y); lineTo(led.r() + 9, led.cy()); lineTo(led.r() + 3, led.b()); closePath(); fillColor(th.lock); fill(); }
        if (cond) { beginPath(); circle(b.cx() + (lc ? -4 : 0), b.y + 22, 2); fillColor(th.printHi); fill(); }
        if (lc) { beginPath(); circle(b.cx() + 6, b.y + 22, 2); fillColor(th.lock); fill(); }
        font(fNum, 10); txt(b.cx(), b.b() - 7, std::to_string(s + 1), th.print, ALIGN_CENTER | ALIGN_BASELINE);
        if (out) globalAlpha(1);
      }
    }
  }
  std::string tuneReadout() {
    const TrackM& t = S().tracks[tr]; const int lo = TUNE_LO_HI[t.m][0], hi = TUNE_LO_HI[t.m][1]; if (!lo) return "";
    const double f = expm(getVal(0), lo, hi) * std::pow(2.0, getVal(40) / 12);
    const double midi = 69 + 12 * std::log2(f / 440); const int n = (int)jsround(midi), cents = (int)jsround((midi - n) * 100);
    char b[96]; std::snprintf(b, sizeof b, "TUNE %sHZ = %s%d %s%dC", f >= 1000 ? (toFixed(f / 1000, 2) + "K").c_str() : toFixed(f, 1).c_str(), NOTE_NAMES[((n % 12) + 12) % 12], (int)std::floor(n / 12.0) - 1, cents >= 0 ? "+" : "", cents);
    return b;
  }
  void oled(R bz) {
    const R sc(bz.x + 9, bz.y + 9, bz.w - 18, bz.h - 18);
    if (D()) {
      rrect(R(bz.x, bz.y + 4, bz.w, bz.h), 14, th.shadow); rrect(bz, 14, th.bezel); rstroke(bz, 14, th.bezelRing);
      rrect(sc, 4, th.oledBg);
    }
    const auto hs = heldSteps();
    // top line
    if (D()) {
      font(fOled, 11);
      std::string left;
      if (isMaster()) left = "FX TRACK";
      else { const TrackM& t = S().tracks[tr]; left = "T" + std::to_string(tr + 1) + " " + MACH_NAMES[t.m]; const int pi = presetIndex(t.pre); if (!t.pre.empty()) left += " \xc2\xb7 " + upper(t.pre) + (pi >= 0 ? std::string(" \xc2\xb7 ") + MATS[PRESETS[pi].mat] : ""); }
      txt(sc.x + 10, sc.y + 17, fit(left, sc.w * 0.62f), th.oled);
      const std::string pg = perfMode ? "PERF" : isMaster() ? MASTER_PAGES[mpage] : PAGES[page];
      const float pw = txtW(pg) + 12; R pr(sc.x + sc.w * 0.665f, sc.y + 6, pw, 15); rrect(pr, 0, th.oled); txt(pr.cx(), pr.y + 11.5f, pg, th.oledBg, ALIGN_CENTER | ALIGN_BASELINE);
      char b[32]; std::snprintf(b, sizeof b, "P%d %.1f", S().cur + 1, (core->hostSynced.load() || core->hostPlaying.load()) ? core->hostBpm.load() : S().bpm);
      txt(sc.r() - 10, sc.y + 17, b, th.oled, ALIGN_RIGHT | ALIGN_BASELINE);
      beginPath(); rect(sc.x, sc.y + 26, sc.w, 1); fillColor(th.oledDim); fill();
    }
    const R mid(sc.x, sc.y + 27, sc.w, 134);
    // machine selector
    if (!isMaster() && page == 2 && !perfMode) {
      font(fOled, 11); const std::string mn = MACH_NAMES[S().tracks[tr].m]; const float mw = txtW(mn) + 14;
      R nx(mid.r() - 22, mid.y + 4, 18, 17), nm(nx.x - mw - 2, mid.y + 4, mw, 17), pv(nm.x - 20, mid.y + 4, 18, 17);
      if (clicked(ID_MPREV, pv)) cycleMachine(-1);
      if (clicked(ID_MNEXT, nx)) cycleMachine(1);
      if (D()) { txt(pv.cx(), pv.y + 12, "<", th.oled, ALIGN_CENTER | ALIGN_BASELINE); txt(nx.cx(), nx.y + 12, ">", th.oled, ALIGN_CENTER | ALIGN_BASELINE); rstroke(nm, 0, th.oledMid); txt(nm.cx(), nm.y + 12, mn, th.oled, ALIGN_CENTER | ALIGN_BASELINE); }
    }
    // lock tag
    if (!hs.empty() || (allMode() && !isMaster())) {
      std::string lt;
      if (!hs.empty()) { const Step& st = pat().steps[tr][hs[0]]; lt = st.on && st.tl ? "TRIGLESS \xc2\xb7 STEP" : "LOCK \xc2\xb7 STEP"; for (size_t k = 0; k < hs.size() && k < 4; k++) lt += " " + std::to_string(hs[k] + 1); if (hs.size() > 4) lt += " +" + std::to_string(hs.size() - 4); }
      else lt = "CONTROL ALL \xc2\xb7 EVERY TRACK";
      font(fOled, 10); const float w = txtW(lt) + 12; R lr(mid.x + 10, mid.b() - 18, w, 14);
      if (clicked(ID_LOCKTAG, lr) && !hs.empty()) { latched.clear(); schedulePreview(); }
      if (D()) { rrect(lr, 0, th.lock); txt(lr.x + 6, lr.y + 10.5f, lt, th.onLock); }
    }
    if (D()) {
      save(); scissor(mid.x, mid.y, mid.w, mid.h);
      if (isMaster()) drawMaster(mid); else if (page == 6 && !perfMode) drawLfo(mid);
      else if (page == 2 && !perfMode && S().tracks[tr].m == M_SMP) drawSample(mid); else drawWave(mid);
      // note line
      std::string note;
      if (perfMode) { note = "PERFORM T" + std::to_string(tr + 1) + ": RETRIG \xc2\xb7 VELOCITY \xc2\xb7 "; const PDef& a = T().track(S().tracks[tr].m, S().perfA); const PDef& b2 = T().track(S().tracks[tr].m, S().perfB); note += std::string(a.valid() ? a.n : "?") + " \xc2\xb7 " + (b2.valid() ? b2.n : "?"); }
      else if (isMaster()) {
        if (mpage == 0) note = "FX TRACK \xc2\xb7 HOLD A STEP TO LOCK MASTER FX"; else if (mpage == 1) note = "8-LINE FEEDBACK REVERB"; else if (mpage == 2) note = "GLUE \xc2\xb7 DRIVE \xc2\xb7 DJ FILTER \xc2\xb7 LIMITER";
        else note = std::string("SIDECHAIN FROM ") + DUCK_SRC[(int)clampd(jsround(S().master[24]), 0, 9)] + " \xc2\xb7 DUCK AMOUNT PER TRACK ON AMP PAGE";
      } else if (page == 0) note = hs.size() ? "STEP: COND, MICRO-TIMING, RETRIG" : "DEFAULTS FOR NEW TRIGS. HOLD A STEP TO LOCK ITS OWN";
      else if (page == 1 && S().tracks[tr].m == M_SMP) note = "SLCS: SLICES (OFF / 8-64 / AUTO) \xc2\xb7 SLCE: STEP = SLICE OF THE STEP \xc2\xb7 LAB > SAMPLE";
      else if (page == 1) note = hs.size() ? "SLIDE TO NEXT TRIG \xc2\xb7 LOCKS APPLY ONLY IF LCND / LPRB PASS" : "HOLD A STEP: SLIDE AND CONDITIONAL LOCKS";
      else if (page == 2 && S().tracks[tr].m == M_SMP) { const Step* st = heldStepPtr(); const SampleData* sd = sampleOf(tr, st); char b[160];
        if (sd) std::snprintf(b, sizeof b, "S%02d %s \xc2\xb7 %.2fS \xc2\xb7 %.1fKHZ \xc2\xb7 %s", slotOf(tr, st) + 1, upper(sd->name).c_str(), sd->seconds(), sd->rate / 1000, upper(sampleFmtName(sd->fmt)).c_str());
        else std::snprintf(b, sizeof b, "S%02d EMPTY \xc2\xb7 LAB > SOUND > USER: LOAD OR IMPORT A WAV", slotOf(tr, st) + 1);
        note = b; }
      else if (page == 2) note = tuneReadout();
      else if (page == 6) { const double* p = S().tracks[tr].p; const int dst = LFO_DEST_IDX[(int)clampd(jsround(p[34]), 0, 15)]; note = std::string("LFO > ") + (dst < 8 ? T().mach[S().tracks[tr].m][dst].n : LFO_DEST[(int)jsround(p[34])]) + (p[33] == 0 ? " (DEPTH 0)" : ""); }
      else if (page == 4) note = "DUCK = HOW MUCH THE SIDECHAIN PUMPS THIS TRACK";
      font(fOled, 10); txt(mid.x + 10, mid.y + 15, fit(note, mid.w * 0.66f), th.oledMid);
      restore();
      // toast
      if (toastUntil > 0) { font(fOled, 12); const float w = txtW(toastMsg) + 20; R t(mid.cx() - w / 2, mid.cy() - 11, w, 22); rrect(t, 0, th.oled); txt(t.cx(), t.y + 15.5f, toastMsg, th.oledBg, ALIGN_CENTER | ALIGN_BASELINE); }
      beginPath(); rect(sc.x, mid.b(), sc.w, 1); fillColor(th.oledDim); fill();
    }
    // param cells
    const float cw = sc.w / 4, ch = (sc.b() - mid.b() - 1) / 2;
    for (int k = 0; k < 8; k++) {
      R c(sc.x + (k % 4) * cw, mid.b() + 1 + (k / 4) * ch, cw, ch);
      paramCtl(ID_CELL + k, c, k);
      if (!D()) continue;
      if (k >= 4) { beginPath(); rect(c.x, c.y, c.w, 1); fillColor(th.oledDim); fill(); }
      if (k % 4) { beginPath(); rect(c.x, c.y, 1, c.h); fillColor(th.oledDim); fill(); }
      const int i = pageBase() + k; const PDef& d = defOf(i); if (!d.valid()) continue;
      const double v = getVal(i), norm = (v - d.min) / (d.max - d.min); double lv; const bool lk = !hs.empty() && lockOf(i, lv);
      const bool dim = (d.step && hs.empty()) || (!hs.empty() && !d.lock);
      if (dim) globalAlpha(.45f);
      const float rx = c.x + 17, ry = c.cy();
      const float a0 = (float)(PI * 0.75), span = (float)(PI * 1.5);
      beginPath(); arc(rx, ry, 7, a0, a0 + span, CW); strokeColor(th.oledDim); strokeWidth(2.5f); stroke();
      beginPath();
      if (d.min < 0) { const double b = v / (v < 0 ? -d.min : d.max); const float mid0 = (float)(PI * 1.5); if (b >= 0) arc(rx, ry, 7, mid0, mid0 + (float)b * span / 2, CW); else arc(rx, ry, 7, mid0 + (float)b * span / 2, mid0, CW); }
      else arc(rx, ry, 7, a0, a0 + (float)(norm * span) + 0.001f, CW);
      strokeColor(th.oled); strokeWidth(2.5f); stroke();
      std::string val = formatValue(d, v);
      if (!isMaster() && i == 34) { const int dst = LFO_DEST_IDX[(int)jsround(v)]; if (dst < 8) val = T().mach[S().tracks[tr].m][dst].n; }
      if (!isMaster() && i == 2 && S().tracks[tr].m == M_SMP) { const int sl = (int)clampd(jsround(v), 0, NSLOT - 1); val += S().smp[sl] ? " " + upper(S().smp[sl]->name) : " \xe2\x80\x94"; }
      font(fOled, 9); txt(c.x + 31, c.y + 15, d.n, th.oledMid);
      font(fOled, 12); const std::string vf = fit(val, c.w - 36);
      if (lk) { const float w = txtW(vf) + 6; rrect(R(c.x + 28, c.y + 18, w, 15), 0, th.lock); txt(c.x + 31, c.y + 30, vf, th.onLock); }
      else txt(c.x + 31, c.y + 30, vf, th.oled);
      if (dim) globalAlpha(1);
    }
  }
  void drawWave(R m) {
    const float top = m.y + 22, bot = m.b() - 14, cy = (top + bot) / 2;
    beginPath(); rect(m.x, std::round(cy), m.w, 1); fillColor(th.oledDim); fill();
    if (wave.empty()) return;
    const int N = (int)wave.size(); const float span = (float)clampd(waveLast * 1.12, PSR * 0.06, N);
    const float amp = (bot - top) / 2 / std::max(1.f, wavePeak);
    fillColor(th.oled);
    const int W = (int)m.w;
    beginPath();
    for (int x = 0; x < W; x++) {
      const int a = (int)std::floor((double)x / W * span), b = std::max(a + 1, (int)std::floor((double)(x + 1) / W * span)); float mn = 1, mxv = -1;
      for (int i = a; i < b && i < N; i++) { const float v = wave[i]; if (v < mn) mn = v; if (v > mxv) mxv = v; }
      if (mxv < mn) continue;
      const float y1 = cy - mxv * amp, y2 = cy - mn * amp; rect(m.x + x, y1, 0.9f, std::max(1.f, y2 - y1));
    }
    fill();
    font(fOled, 9); const double ms = span / PSR * 1000;
    txt(m.x + 6, m.b() - 3, "0", th.oledMid);
    const std::string lab = ms >= 1000 ? toFixed(ms / 1000, 2) + "S" : std::to_string((int)jsround(ms)) + "MS"; txt(m.r() - 6, m.b() - 3, lab, th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
    char b2[64]; std::snprintf(b2, sizeof b2, "PEAK %.1fDB  TAIL %dMS", 20 * std::log10(wavePeak + 1e-9), (int)jsround(waveLast / PSR * 1000)); txt(m.cx(), m.b() - 3, b2, th.oledMid, ALIGN_CENTER | ALIGN_BASELINE);
  }
  // SAMPLE machine, SRC page: the whole sample with the played range, the loop point and the direction
  void drawSample(R m) {
    const Step* st = heldStepPtr(); const SampleData* sd = sampleOf(tr, st);
    const float top = m.y + 24, bot = m.b() - 14, cy = (top + bot) / 2, x0 = m.x + 8, w = m.w - 16;
    beginPath(); rect(x0, std::round(cy), w, 1); fillColor(th.oledDim); fill();
    font(fOled, 9);
    if (!sd || sd->len < 2) { txt(m.cx(), cy - 8, "NO SAMPLE IN THIS SLOT", th.oledMid, ALIGN_CENTER | ALIGN_BASELINE); return; }
    auto gv = [&](int i) { return getVal(i); };
    // the slice this step plays (the whole sample without slices): STRT / END / LOOP are inside it
    const int slc = (int)clampd(jsround(gv(51)), 0, 5); double lo = 0, hi = sd->len;
    { const auto hs = heldSteps(); if (slc > 0) MSmp::sliceRange(*sd, slc, (int)clampd(jsround(gv(52)), 0, 64), hs.empty() ? 0 : hs[0], lo, hi); }
    const double u0 = lo / sd->len, uw = (hi - lo) / sd->len;
    const double a = u0 + clampd(gv(3), 0, 127) / 127 * uw, e = std::max(a, u0 + clampd(gv(4), 0, 127) / 127 * uw), lf = clampd(gv(6), 0, 127) / 127; const int mode = (int)clampd(jsround(gv(5)), 0, 3);
    const float amp = (bot - top) / 2 / std::max(0.05f, sd->peak);
    beginPath();
    for (int x = 0; x < (int)w; x++) {
      const int k0 = (int)((double)x / w * OVERVIEW_N), k1 = std::max(k0 + 1, (int)((double)(x + 1) / w * OVERVIEW_N)); float mn = 0, mxv = 0;
      for (int k = k0; k < k1 && k < OVERVIEW_N; k++) { mn = std::min(mn, sd->ov[k * 2]); mxv = std::max(mxv, sd->ov[k * 2 + 1]); }
      const float y1 = cy - mxv * amp, y2 = cy - mn * amp; rect(x0 + x, y1, 0.9f, std::max(1.f, y2 - y1));
    }
    fillColor(th.oled); fill();
    // outside the played range: dimmed
    const float xa = x0 + (float)a * w, xe = x0 + (float)e * w;
    beginPath(); rect(x0, top - 2, xa - x0, bot - top + 4); rect(xe, top - 2, x0 + w - xe, bot - top + 4); fillColor(A(th.oledBg, .72f)); fill();
    beginPath(); rect(xa - .5f, top - 4, 1.5f, bot - top + 8); rect(xe - 1, top - 4, 1.5f, bot - top + 8); fillColor(th.oled); fill();
    const bool rev = mode == 1 || mode == 3;
    if (mode >= 2) { const float xl = rev ? xe - (float)(lf * (e - a)) * w : xa + (float)(lf * (e - a)) * w; beginPath(); for (float yy = top; yy < bot; yy += 5) rect(xl - .5f, yy, 1, 3); fillColor(th.oledMid); fill(); txt(xl + 3, top + 6, "L", th.oledMid); }
    { const float ax = rev ? xe - 4 : xa + 4, dir = rev ? -1.f : 1.f; beginPath(); moveTo(ax, top - 4); lineTo(ax + dir * 6, top - 1); lineTo(ax, top + 2); closePath(); fillColor(th.oled); fill(); }
    char b[96]; if (slc > 0) std::snprintf(b, sizeof b, "%s \xc2\xb7 SLICE \xc2\xb7 %.3fS PLAYED", SMP_MODE_NAMES[mode], (e - a) * sd->seconds()); else std::snprintf(b, sizeof b, "%s \xc2\xb7 %.3fS PLAYED", SMP_MODE_NAMES[mode], (e - a) * sd->seconds());
    txt(m.x + 6, m.b() - 3, b, th.oledMid);
    txt(m.r() - 6, m.b() - 3, toFixed(sd->seconds(), 2) + "S", th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
  }
  static double lfoShape(const double* p, double ph) {
    switch ((int)jsround(p[35])) { case 0: return std::sin(TAU * ph); case 1: return ph < 0.25 ? ph * 4 : ph < 0.75 ? 2 - ph * 4 : ph * 4 - 4; case 2: return ph < 0.5 ? 1 : -1; case 3: return 1 - 2 * ph; case 4: return 2 * ph - 1; case 5: return std::sin(std::floor(ph * 8) * 12.9898) * 0.9; default: return std::exp(-ph * 5) * 2 - 1; }
  }
  void drawLfo(R m) {
    const double* p = S().tracks[tr].p; const float top = m.y + 24, bot = m.b() - 14, cy = (top + bot) / 2;
    const float a = (bot - top) / 2 * (float)std::max(0.08, std::fabs(p[33]) / 64) * (p[33] < 0 ? -1 : 1);
    const double steps = LFO_SPD_ST[(int)clampd(jsround(p[32]), 0, 13)], view = std::max(16.0, steps * 2);
    fillColor(th.oledDim); beginPath(); rect(m.x, cy, m.w, 1); fill();
    for (double s = 0; s <= view; s += 4) { beginPath(); rect(m.x + (float)(s / view) * m.w, bot, 1, 4); fill(); }
    const int mode = (int)jsround(p[36]); const double off = p[37] / 128;
    beginPath();
    for (int x = 0; x <= (int)m.w; x++) {
      double phv = x / m.w * view / steps + off; if (mode == 2) phv = std::min(phv, 0.9999); else if (mode == 3) phv = std::min(phv, 0.4999); else phv = std::fmod(phv, 1.0);
      const float y = cy - (float)lfoShape(p, phv) * a; if (x) lineTo(m.x + x, y); else moveTo(m.x + x, y);
    }
    strokeColor(th.oled); strokeWidth(1.5f); stroke();
    font(fOled, 9); txt(m.r() - 6, m.b() - 3, std::to_string((int)view) + " STEPS \xc2\xb7 " + LFO_MODE[mode], th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
  }
  void drawMaster(R m) {
    const double* M = S().master; const float top = m.y + 24, bot = m.b() - 14; font(fOled, 9);
    auto mv = [&](int i) { return getVal(i); };
    if (mpage == 0) {
      const double t = DLY_ST[(int)clampd(jsround(M[0]), 0, 9)], view = 32, fbk = mv(1) / 127 * 0.95;
      fillColor(th.oledDim); for (double s = 0; s <= view; s += 4) { beginPath(); rect(m.x + (float)(s / view) * m.w, bot, 1, 4); fill(); }
      double g = 1; int k = 0; fillColor(th.oled);
      for (double tt = 0; tt <= view && g > 0.02; tt += t, k++) { const float x = m.x + (float)(tt / view) * (m.w - 4); const float hh = (bot - top) * (float)g; beginPath(); rect(x, bot - hh, 3, hh); fill(); g = k == 0 ? 0.8 : g * fbk; }
      txt(m.r() - 6, m.b() - 3, "32 STEPS", th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
    } else if (mpage == 1) {
      const double t60 = expm(mv(9), 0.3, 20), view = std::max(2.0, std::min(20.0, t60 * 1.2)), pre = mv(11) / 127 * 0.2;
      beginPath();
      for (int x = 0; x <= (int)m.w; x++) { const double tt = x / m.w * view; const double g = tt < pre ? 0 : std::pow(10.0, -3 * (tt - pre) / t60); const float y = bot - (bot - top) * (float)g; if (x) lineTo(m.x + x, y); else moveTo(m.x + x, y); }
      strokeColor(th.oled); strokeWidth(1.5f); stroke();
      txt(m.r() - 6, m.b() - 3, "RT60 " + toFixed(t60, 1) + "S", th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
    } else if (mpage == 3) {
      const int li = (int)clampd(jsround(M[25]), 0, 8); const double depth = mv(27) / 127, k = std::pow(2.0, -1 + mv(26) / 127 * 3), lenSt = li == 0 ? 2 : DUCK_LEN_ST[li], view = std::max(8.0, lenSt * 2);
      fillColor(th.oledDim); for (double s = 0; s <= view; s += 1) { beginPath(); rect(m.x + (float)(s / view) * m.w, bot, 1, ((int)s % 4) ? 2 : 5); fill(); }
      beginPath(); rect(m.x, top, m.w, 1); fill();
      beginPath();
      for (int x = 0; x <= (int)m.w; x++) { const double st = x / m.w * view, u = std::fmod(st, lenSt) / lenSt; const double g = st < lenSt ? 1 - depth * std::pow(1 - u, k) : 1; const float y = bot - (bot - top) * (float)g; if (x) lineTo(m.x + x, y); else moveTo(m.x + x, y); }
      strokeColor(th.oled); strokeWidth(1.5f); stroke();
      fillColor(th.oled); beginPath(); rect(m.r() - 14, top, 6, (bot - top) * (1 - duckG)); fill();
      txt(m.x + 6, m.b() - 3, li == 0 ? "AUTO: FOLLOWS SOURCE LEVEL" : "GAIN OVER " + std::to_string((int)view) + " STEPS", th.oledMid);
    } else {
      const int n = (int)meterHist.size(); const float bw = m.w / 160; fillColor(th.oled);
      for (int i = 0; i < n; i++) { const float db = 20 * std::log10(meterHist[i] + 1e-9f); const float v = (float)clampd((db + 48) / 48, 0, 1); const float hh = (bot - top) * v; beginPath(); rect(m.r() - (n - i) * bw, bot - hh, std::max(1.f, bw - 1), hh); fill(); }
      const float y0 = bot - (bot - top) * (42.f / 48); fillColor(th.oledDim); beginPath(); rect(m.x, y0, m.w, 1); fill();
      txt(m.x + 6, m.b() - 3, playing ? "OUTPUT PEAK" : "OUTPUT PEAK \xc2\xb7 PRESS PLAY", th.oledMid);
      txt(m.r() - 6, y0 - 3, "-6DB", th.oledMid, ALIGN_RIGHT | ALIGN_BASELINE);
    }
  }
  void pullRow() {
    const float y = PY + PH;
    // LAB pull tab
    R pull(WW / 2 - 56, y - 2, 112, 36);
    if (clicked(ID_PULL, pull)) { drawer = !drawer; saveUiState(); applySize(); }
    if (D()) {
      beginPath(); moveTo(pull.x, pull.y); lineTo(pull.r(), pull.y); lineTo(pull.r(), pull.b() - 12); arcTo(pull.r(), pull.b(), pull.r() - 12, pull.b(), 12); lineTo(pull.x + 12, pull.b()); arcTo(pull.x, pull.b(), pull.x, pull.b() - 12, 12); closePath();
      fillPaint(linearGradient(0, pull.y, 0, pull.b(), th.body, th.bodyHi)); fill(); strokeColor(th.edge); strokeWidth(1); stroke();
      beginPath(); rect(pull.x + 1, pull.y, pull.w - 2, 3); fillColor(th.body); fill();
      ledDot(pull.x + 20, pull.cy() + 1, drawer, th.led);
      font(fPrint, 11, 3.5f); txt(pull.x + 34, pull.cy() + 5, "LAB", drawer ? th.printHi : th.print);
      save(); translate(pull.r() - 22, pull.cy() + (drawer ? 2 : -1)); rotate(drawer ? (float)(-PI * 0.75) : (float)(PI * 0.25));
      beginPath(); moveTo(3.5f, -3.5f); lineTo(3.5f, 3.5f); lineTo(-3.5f, 3.5f); strokeColor(drawer ? th.printHi : th.print); strokeWidth(1.5f); stroke(); restore();
    }
    // theme switch tab
    R mode(PX + PW - 22 - 170, y - 2, 170, 36);
    if (clicked(ID_THEME, mode)) { light = !light; th = light ? lightTheme() : darkTheme(); saveUiState(); }
    if (D()) {
      beginPath(); moveTo(mode.x, mode.y); lineTo(mode.r(), mode.y); lineTo(mode.r(), mode.b() - 12); arcTo(mode.r(), mode.b(), mode.r() - 12, mode.b(), 12); lineTo(mode.x + 12, mode.b()); arcTo(mode.x, mode.b(), mode.x, mode.b() - 12, 12); closePath();
      fillPaint(linearGradient(0, mode.y, 0, mode.b(), th.body, th.bodyHi)); fill(); strokeColor(th.edge); strokeWidth(1); stroke();
      beginPath(); rect(mode.x + 1, mode.y, mode.w - 2, 3); fillColor(th.body); fill();
      font(fPrint, 10, 2); txt(mode.x + 18, mode.cy() + 4, "DARK", light ? th.print : th.printHi);
      R sw(mode.x + 70, mode.cy() - 7, 30, 14); rrect(sw, 7, th.well);
      beginPath(); circle(light ? sw.r() - 7 : sw.x + 7, sw.cy(), 5); fillColor(th.printHi); fill();
      txt(mode.x + 112, mode.cy() + 4, "LIGHT", light ? th.printHi : th.print);
    }
  }
  // ================= LAB =================
  void lab() {
    const float y0 = HCLOSED - 6; R box(PX, y0, PW, LABH - 14);
    if (D()) { rrect(box, 12, th.modBg); rstroke(box, 12, th.modEdge); }
    const int* order = standalone ? TAB_ORDER_SA : TAB_ORDER_PL; const int ntabs = standalone ? 9 : 7;
    for (int k = 0; k < ntabs; k++) {
      const int id = order[k]; R r(box.x + 14, box.y + 12 + k * 37, 118, 30);
      if (clicked(ID_TAB + id, r)) { labTab = id; saveUiState(); }
      if (D()) {
        if (id == labTab) { rrect(r, 7, th.cap); rstroke(r, 7, th.led); }
        font(fMark, 10, 2.4f); txt(r.x + 12, r.cy() + 4, TAB_NAMES[id], id == labTab ? th.printHi : th.print);
      }
    }
    if (D()) { beginPath(); rect(box.x + 146, box.y + 14, 1, box.h - 28); fillColor(th.modEdge); fill(); }
    const R c(box.x + 164, box.y + 14, box.w - 182, box.h - 28);
    switch (labTab) {
      case TAB_GROOVE: labGroove(c); break; case TAB_SOUND: labSound(c); break; case TAB_KITS: labKits(c); break; case TAB_PERFORM: labPerform(c); break;
      case TAB_SONG: labSong(c); break; case TAB_SAMPLE: labSample(c); break;
      case TAB_CONTROL: if (standalone) labControl(c); else labProject(c); break;
      case TAB_AUDIO: if (standalone) labAudio(c); else labProject(c); break;
      default: labProject(c);
    }
    if (D() && drag.kind == 5 && dragMoved) {   // the dragged item follows the mouse
      font(fPrintB, 12, 0.2f); const std::string l = fit(dragLabel, 220); const R g(dragX + 12, dragY - 12, txtW(l) + 22, 24);
      rrect(R(g.x, g.y + 3, g.w, g.h), 7, th.shadow); rrect(g, 7, th.cap); rstroke(g, 7, th.led); txt(g.x + 11, g.cy() + 4.5f, l, th.printHi);
    }
  }
  void h2(float x, float y, const std::string& t, const std::string& small = "") {
    if (!D()) return; font(fMark, 12, 12 * 0.26f); const float w = txt(x, y, t, th.printHi);
    if (!small.empty()) { font(fPrint, 11, 1.3f); txt(x + w + 12, y, upper(small), th.print); }
  }
  void para(float x, float y, float w, const std::string& s, Color c, float size = 12.5f) { if (!D()) return; font(fPrint, size, 0.2f); fillColor(c); textAlign(ALIGN_LEFT | ALIGN_TOP); textLineHeight(1.25f); textBox(x, y, w, s.c_str(), nullptr); }
  void labGroove(R c) {
    h2(c.x, c.y + 12, "GROOVE LIBRARY");
    { float x = c.x + 200; for (int k = 0; k < 2; k++) { const char* l = k ? "USER" : "FACTORY"; const float w = chipW(l, true); if (chip(ID_GSRC + k, R(x, c.y - 2, w, 20), l, grvSrc == k, true)) { grvSrc = k; if (k) scanLib(true); } x += w + 5; } }
    if (grvSrc == 1) labGrooveUser(c, 600);
    float y = c.y + 30;
    if (grvSrc == 0) {
    for (int f = 0; f < NFAMILIES; f++) {
      const FamilyDef& F = FAMILIES[f];
      if (D()) { font(fPrint, 11, 1.5f); txt(c.x, y + 17, F.name, th.print); }
      float x = c.x + 78;
      for (int k = 0; k < F.n; k++) { const int si = F.styles[k]; const float w = chipW(STYLES[si].name); if (chip(ID_STYLE + si, R(x, y + 2, w, 24), STYLES[si].name, si == style)) style = si; x += w + 6; }
      y += 30;
    }
    { int fi = 0; for (int f = 0; f < NFAMILIES; f++) for (int k = 0; k < FAMILIES[f].n; k++) if (FAMILIES[f].styles[k] == style) fi = f;
      char b[220]; std::snprintf(b, sizeof b, "%s: %s. %s starts at %g BPM, swing %g%%.", FAMILIES[fi].name, FAMILIES[fi].desc, STYLES[style].name, STYLES[style].bpm, STYLES[style].swing);
      para(c.x, y + 6, 560, b, th.print, 12); }
    }
    const float rx = c.x + 610, rw = c.w - 610;
    slider(ID_DEN, R(rx, c.y + 4, rw, 22), "DENSITY", denPct, 0, 100);
    slider(ID_VAR, R(rx, c.y + 32, rw, 22), "VARIATION", varPct, 0, 100);
    gopt.den = denPct / 100.0; gopt.vari = varPct / 100.0;
    check(ID_CLOCKS, R(rx, c.y + 62, 180, 20), "Sound & pitch locks", gopt.locks);
    check(ID_CFILLS, R(rx + 180, c.y + 62, 120, 20), "Fill trigs", gopt.fills);
    check(ID_CHALF, R(rx, c.y + 86, 180, 20), "Half-time feel", gopt.half);
    check(ID_CTEMPO, R(rx + 180, c.y + 86, 150, 20), "Style tempo & swing", styleTempo);
    if (btn(ID_GEN, R(rx, c.y + 116, 170, 32), "GENERATE PATTERN", true)) genAll();
    if (btn(ID_GENTRK, R(rx + 178, c.y + 116, 152, 32), "GENERATE TRACK")) genTrk();
    if (btn(ID_MUT, R(rx, c.y + 156, 100, 32), "MUTATE")) mutate();
    if (D()) { beginPath(); rect(rx, c.y + 200, rw, 1); fillColor(th.modEdge); fill(); font(fPrint, 11, 1.5f); txt(rx, c.y + 220, "METHODS \xc2\xb7 WORK IN ANY STYLE", th.print); }
    const int n = pat().len[tr]; if (eK > n) eK = n; if (eR > n - 1) eR = std::max(0, n - 1);
    slider(ID_EK, R(rx, c.y + 230, rw, 22), "PULSES", eK, 1, n);
    slider(ID_ER, R(rx, c.y + 256, rw, 22), "ROTATE", eR, 0, std::max(0, n - 1));
    if (btn(ID_EUC, R(rx, c.y + 286, 150, 32), "EUCLID ON TRACK")) applyEuclid();
    if (btn(ID_POLY, R(rx + 158, c.y + 286, 172, 32), "POLYMETER PATTERN")) genPoly();
  }
  void labSound(R c) {
    h2(c.x, c.y + 12, "SOUND", isMaster() ? "select a track" : "track " + std::to_string(tr + 1));
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, c.y + 38, "ROLE IN GROOVES", th.print); txt(c.x, c.y + 70, "ENGINE", th.print); }
    float x = c.x + 112;
    for (int r = 0; r < NROLES; r++) { const float w = chipW(ROLE_NAMES[r], true); if (chip(ID_ROLE + r, R(x, c.y + 24, w, 22), ROLE_NAMES[r], !isMaster() && S().roles[tr] == r, true, isMaster())) { S().roles[tr] = r; toast(std::string("ROLE ") + upper(ROLE_NAMES[r])); } x += w + 5; }
    x = c.x + 112;
    for (int m = 0; m < NMACH; m++) { const float w = chipW(MACH_NAMES[m], true); if (chip(ID_MACH + m, R(x, c.y + 56, w, 22), MACH_NAMES[m], !isMaster() && S().tracks[tr].m == m, true, isMaster())) setMachine(m); x += w + 5; }
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, c.y + 104, "MATERIAL", th.print); }
    x = c.x + 112;
    for (int m = 0; m < NMATS; m++) { const float w = chipW(MATS[m]); if (chip(ID_MAT + m, R(x, c.y + 88, w, 24), MATS[m], m == mat && sndSrc == 0)) { mat = m; presetScroll = 0; sndSrc = 0; } x += w + 6; }
    { x += 8; if (D()) { beginPath(); rect(x - 7, c.y + 92, 1, 16); fillColor(th.modEdge); fill(); }
      const float w = chipW("USER LIBRARY"); if (chip(ID_SSRC, R(x, c.y + 88, w, 24), "USER LIBRARY", sndSrc == 1)) { sndSrc = 1; scanLib(true); } }
    if (sndSrc == 1) { labSoundUser(c); return; }
    std::vector<int> inMat; for (int i = 0; i < NPRESETS; i++) if (PRESETS[i].mat == mat) inMat.push_back(i);
    bool has[NFUNCS] = { false }; for (int i : inMat) has[PRESETS[i].f] = true;
    if (fn >= 0 && !has[fn]) fn = -1;
    x = c.x + 112;
    { const float w = chipW("ALL", true); if (chip(ID_FN, R(x, c.y + 120, w, 22), "ALL", fn < 0, true)) { fn = -1; presetScroll = 0; } x += w + 5; }
    for (int f = 0; f < NFUNCS; f++) { const float w = chipW(FUNCS[f], true); if (chip(ID_FN + 1 + f, R(x, c.y + 120, w, 22), FUNCS[f], fn == f, true, !has[f])) { fn = f; presetScroll = 0; } x += w + 5; }
    { char b[160]; std::snprintf(b, sizeof b, "%s \xc2\xb7 %d sounds", MAT_DESC[mat], (int)inMat.size()); if (D()) { font(fPrint, 12, 0.2f); txt(x + 10, c.y + 136, b, th.print); } }
    std::vector<int> list; for (int i : inMat) if (fn < 0 || PRESETS[i].f == fn) list.push_back(i);
    const R lr(c.x, c.y + 152, c.w - 160, c.h - 152); const int cols = 4, rows = 4; const float cw = (lr.w - (cols - 1) * 6) / cols, chh = 30;
    const int maxScroll = std::max(0, ((int)list.size() + cols - 1) / cols - rows);
    if (scrolled(lr)) presetScroll = (int)clampd(presetScroll + (wheel > 0 ? -1 : 1), 0, maxScroll);
    if (presetScroll > maxScroll) presetScroll = maxScroll;
    const std::string cur = isMaster() ? "" : S().tracks[tr].pre;
    for (int k = presetScroll * cols; k < (int)list.size() && k < (presetScroll + rows) * cols; k++) {
      const int pi = list[k], vk = k - presetScroll * cols; R r(lr.x + (vk % cols) * (cw + 6), lr.y + (vk / cols) * (chh + 6), cw, chh);
      if (clicked(ID_PRESET + pi, r)) loadPreset(pi);
      if (D()) {
        const bool sel = cur == PRESETS[pi].n; rstroke(r, 7, sel ? th.led : th.preEdge);
        font(fPrintB, 12.5f, 0.2f); txt(r.x + 9, r.cy() + 4.5f, fit(PRESETS[pi].n, cw - 60), th.printHi);
        font(fNum, 9.5f); txt(r.r() - 9, r.cy() + 4, FUNCS[PRESETS[pi].f], th.print, ALIGN_RIGHT | ALIGN_BASELINE);
      }
    }
    if (D() && maxScroll > 0) { const float th_ = lr.h * rows / (rows + maxScroll); rrect(R(lr.r() + 4, lr.y, 3, lr.h - 6), 1.5f, th.range); rrect(R(lr.r() + 4, lr.y + (lr.h - 6 - th_) * presetScroll / maxScroll, 3, th_), 1.5f, th.print); }
    if (btn(ID_RND, R(c.r() - 148, c.y + 152, 148, 32), "RANDOMIZE")) randomizeSound();
    if (btn(ID_INIT, R(c.r() - 148, c.y + 190, 148, 32), "INIT TRACK")) initTrack();
    para(c.r() - 148, c.y + 232, 148, "Scroll the list with the mouse wheel. Loading a preset keeps choke and duck routing.", th.print, 11.5f);
  }
  // ================= user library views =================
  std::map<std::string, std::pair<int64_t, std::string>> tagCache;   // path -> (mtime, tag shown on the item)
  std::string itemTag(const LibItem& it) {
    if (it.kind == LK_WAV) return "WAV";
    if (it.kind == LK_KIT) return pathExists(kitSamplesDir(it.path)) ? "KIT+S" : "KIT";
    auto c = tagCache.find(it.path); if (c != tagCache.end() && c->second.first == it.mtime) return c->second.second;
    std::string tag = "?", txt; JVal j;
    if (it.size < (64u << 20) && readTextFile(it.path, txt) && parseJson(txt, j) && j.isObj()) {
      if (it.kind == LK_GROOVE) { const JVal& p = j["pattern"]; if (p["bpm"].isNum()) tag = toFixed(p["bpm"].n, 0); }
      else { const int m = machineIndex(j["machine"].str().c_str()); if (m >= 0) tag = MACH_IDS[m]; }
    }
    tagCache[it.path] = { it.mtime, tag }; return tag;
  }
  bool teActive(int lib) const { return lib == 1 ? (te.act >= TE_NEWCAT_S && te.act <= TE_SAVE_S) : lib == 2 ? (te.act >= TE_NEWCAT_G && te.act <= TE_SAVE_G) : (te.act >= TE_NEWCAT_K && te.act <= TE_SAVE_K); }
  void teBar(R r) {
    if (D()) { font(fPrint, 11, 1.5f); }
    font(fPrint, 11, 1.5f); const float tw = txtW(upper(te.title)) + 14;
    const R okR(r.r() - 150, r.y, 68, r.h), caR(r.r() - 76, r.y, 76, r.h), fld(r.x + tw, r.y, r.w - tw - 158, r.h);
    if (btn(ID_TEOK, okR, "OK", true)) { commitEdit(); return; }
    if (btn(ID_TECANCEL, caR, "CANCEL")) { te = TextEdit(); needRepaint = true; return; }
    if (!D()) return;
    rrect(R(r.x - 6, r.y - 4, r.w + 12, r.h + 8), 8, th.modBg);
    font(fPrint, 11, 1.5f); txt(r.x, r.cy() + 4, upper(te.title), th.printHi);
    rrect(fld, 6, th.oledBg); rstroke(fld, 6, th.led);
    font(fPrint, 13, 0.2f); std::string s = te.text; while (!s.empty() && txtW(s) > fld.w - 22) { size_t n = 1; while (n < s.size() && ((unsigned char)s[n] & 0xC0) == 0x80) n++; s.erase(0, n); }
    const float w = txt(fld.x + 9, fld.cy() + 4.5f, s, th.oled) - (fld.x + 9);
    if (std::fmod(nowMs(), 1000) < 600) { beginPath(); rect(fld.x + 10 + w, fld.y + 5, 1.5f, fld.h - 10); fillColor(th.oled); fill(); }
    needRepaint = true;   // caret blink
  }
  // category chips: the root (INBOX / UNSORTED) first, + NEW last; each one is a drop target for a dragged item
  void catRow(int lib, R r) {
    LibView v = lv(lib); const LibIndex& L = v.L; std::string& cat = v.cat; std::string& sel = v.sel;
    int& sc = v.catScroll; int& scroll = v.scroll;
    const int idBase = v.idCat; const char* rootName = v.root;
    std::vector<std::string> cats{ "" }; for (const std::string& c : L.cats) cats.push_back(c);
    std::map<std::string, int> count; for (const LibItem& it : L.items) count[it.cat]++;
    auto lab = [&](const std::string& c) { const std::string n = c.empty() ? rootName : c; const int k = count[c]; return k ? n + " " + std::to_string(k) : n; };
    const float newW = chipW("+ NEW", true), arrW = 22;
    if (scrolled(r)) sc += wheel > 0 ? -1 : 1;
    sc = (int)clampd(sc, 0, std::max(0, (int)cats.size() - 1));
    // how many fit from sc; arrows when not all fit
    auto fitFrom = [&](int s0, float avail) { int n = 0; float x = 0; for (int k = s0; k < (int)cats.size(); k++) { const float w = chipW(lab(cats[k]), true) + 5; if (x + w > avail) break; x += w; n++; } return n; };
    float avail = r.w - newW - 6; int nfit = fitFrom(sc, avail);
    const bool over = sc > 0 || sc + nfit < (int)cats.size();
    if (over) { avail -= 2 * arrW + 8; nfit = std::max(1, fitFrom(sc, avail)); }
    float x = r.x;
    for (int k = sc; k < sc + nfit && k < (int)cats.size(); k++) {
      const std::string& c = cats[k]; const std::string l = lab(c); const float w = chipW(l, true); const R cr(x, r.y, w, r.h);
      const bool hover = drag.kind == 5 && dragMoved && dragLib == lib && cr.in(dragX, dragY);
      if (chip(idBase + std::min(k, 190), cr, l, c == cat || hover, true)) {
        if (dbl && !c.empty() && c == cat) startEdit(v.teRenCat, "RENAME CATEGORY", c, c);
        else { cat = c; sel.clear(); scroll = 0; }
      }
      catDrop.push_back({ c, cr }); x += w + 5;
    }
    if (over) {
      const R pv(r.r() - newW - 2 * arrW - 10, r.y, arrW, r.h), nx(r.r() - newW - arrW - 6, r.y, arrW, r.h);
      if (chip(ID_CATPREV + v.idArrow, pv, "<", false, true, sc == 0)) sc--;
      if (chip(ID_CATNEXT + v.idArrow, nx, ">", false, true, sc + nfit >= (int)cats.size())) sc++;
    }
    if (chip(idBase + 199, R(r.r() - newW, r.y, newW, r.h), "+ NEW", false, true)) startEdit(v.teNewCat, "NEW CATEGORY", "");
  }
  // items of the current category; click = load, drag onto a category = move there
  void itemGrid(int lib, R lr, int cols, int rows, float chh) {
    LibView v = lv(lib); const LibIndex& L = v.L; const std::string& cat = v.cat; std::string& sel = v.sel;
    int& scroll = v.scroll; const int idBase = v.idItem;
    std::vector<const LibItem*> list; for (const LibItem& it : L.items) if (it.cat == cat) list.push_back(&it);
    const float cw = (lr.w - (cols - 1) * 6) / cols;
    const int maxScroll = std::max(0, ((int)list.size() + cols - 1) / cols - rows);
    if (scrolled(lr)) scroll = (int)clampd(scroll + (wheel > 0 ? -1 : 1), 0, maxScroll);
    if (scroll > maxScroll) scroll = maxScroll;
    if (list.empty() && D()) {
      const std::string where = std::string(lib == 1 ? "Library/Sounds" : lib == 2 ? "Library/Grooves" : "Library/Kits") + (cat.empty() ? std::string() : "/" + cat);
      para(lr.x + 4, lr.y + 6, lr.w - 8, lib == 1
        ? "Nothing here yet. SAVE SOUND stores the selected track's sound in this category, IMPORT WAV copies a WAV file here and loads it. Other programs can write WAV files or HARDER sound files into Documents/HARDER/" + where + "; they show up here within two seconds."
        : lib == 2 ? "Nothing here yet. SAVE PATTERN stores the current pattern in this category. Other programs can write HARDER groove files into Documents/HARDER/" + where + "."
        : "Nothing here yet. SAVE KIT stores the eight sounds (with their samples) in this category: Documents/HARDER/" + where + ".", th.print, 12);
    }
    for (int k = scroll * cols; k < (int)list.size() && k < (scroll + rows) * cols; k++) {
      const LibItem& it = *list[k]; const int vk = k - scroll * cols; const R r(lr.x + (vk % cols) * (cw + 6), lr.y + (vk / cols) * (chh + 6), cw, chh);
      const int id = idBase + vk;
      if (press(id, r)) { drag = Drag(); drag.id = id; drag.kind = 5; dragPath = it.path; dragLib = lib; dragMoved = false; dragX = dragX0 = mx; dragY = dragY0 = my; dragLabel = it.name; }
      if (ph == PH_RELEASE && pressedId == id && r.in(mx, my) && !dragMoved) {
        sel = it.path; const LibItem copy = it;
        if (lib == 1) loadLibSound(copy); else if (lib == 2) loadLibGroove(copy); else loadLibKit(copy);
      }
      if (D()) {
        const bool isSel = sel == it.path; rstroke(r, 7, isSel ? th.led : th.preEdge);
        if (drag.kind == 5 && dragMoved && dragPath == it.path) { globalAlpha(.4f); }
        font(fPrintB, 12.5f, 0.2f); txt(r.x + 9, r.cy() + 4.5f, fit(it.name, cw - 52), isSel ? th.led : th.printHi);
        font(fNum, 9.5f); txt(r.r() - 9, r.cy() + 4, itemTag(it), th.print, ALIGN_RIGHT | ALIGN_BASELINE);
        if (drag.kind == 5 && dragMoved && dragPath == it.path) globalAlpha(1);
      }
    }
    if (D() && maxScroll > 0) { const float th_ = lr.h * rows / (rows + maxScroll); rrect(R(lr.r() + 4, lr.y, 3, lr.h - 6), 1.5f, th.range); rrect(R(lr.r() + 4, lr.y + (lr.h - 6 - th_) * scroll / maxScroll, 3, th_), 1.5f, th.print); }
    // drop onto a category chip (collected by catRow in this same pass)
    if (ph == PH_RELEASE && drag.kind == 5 && dragMoved && dragLib == lib)
      for (auto& cd : catDrop) if (cd.second.in(mx, my)) { const std::string p = dragPath, c = cd.first; moveToCat(lib, p, c); break; }
  }
  void renameOrDelete(int lib, bool del) {
    LibView v = lv(lib);
    if (del) { deleteSelected(lib); return; }
    if (!v.sel.empty()) { const LibItem* it = findItem(v.L, v.sel); if (it) startEdit(v.teRenItem, lib == 1 ? "RENAME SOUND" : lib == 2 ? "RENAME GROOVE" : "RENAME KIT", it->name, v.sel); }
    else if (!v.cat.empty()) startEdit(v.teRenCat, "RENAME CATEGORY", v.cat, v.cat);
    else toast(lib == 1 ? "SELECT A SOUND OR A CATEGORY" : lib == 2 ? "SELECT A GROOVE OR A CATEGORY" : "SELECT A KIT OR A CATEGORY");
  }
  void labSoundUser(R c) {
    const bool modal = teActive(1); const R bar(c.x, c.y + 120, c.w - 160, 24);
    if (modal && !D()) { teBar(bar); if (ph == PH_PRESS || ph == PH_SCROLL) consumed = true; }
    if (modal && D()) globalAlpha(.3f);
    catRow(1, R(c.x, c.y + 120, c.w - 160, 22));
    const R lr(c.x, c.y + 152, c.w - 172, 4 * 36 - 6);
    itemGrid(1, lr, 4, 4, 30);
    const float bx = c.r() - 148;
    const std::string trName = isMaster() ? std::string() : (S().tracks[tr].pre.size() ? S().tracks[tr].pre : std::string(MACH_NAMES[S().tracks[tr].m]));
    if (btn(ID_LBTN + 0, R(bx, c.y + 118, 148, 26), "SAVE SOUND", true)) { if (isMaster()) toast("SELECT A TRACK"); else startEdit(TE_SAVE_S, "SAVE AS", trName); }
    if (btn(ID_LBTN + 1, R(bx, c.y + 148, 148, 26), "IMPORT WAV\xe2\x80\xa6")) { if (isMaster()) toast("SELECT A TRACK"); else openFile(5); }
    if (btn(ID_LBTN + 2, R(bx, c.y + 178, 71, 26), "RENAME")) renameOrDelete(1, false);
    if (btn(ID_LBTN + 3, R(bx + 77, c.y + 178, 71, 26), "DELETE")) renameOrDelete(1, true);
    if (btn(ID_LBTN + 4, R(bx, c.y + 208, 148, 26), "OPEN FOLDER")) { scanLib(true); openInFileManager(sndDir(sndCat)); }
    { bool a = autoLoad; if (check(ID_LBTN + 5, R(bx, c.y + 244, 148, 20), "Auto-load new", a)) { autoLoad = a; autoArmed = false; saveUiState(); toast(autoLoad ? "NEW FILES LOAD ONTO THE TRACK" : "AUTO-LOAD OFF"); } }
    { const int n = slotsInUse(); char b[48]; std::snprintf(b, sizeof b, "CLEAN UP SLOTS %d/64", n);
      if (btn(ID_SLOTCLR, R(bx, c.y + 272, 148, 24), b)) purgeSlots(); }
    if (D()) {
      std::string info;
      if (!sndSel.empty()) { const LibItem* it = findItem(sndLib, sndSel); if (it) info = upper(it->name) + " \xc2\xb7 " + (it->kind == LK_WAV ? "WAV" : "HARDER SOUND") + " \xc2\xb7 " + (it->cat.empty() ? "INBOX" : upper(it->cat)); }
      if (info.empty()) info = "CLICK = LOAD ONTO TRACK " + (isMaster() ? std::string("\xe2\x80\x94") : std::to_string(tr + 1)) + " \xc2\xb7 DRAG ONTO A CATEGORY = MOVE \xc2\xb7 DOUBLE-CLICK A CATEGORY = RENAME";
      font(fPrint, 10.5f, 1.0f); txt(c.x, c.b() - 2, fit(info, c.w - 172), th.print);
    }
    if (modal && D()) { globalAlpha(1); teBar(bar); }
  }
  void labGrooveUser(R c, float w) {
    const bool modal = teActive(2); const R bar(c.x, c.y + 30, w, 24);
    if (modal && !D()) { teBar(bar); if (ph == PH_PRESS || ph == PH_SCROLL) consumed = true; }
    if (modal && D()) globalAlpha(.3f);
    catRow(2, R(c.x, c.y + 30, w, 22));
    itemGrid(2, R(c.x, c.y + 62, w - 12, 6 * 31 - 5), 3, 6, 26);
    if (btn(ID_LBTN + 10, R(c.x, c.y + 262, 150, 28), "SAVE PATTERN", true)) startEdit(TE_SAVE_G, "SAVE AS", "P" + std::to_string(S().cur + 1) + " " + STYLES[style].name);
    if (btn(ID_LBTN + 11, R(c.x + 158, c.y + 262, 92, 28), "RENAME")) renameOrDelete(2, false);
    if (btn(ID_LBTN + 12, R(c.x + 258, c.y + 262, 92, 28), "DELETE")) renameOrDelete(2, true);
    if (btn(ID_LBTN + 13, R(c.x + 358, c.y + 262, 130, 28), "OPEN FOLDER")) { scanLib(true); openInFileManager(grvDir(grvCat)); }
    if (D()) {
      std::string info = "CLICK = LOAD INTO P" + std::to_string(S().cur + 1) + " (UNDO WITH UNDO) \xc2\xb7 DRAG ONTO A CATEGORY = MOVE";
      font(fPrint, 10.5f, 1.0f); txt(c.x, c.y + 312, fit(info, w), th.print);
    }
    if (modal && D()) { globalAlpha(1); teBar(bar); }
  }
  // ================= standalone controls: keyboard and MIDI mappings (LAB > CONTROL) =================
  std::string controlPath() { const std::string d = harderConfigDir(); return d.empty() ? d : d + "control.json"; }
  void loadControl() {
    std::string txt; JVal j;
    if (readTextFile(controlPath(), txt) && parseJson(txt, j)) cmap.fromJson(j); else cmap.defaults();
    pushPadTable(); core->midiMapped.store(true);
  }
  void saveControl() { std::string out; writeJson(out, cmap.toJson()); writeFileAtomic(controlPath(), out); pushPadTable(); }
  // notes bound to PAD are played by the audio thread (no UI latency): give it the table
  void pushPadTable() {
    uint8_t t[16][128]; std::memset(t, 0, sizeof t);
    for (int pass = 0; pass < 2; pass++)   // any-channel bindings first, exact channels override
      for (const MidiBind& b : cmap.midi) if (b.type == MT_NOTE && b.a.k == AC_PAD && (pass == 0) == (b.ch < 0)) { if (b.ch < 0) for (int c = 0; c < 16; c++) t[c][b.num] = (uint8_t)(b.a.i + 1); else t[b.ch][b.num] = (uint8_t)(b.a.i + 1); }
    // a note bound to something else on a channel must not play a pad there
    for (const MidiBind& b : cmap.midi) if (b.type == MT_NOTE && b.a.k != AC_PAD) { if (b.ch < 0) for (int c = 0; c < 16; c++) t[c][b.num] = 0; else t[b.ch][b.num] = 0; }
    for (int c = 0; c < 16; c++) for (int n = 0; n < 128; n++) core->padMap[c][n].store(t[c][n], std::memory_order_relaxed);
  }
  std::string keyLabel(int key) {
    if (!key) return "\xe2\x80\x94";
    if (key >= 0xE000) return specialKeyName(key);
    std::string n = keyNameForScancode(key); if (n.empty()) n = usKeyName(key); if (n.empty()) { char b[16]; std::snprintf(b, sizeof b, "#%02X", key); n = b; }
    return upper(n);
  }
  std::string actName(const Action& a) {
    if (a.k == AC_PARAM) {
      const PDef& d = a.tr == 8 ? T().master[a.i] : T().track(S().tracks[a.tr].m, a.i);
      return (a.tr == 8 ? std::string("FX ") : "T" + std::to_string(a.tr + 1) + " ") + (d.valid() ? d.n : "?");
    }
    return actLabel(a);
  }
  // the panel control under the mouse, as a mappable action (MIDI learn by clicking the control)
  bool learnActionForId(int id, Action& a) {
    if (id >= ID_PAT && id < ID_PAT + NPAT) { a = act(AC_PAT, id - ID_PAT); return true; }
    if (id == ID_BPM) { a = act(AC_BPM); return true; } if (id == ID_SWING) { a = act(AC_SWING); return true; }
    if ((id >= ID_ENC && id < ID_ENC + 8) || (id >= ID_CELL && id < ID_CELL + 8)) {
      const int k = id >= ID_ENC ? id - ID_ENC : id - ID_CELL;
      if (!learnFixed) { a = act(AC_ENC, k); return true; }
      const int i = pageBase() + k; if (!defOf(i).valid()) return false; a = act(AC_PARAM, i, tr); return true;
    }
    if (id >= ID_PAGE && id < ID_PAGE + 7) { a = act(AC_PAGE, id - ID_PAGE); return true; }
    if (id >= ID_TRACK && id < ID_TRACK + NTR) { a = act(AC_TRACK, id - ID_TRACK); return true; }
    if (id >= ID_TRIG && id < ID_TRIG + 16) { a = act(AC_STEP, id - ID_TRIG); return true; }
    if (id >= ID_SPAGE && id < ID_SPAGE + 4) { a = act(AC_SPAGE, id - ID_SPAGE); return true; }
    switch (id) {
      case ID_FUNC: a = act(AC_FUNC); return true; case ID_REC: a = act(AC_REC); return true; case ID_FILL: a = act(AC_FILL); return true;
      case ID_ALL: a = act(AC_ALL); return true; case ID_PLAY: a = act(AC_PLAY); return true; case ID_STOP: a = act(AC_STOP); return true;
      case ID_PERF: a = act(AC_PERF); return true; case ID_UNDO: a = act(AC_UNDO); return true;
      case ID_COPY: a = act(AC_COPY); return true; case ID_PASTE: a = act(AC_PASTE); return true; case ID_CLEAR: a = act(AC_CLEAR); return true; case ID_CLRLK: a = act(AC_CLEARLOCKS); return true;
      case ID_MUTE: a = act(AC_MUTE); return true; case ID_SOLO: a = act(AC_SOLO); return true;
      case ID_TFADER: a = act(AC_TVOL); return true; case ID_OFADER: a = act(AC_OUT); return true; case ID_PULL: a = act(AC_LAB); return true;
    }
    return false;
  }
  void setLearnTarget(const Action& a) { learnAct = a; midiLearn = true; toast("NOW MOVE / PRESS: " + actName(a)); needRepaint = true; }
  // a step key held from the keyboard or a controller (src = key or MIDI source)
  void stepKey(int src, int si, bool press) {
    if (press) {
      if (perfMode && !isMaster()) { keyPerf[src] = si; perfDown(si); }
      else if (func()) toggleTrigless(spage * 16 + si);
      else { keyHeld[src] = spage * 16 + si; edited = false; schedulePreview(); }
    } else {
      auto pf = keyPerf.find(src); if (pf != keyPerf.end()) { perfUp(pf->second); keyPerf.erase(pf); }
      auto it = keyHeld.find(src); if (it != keyHeld.end()) { const int st = it->second; keyHeld.erase(it); if (!edited) toggleStep(st); schedulePreview(); }
    }
  }
  void doButton(const Action& a, bool press, int src, int vel) {
    switch (a.k) {
      case AC_PLAYSTOP: if (press) { if (playing) stop(); else play(); } break;
      case AC_PLAY: if (press) play(); break;
      case AC_STOP: if (press) stop(); break;
      case AC_REC: if (press) { recOn = !recOn; if (recOn) pushUndo(); toast(recOn ? "LIVE REC ON" : "LIVE REC OFF"); } break;
      case AC_FILL: if (press) { if (func()) { fillLatch = !fillLatch; setFill(fillLatch); } else setFill(true); } else if (!fillLatch) setFill(false); break;
      case AC_FUNC: if (press) funcSrc.insert(src); else funcSrc.erase(src); break;
      case AC_ALL: if (press) allSrc.insert(src); else allSrc.erase(src); break;
      case AC_PERF: if (press) { if (isMaster()) toast("SELECT A TRACK"); else { perfMode = !perfMode; latched.clear(); schedulePreview(); } } break;
      case AC_UNDO: if (press) undo(); break;
      case AC_STEP: stepKey(src, a.i, press); break;
      case AC_PAD: if (press) { if (func()) toggleMute(a.i); else { core->sendAud(a.i, vel, nullptr); flashUntil[a.i] = nowMs() + 90; if (recOn && playing) { Locks L; if (vel != 110) L.set(41, vel); recPerf(a.i, &L); } } } break;
      case AC_TRACK: if (press) trackPress(a.i); break;
      case AC_TRACKPREV: case AC_TRACKNEXT: if (press) selectTrack((tr + (a.k == AC_TRACKNEXT ? 1 : -1) + NTR) % NTR); break;
      case AC_MUTE: if (press && tr < 8) toggleMute(tr); break;
      case AC_SOLO: if (press && tr < 8) toggleSolo(tr); break;
      case AC_PAT: if (press) patternPress(a.i); break;
      case AC_PAGE: if (press) { if (isMaster()) { if (a.i < 4) mpage = a.i; } else page = a.i; schedulePreview(); } break;
      case AC_PAGEPREV: case AC_PAGENEXT: if (press) { const int d = a.k == AC_PAGENEXT ? 1 : -1; if (isMaster()) mpage = (mpage + d + 4) % 4; else page = (page + d + 7) % 7; schedulePreview(); } break;
      case AC_SPAGE: if (press) spage = a.i; break;
      case AC_COPY: if (press) copyBtn(); break;
      case AC_PASTE: if (press) pasteBtn(); break;
      case AC_CLEAR: if (press) clearBtn(); break;
      case AC_CLEARLOCKS: if (press) clearLocks(); break;
      case AC_RELEASE: if (press) { latched.clear(); schedulePreview(); } break;
      case AC_LAB: if (press) { drawer = !drawer; saveUiState(); applySize(); } break;
    }
    needRepaint = true;
  }
  // knobs: v = CC value; mode ABS maps 0..127 onto the range, the relative modes step from the current value
  void doKnob(const Action& a, int v, int mode) {
    const bool rel = mode != CM_ABS; const int d = rel ? relDelta(mode, v) : 0; if (rel && !d) return;
    const bool fine = func(); const double u = v / 127.0;
    switch (a.k) {
      case AC_ENC: {
        const int i = pageBase() + a.i; const PDef& pd = defOf(i); if (!pd.valid()) return;
        const double range = pd.max - pd.min; double nv = rel ? getVal(i) + d * (range >= 50 && fine ? 0.2 : 1) : pd.min + u * range;
        nv = stepRound(clampd(nv, pd.min, pd.max), pd); if (nv != getVal(i)) setVal(i, nv, false);
        break;
      }
      case AC_PARAM: {
        const int t = a.tr, i = a.i; const PDef& pd = t == 8 ? T().master[i] : T().track(S().tracks[t].m, i); if (!pd.valid()) return;
        const double cur = t == 8 ? S().master[i] : S().tracks[t].p[i], range = pd.max - pd.min;
        double nv = rel ? cur + d * (range >= 50 && fine ? 0.2 : 1) : pd.min + u * range; nv = stepRound(clampd(nv, pd.min, pd.max), pd);
        if (nv == cur) return;
        if (t < 8 && (i == 22 || i == 23)) { S().tracks[t].p[i] = nv; core->sendParam(t, i, nv); } else setBase(t, i, nv, false);
        if (t == tr) schedulePreview();
        break;
      }
      case AC_BPM: numSet(ID_BPM, rel ? S().bpm + d * (fine ? 0.1 : 1) : 60 + v); break;      // absolute: 60-187 BPM
      case AC_SWING: numSet(ID_SWING, rel ? S().swing + d : 50 + jsround(u * 30)); break;
      case AC_OUT: { const double cur = outDb <= -80 ? -70 : outDb; const double db = rel ? clampd(cur + d * (fine ? 0.1 : 0.5), -70, 0) : iecInv(u * iec(0)); applyFader(ID_OFADER, db, true); break; }
      case AC_TVOL: {
        if (tr >= 8) return; const double cur = volToDb(S().tracks[tr].p[21]);
        const double db = rel ? clampd((cur < -69.9 ? -70 : cur) + d * (fine ? 0.1 : 0.5), -70, 6) : iecInv(u * iec(6));
        const double vol = clampd(jsround(dbToVol(db) * 10) / 10, 0, 127); if (vol != S().tracks[tr].p[21]) { setBase(tr, 21, vol, false); schedulePreview(); }
        break;
      }
    }
    needRepaint = true;
  }
  // MIDI from the audio thread: learn, or play the mapping
  void pollMidi() {
    Core::MidiMsg m; int n = 0;
    while (n++ < 1024 && core->midiPop(m)) handleMidiMsg(m);
  }
  void handleMidiMsg(const Core::MidiMsg& m) {
    const int st = m.st & 0xF0, ch = m.st & 15; const bool isNote = st == 0x80 || st == 0x90, on = st == 0x90 && m.d2 > 0;
    if (!isNote && st != 0xB0) return;
    const int type = isNote ? MT_NOTE : MT_CC;
    { char b[64]; if (isNote) std::snprintf(b, sizeof b, "NOTE %s %s \xc2\xb7 VEL %d \xc2\xb7 CH %d", noteName(m.d1).c_str(), on ? "ON" : "OFF", m.d2, ch + 1);
      else std::snprintf(b, sizeof b, "CC %d = %d \xc2\xb7 CH %d", m.d1, m.d2, ch + 1);
      lastMidi = b; needRepaint = true; }
    if (midiLearn) {
      if (learnAct.k == AC_NONE) return;
      if (isNote && !on) return;
      if (learnAct.knob() && type != MT_CC) { toast("TURN A KNOB OR MOVE A FADER"); return; }
      if (!isNote && !learnAct.knob() && m.d2 == 0) return;   // the release of a CC button
      MidiBind nb; nb.type = (int8_t)type; nb.ch = (int8_t)ch; nb.num = m.d1; nb.a = learnAct; nb.mode = CM_ABS;
      cmap.bind(nb); saveControl(); midiScroll = 0; toast(midiSrcLabel(nb) + " = " + actName(learnAct));
      learnAct = Action(); learnedSrc = midiSrcId(type, ch, m.d1); learnedUntil = nowMs() + 400;   // the rest of this gesture is not played
      return;
    }
    const int src = midiSrcId(type, ch, m.d1);
    if (src == learnedSrc && nowMs() < learnedUntil) return;
    const MidiBind* bnd = cmap.find(type, ch, m.d1); if (!bnd) return;
    const Action a = bnd->a;
    if (a.knob()) { if (type == MT_CC) doKnob(a, m.d2, bnd->mode); return; }
    bool press;
    if (isNote) press = on;
    else { press = m.d2 >= 64; const bool was = ccDown.count(src) > 0; if (press == was) return; if (press) ccDown.insert(src); else ccDown.erase(src); }
    if (a.k == AC_PAD && isNote && core->padMap[ch][m.d1].load() == a.i + 1) {   // already played by the audio thread
      if (on) { flashUntil[a.i] = nowMs() + 90; if (recOn && playing) { Locks L; L.set(41, m.d2); recPerf(a.i, &L); } }
      return;
    }
    doButton(a, press, src, isNote ? m.d2 : 110);
  }
  void learnKey(int key) {
    const Action a = keyLearn; keyLearn = Action();
    if (key == 0x0E || key == 0x53 || key == 0xE07F) { cmap.setKey(a, 0); saveControl(); toast(actName(a) + ": NO KEY"); return; }   // Backspace / Delete clear
    if (!keyAssignable(key)) { toast("THAT KEY CANNOT BE USED"); return; }
    const Action lost = cmap.setKey(a, key); saveControl();
    toast(keyLabel(key) + " = " + actName(a) + (lost.k != AC_NONE ? " (WAS " + actName(lost) + ")" : ""));
  }
  // LAB > CONTROL
  void labControl(R c) {
    h2(c.x, c.y + 12, "CONTROL", "computer keyboard and MIDI controller");
    const auto& G = actGroups(); ctlGroup = (int)clampd(ctlGroup, 0, (int)G.size() - 1);
    float x = c.x;
    for (int g = 0; g < (int)G.size(); g++) { const float w = chipW(G[g].name, true); if (chip(ID_CGROUP + g, R(x, c.y + 24, w, 22), G[g].name, g == ctlGroup, true)) { ctlGroup = g; keyLearn = Action(); } x += w + 5; }
    const std::vector<Action>& L = G[ctlGroup].acts;
    if (D()) { font(fPrint, 9.5f, 1.2f); for (int col = 0; col < (L.size() > 9 ? 2 : 1); col++) { txt(c.x + 118 + col * 290, c.y + 66, "KEY", th.print); txt(c.x + 188 + col * 290, c.y + 66, "MIDI", th.print); } }
    for (int k = 0; k < (int)L.size() && k < 18; k++) {
      const Action& a = L[k]; const float rx = c.x + (k / 9) * 290, ry = c.y + 72 + (k % 9) * 26;
      if (D()) { font(fPrint, 11.5f, 0.6f); txt(rx, ry + 15, fit(actName(a), 112), th.printHi); }
      // key
      const R kr(rx + 116, ry, 66, 21);
      if (a.knob()) { if (D()) { font(fPrint, 11, 1); txt(kr.cx(), kr.y + 15, "\xe2\x80\x94", th.print, ALIGN_CENTER | ALIGN_BASELINE); } }
      else {
        const bool learning = keyLearn == a && keyLearn.k != AC_NONE;
        const int key = cmap.keyFor(a);
        if (chip(ID_CKEY + k, kr, learning ? (std::fmod(nowMs(), 800) < 500 ? "PRESS\xe2\x80\xa6" : "") : keyLabel(key), learning || key, true)) { keyLearn = learning ? Action() : a; midiLearn = false; learnAct = Action(); }
        if (learning) needRepaint = true;
      }
      // MIDI
      const R mr(rx + 186, ry, 92, 21); const std::vector<int> mb = cmap.midiFor(a);
      const bool ml = midiLearn && learnAct == a && learnAct.k != AC_NONE;
      std::string ms = mb.empty() ? "\xe2\x80\x94" : (cmap.midi[mb[0]].type == MT_NOTE ? noteName(cmap.midi[mb[0]].num) : "CC " + std::to_string(cmap.midi[mb[0]].num)) + (mb.size() > 1 ? " +" + std::to_string(mb.size() - 1) : "");
      if (ml) ms = std::fmod(nowMs(), 800) < 500 ? "MOVE\xe2\x80\xa6" : "";
      if (chip(ID_CMIDI + k, mr, ms, ml || !mb.empty(), true)) { if (ml) { learnAct = Action(); } else { keyLearn = Action(); setLearnTarget(a); } }
      if (ml) needRepaint = true;
    }
    if (D()) {
      std::string hint = G[ctlGroup].name == std::string("KNOBS")
        ? "ENCODER n follows the panel: track, page and held steps (hold a step + turn = parameter lock)."
        : "Click KEY, then press a key (Backspace: none). Click MIDI, then press a pad or key on the controller.";
      font(fPrint, 10.5f, 0.3f); txt(c.x, c.b() - 2, fit(hint, 570), th.print);
    }
    // ---- MIDI column
    const float rx = c.x + 596, rw = c.r() - rx;
    if (D()) { beginPath(); rect(rx - 14, c.y + 4, 1, c.h - 8); fillColor(th.modEdge); fill(); }
    h2(rx, c.y + 12, "MIDI");
    if (btn(ID_CLEARN, R(rx, c.y + 24, 120, 28), midiLearn ? "LEARN ON" : "MIDI LEARN", midiLearn)) { midiLearn = !midiLearn; learnAct = Action(); keyLearn = Action(); if (midiLearn) toast("CLICK A CONTROL, THEN MOVE THE CONTROLLER"); }
    { const char* L2[2] = { "FOLLOW PANEL", "FIXED PARAM" }; float x2 = rx + 128; if (D()) { font(fPrint, 9.5f, 1.2f); txt(x2, c.y + 30, "ENCODERS LEARN AS", th.print); }
      for (int k = 0; k < 2; k++) { const float w = chipW(L2[k], true); if (chip(ID_CFIXED + k, R(x2, c.y + 34, w, 20), L2[k], learnFixed == (k == 1), true)) learnFixed = k == 1; x2 += w + 4; } }
    if (D()) {
      std::string s; Color col = th.print;
      if (midiLearn) { col = th.led; s = learnAct.k == AC_NONE ? "CLICK A BUTTON, KNOB OR FADER ON THE PANEL (OR A MIDI CELL)" : "NOW MOVE / PRESS ON THE CONTROLLER: " + actName(learnAct); }
      else s = lastMidi.empty() ? (midiNames.empty() ? "NO MIDI INPUT \xc2\xb7 LAB > AUDIO" : "MIDI IN: WAITING") : "LAST: " + lastMidi;
      font(fPrint, 10.5f, 0.8f); txt(rx, c.y + 72, fit(s, rw), col);
    }
    // bindings
    const int vis = 7; const R lr(rx, c.y + 80, rw, vis * 26);
    const int n = (int)cmap.midi.size(), maxScroll = std::max(0, n - vis);
    if (scrolled(lr)) midiScroll = (int)clampd(midiScroll + (wheel > 0 ? -1 : 1), 0, maxScroll);
    midiScroll = std::min(midiScroll, maxScroll);
    int del = -1;
    for (int k = midiScroll; k < n && k < midiScroll + vis; k++) {   // newest first
      const int bi = n - 1 - k; MidiBind& b = cmap.midi[bi]; const float ry = lr.y + (k - midiScroll) * 26; const int vk = k - midiScroll;
      if (D()) { font(fNum, 9.5f); txt(rx, ry + 15, midiSrcLabel(b), th.print); font(fPrint, 11.5f, 0.5f); txt(rx + 132, ry + 15, fit(actName(b.a), rw - 132 - 104), th.printHi); }
      if (b.type == MT_CC && b.a.knob()) {
        if (chip(ID_CMODE + vk, R(rx + rw - 100, ry + 1, 70, 20), CC_MODE_NAMES[b.mode], b.mode != CM_ABS, true)) { b.mode = (int8_t)((b.mode + 1) % CM_COUNT); saveControl(); toast(std::string("CC ") + std::to_string(b.num) + ": " + CC_MODE_NAMES[b.mode]); }
      }
      if (chip(ID_CDEL + vk, R(rx + rw - 24, ry + 1, 22, 20), "\xc3\x97", false, true)) del = bi;
    }
    if (del >= 0) { cmap.midi.erase(cmap.midi.begin() + del); saveControl(); toast("MAPPING REMOVED"); }
    if (n == 0 && D()) para(rx, lr.y + 4, rw, "No MIDI mappings. MIDI LEARN, click a control, move the controller.", th.print, 11.5f);
    if (D() && maxScroll > 0) { const float th_ = lr.h * vis / (vis + maxScroll); rrect(R(lr.r() + 4, lr.y, 3, lr.h), 1.5f, th.range); rrect(R(lr.r() + 4, lr.y + (lr.h - th_) * midiScroll / maxScroll, 3, th_), 1.5f, th.print); }
    if (btn(ID_CRESET, R(rx, c.y + 270, 120, 26), "RESET KEYS") && confirm("resetkeys")) { cmap.defaultKeys(); saveControl(); toast("DEFAULT KEYS"); }
    if (btn(ID_CRESET + 1, R(rx + 128, c.y + 270, 120, 26), "RESET MIDI") && confirm("resetmidi")) { cmap.defaultMidi(); saveControl(); midiScroll = 0; toast("DEFAULT MIDI MAPPING"); }
    if (D()) { font(fPrint, 9.5f, 0.6f); txt(rx, c.y + 314, fit("CC 120/123 = ALL SOUND OFF \xc2\xb7 PROGRAM CHANGE 1-8 = PATTERN", rw), th.print); }
  }
  void labKits(R c) {
    h2(c.x, c.y + 12, "KITS", kitSrc == 0 ? "one per material \xc2\xb7 replace all 8 sounds, keep patterns" : "your kits \xc2\xb7 all 8 sounds with their samples");
    { const float wf = chipW("FACTORY", true), wu = chipW("USER", true); float x = c.r() - wf - wu - 5;
      for (int k = 0; k < 2; k++) { const char* l = k ? "USER" : "FACTORY"; const float w = k ? wu : wf; if (chip(ID_KSRC + k, R(x, c.y - 2, w, 20), l, kitSrc == k, true)) { kitSrc = k; saveUiState(); if (k) scanLib(true); } x += w + 5; } }
    if (kitSrc == 1) { labKitsUser(c); return; }
    const float cw = (c.w - 3 * 10) / 4, chh = 82;
    for (int k = 0; k < NKITS; k++) {
      R r(c.x + (k % 4) * (cw + 10), c.y + 30 + (k / 4) * (chh + 10), cw, chh);
      if (clicked(ID_KIT + k, r)) loadKit(k);
      if (D()) {
        rstroke(r, 8, k == S().kit ? th.led : th.chipEdge);
        font(fMark, 11, 2.2f); txt(r.x + 12, r.y + 22, KITS[k].name, th.printHi);
        para(r.x + 12, r.y + 32, cw - 24, KITS[k].desc, th.print, 12);
      }
    }
    para(c.x, c.y + 230, c.w, "Kits set the eight sounds and the master FX character. The master volume stays where it is; patterns are untouched. USER: save your own kits (SAMPLE tracks keep their samples).", th.print, 12);
  }
  void labKitsUser(R c) {
    const bool modal = teActive(3); const R bar(c.x, c.y + 30, c.w - 172, 24);
    if (modal && !D()) { teBar(bar); if (ph == PH_PRESS || ph == PH_SCROLL) consumed = true; }
    if (modal && D()) globalAlpha(.3f);
    catRow(3, R(c.x, c.y + 30, c.w - 172, 22));
    itemGrid(3, R(c.x, c.y + 62, c.w - 184, 6 * 36 - 6), 3, 6, 30);
    const float bx = c.r() - 148;
    if (btn(ID_LBTN + 20, R(bx, c.y + 28, 148, 26), "SAVE KIT", true)) startEdit(TE_SAVE_K, "SAVE KIT AS", "Kit " + std::to_string((int)kitLib.items.size() + 1));
    if (btn(ID_LBTN + 21, R(bx, c.y + 58, 71, 26), "RENAME")) renameOrDelete(3, false);
    if (btn(ID_LBTN + 22, R(bx + 77, c.y + 58, 71, 26), "DELETE")) renameOrDelete(3, true);
    if (btn(ID_LBTN + 23, R(bx, c.y + 88, 148, 26), "OPEN FOLDER")) { scanLib(true); openInFileManager(kitDir(kitCat)); }
    { bool a = kitWithMaster; if (check(ID_LBTN + 24, R(bx, c.y + 124, 148, 20), "Save master FX", a)) { kitWithMaster = a; saveUiState(); } }
    if (D()) {
      para(bx, c.y + 152, 148, "A kit holds the eight tracks' sounds and roles. SAMPLE tracks keep their samples; loading puts them into free slots.", th.print, 11);
      std::string info = "CLICK = LOAD ALL 8 TRACKS (UNDO WITH UNDO) \xc2\xb7 DRAG ONTO A CATEGORY = MOVE \xc2\xb7 DOUBLE-CLICK A CATEGORY = RENAME";
      if (!kitSel.empty()) { const LibItem* it = findItem(kitLib, kitSel); if (it) info = upper(it->name) + " \xc2\xb7 " + (it->cat.empty() ? "UNSORTED" : upper(it->cat)); }
      font(fPrint, 10.5f, 1.0f); txt(c.x, c.b() - 2, fit(info, c.w - 172), th.print);
    }
    if (modal && D()) { globalAlpha(1); teBar(bar); }
  }
  // ================= LAB > SAMPLE: slices and resampling =================
  // the SAMPLE track's sample, replaced by a copy with other slice points (undo restores the old one)
  void setSlicePoints(const std::vector<int>& pts, const std::string& msg) {
    if (isMaster() || S().tracks[tr].m != M_SMP) { toast("SELECT A SAMPLE TRACK"); return; }
    const int slot = slotOf(tr); const SampleRef cur = S().smp[slot]; if (!cur) { toast("NO SAMPLE IN THIS SLOT"); return; }
    pushUndo(); S().smp[slot] = core->cache.add(withSlices(*cur, pts)); core->sendSamples(); schedulePreview(); toast(msg);
  }
  void detectNow() {
    if (isMaster() || S().tracks[tr].m != M_SMP || !S().smp[slotOf(tr)]) { toast("SELECT A SAMPLE TRACK"); return; }
    const std::vector<int> pts = detectSlices(*S().smp[slotOf(tr)], sliceSens);
    setSlicePoints(pts, std::to_string(pts.size()) + " SLICES FOUND");
    if (S().tracks[tr].p[51] != 5) setVal(51, 5, false);
  }
  // resampling: buffers for the audio thread (60 s at the engine rate), owned here
  struct RecBuf { std::unique_ptr<float[]> L, R; int64_t cap = 0; uint64_t seq = 0, armSeq = 0; };
  RecBuf recBuf; std::vector<RecBuf> recRetired; bool recArmed = false; int recSrc = -1, recLenIdx = 0, recStartIdx = 0; double recStartedAt = 0;
  static constexpr double REC_STEPS[5] = { 0, 16, 32, 64, 128 };
  void recArm() {
    if (recArmed) return;
    const double sr = core->eng->sr; RecBuf b; b.cap = (int64_t)(sr * SAMPLE_MAX_SECONDS);
    try { b.L.reset(new float[(size_t)b.cap]); b.R.reset(new float[(size_t)b.cap]); } catch (const std::bad_alloc&) { toast("NOT ENOUGH MEMORY"); return; }
    Cmd c; c.t = Cmd::REC; c.f1 = b.L.get(); c.f2 = b.R.get(); c.s = (int)b.cap; c.tr = (int8_t)recSrc; c.v = REC_STEPS[recLenIdx]; c.flag = recStartIdx == 1;
    b.seq = b.armSeq = core->link.push(c); if (b.seq == UINT64_MAX) { toast("BUSY, TRY AGAIN"); return; }
    recBuf = std::move(b); recArmed = true; recStartedAt = nowMs();
    toast(recStartIdx == 1 ? "RECORDING" : playing ? "RECORDS FROM THE NEXT PATTERN START" : "RECORDS WHEN THE PATTERN STARTS: PRESS PLAY");
  }
  // waiting: cancelled; recording: stops here and keeps what was recorded (the engine reports DONE)
  void recCancel() {
    if (!recArmed) return;
    Cmd c; c.t = Cmd::RECCANCEL; const uint64_t seq = core->link.push(c); if (seq != UINT64_MAX) recBuf.seq = seq;
  }
  void recPoll() {
    // buffers the audio thread may still have written to are freed once it is past the cancel
    const uint64_t done = core->link.consumed.load(std::memory_order_acquire);
    recRetired.erase(std::remove_if(recRetired.begin(), recRetired.end(), [done](const RecBuf& b) { return b.seq < done; }), recRetired.end());
    if (!recArmed || done <= recBuf.armSeq) return;   // the engine has not taken the buffers yet
    const int st = core->eng->fb.recState.load(std::memory_order_acquire);
    if (st == REC_ON || st == REC_WAIT) { needRepaint = true; return; }
    if (st == REC_IDLE) { if (done > recBuf.seq) { recRetired.push_back(std::move(recBuf)); recBuf = RecBuf(); recArmed = false; toast("RESAMPLE CANCELLED"); } return; }
    // done: build the sample (mono when both channels are the same), save it to the library and load it
    const int64_t n = core->eng->fb.recN.load(); recArmed = false;
    if (n < 2) { recBuf = RecBuf(); toast("NOTHING RECORDED"); return; }
    std::shared_ptr<SampleData> sd(new SampleData()); sd->rate = core->eng->sr; sd->fmt = SF_F32;
    bool same = true; for (int64_t i = 0; i < n && same; i++) if (recBuf.L[i] != recBuf.R[i]) same = false;
    sd->ch = same ? 1 : 2; sd->d.assign(recBuf.L.get(), recBuf.L.get() + n); if (!same) sd->dR.assign(recBuf.R.get(), recBuf.R.get() + n);
    recBuf = RecBuf();
    const std::string dir = sndDir("Resampled"); const std::string what = recSrc < 0 ? std::string("Main") : "T" + std::to_string(recSrc + 1);
    const std::string path = makeDir(dir) ? uniquePath(dir, "Resample " + what, ".wav") : std::string();
    sd->name = path.empty() ? "Resample " + what : stemOf(fileName(path));
    finishSample(*sd); SampleRef ref = core->cache.add(sd);
    if (!path.empty()) { const std::vector<uint8_t> w = encodeWav(*ref); if (writeFileAtomic(path, w.data(), w.size())) markSelf(path); }
    SoundDef d; wavSound(ref, d); d.name = ref->name;
    if (!isMaster() && applySound(d, d.name)) toast("RESAMPLED " + upper(d.name) + " \xc2\xb7 ON TRACK " + std::to_string(tr + 1));
    else toast("RESAMPLED: LIBRARY/SOUNDS/RESAMPLED");
    scanLib(true);
  }
  void labSample(R c) {
    const bool smpTr = !isMaster() && S().tracks[tr].m == M_SMP; const Step* hst = heldStepPtr();
    const SampleData* sd = smpTr ? sampleOf(tr, hst) : nullptr;
    h2(c.x, c.y + 12, "SAMPLE", isMaster() ? "select a track" : smpTr ? "track " + std::to_string(tr + 1) : "track " + std::to_string(tr + 1) + " is not a SAMPLE track");
    // waveform with slice points; the slice the held step (or step 1) plays is lit
    const R wv(c.x, c.y + 26, 600, 138);
    if (D()) {
      rrect(wv, 6, th.oledBg); rstroke(wv, 6, th.numEdge);
      if (!sd || sd->len < 2) { font(fOled, 11); txt(wv.cx(), wv.cy(), smpTr ? "NO SAMPLE IN THIS SLOT" : "SAMPLE MACHINE: LAB > SOUND > ENGINE", th.oledMid, ALIGN_CENTER | ALIGN_BASELINE); }
      else {
        const float x0 = wv.x + 8, w = wv.w - 16, top = wv.y + 10, bot = wv.b() - 18, cy = (top + bot) / 2, amp = (bot - top) / 2 / std::max(0.05f, sd->peak);
        const int slc = (int)clampd(jsround(getVal(51)), 0, 5), sel = (int)clampd(jsround(getVal(52)), 0, 64);
        const auto hs = heldSteps(); const int step = hs.empty() ? 0 : hs[0];
        if (slc > 0) { double lo, hi; MSmp::sliceRange(*sd, slc, sel, step, lo, hi); beginPath(); rect(x0 + (float)(lo / sd->len) * w, top - 4, std::max(1.f, (float)((hi - lo) / sd->len) * w), bot - top + 8); fillColor(A(th.oled, .14f)); fill(); }
        beginPath();
        for (int x = 0; x < (int)w; x++) {
          const int k0 = (int)((double)x / w * OVERVIEW_N), k1 = std::max(k0 + 1, (int)((double)(x + 1) / w * OVERVIEW_N)); float mn = 0, mxv = 0;
          for (int k = k0; k < k1 && k < OVERVIEW_N; k++) { mn = std::min(mn, sd->ov[k * 2]); mxv = std::max(mxv, sd->ov[k * 2 + 1]); }
          rect(x0 + x, cy - mxv * amp, 0.9f, std::max(1.f, (mxv - mn) * amp));
        }
        fillColor(th.oled); fill();
        // slice boundaries: the points (AUTO) or the equal grid
        std::vector<double> b;
        if (slc == 5 && !sd->slices.empty()) for (int x : sd->slices) b.push_back(x);
        else if (slc > 0) { const int ns = slc == 5 ? 16 : (8 << (slc - 1)); for (int k = 0; k < ns; k++) b.push_back(std::floor((double)k * sd->len / ns)); }
        else for (int x : sd->slices) b.push_back(x);
        font(fOled, 8);
        for (size_t k = 0; k < b.size(); k++) { const float bx = x0 + (float)(b[k] / sd->len) * w; beginPath(); rect(bx, top - 4, 1, bot - top + 8); fillColor(slc > 0 ? th.oledMid : th.oledDim); fill(); if (slc > 0 && (b.size() <= 32 || k % 2 == 0)) txt(bx + 2, top + 2, std::to_string(k + 1), th.oledMid); }
        char ib[160]; std::snprintf(ib, sizeof ib, "S%02d %s \xc2\xb7 %s \xc2\xb7 %.1fKHZ \xc2\xb7 %.2fS \xc2\xb7 %d SLICE POINTS", slotOf(tr, hst) + 1, upper(sd->name).c_str(), sd->stereo() ? "STEREO" : "MONO", sd->rate / 1000, sd->seconds(), (int)sd->slices.size());
        font(fOled, 9); txt(wv.x + 8, wv.b() - 5, fit(ib, wv.w - 16), th.oledMid);
      }
    }
    // slicing
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, c.y + 186, "SLICES", th.print); }
    { static const char* const SL[6] = { "OFF", "8", "16", "32", "64", "AUTO" }; float x = c.x + 70;
      for (int k = 0; k < 6; k++) { const float w = chipW(SL[k], true); if (chip(ID_SMPTAB + k, R(x, c.y + 172, w, 20), SL[k], smpTr && (int)jsround(getVal(51)) == k, true, !smpTr)) {
          setVal(51, k, false); }
        x += w + 5; } }
    slider(ID_SMPTAB + 10, R(c.x, c.y + 202, 330, 22), "SENSITIVITY", sliceSens, 0, 100);
    if (btn(ID_SMPTAB + 11, R(c.x + 344, c.y + 198, 120, 28), "DETECT")) { saveUiState(); detectNow(); }
    if (btn(ID_SMPTAB + 12, R(c.x + 472, c.y + 198, 128, 28), "CLEAR POINTS")) setSlicePoints({}, "SLICE POINTS CLEARED");
    para(c.x, c.y + 236, 600, "Step n plays slice n (SLCE = STEP); lock SLCE on a step (STEP page) to play another one, or set it for the whole track. STRT, END, MODE and LOOP work inside the slice, so REV plays a slice backwards. AUTO uses the slice points: DETECT finds the hits, WAV cue points are read and written.", th.print, 11.5f);
    // ---- resample
    const float rx = c.x + 630, rw = c.r() - rx;
    if (D()) { beginPath(); rect(rx - 16, c.y + 4, 1, c.h - 8); fillColor(th.modEdge); fill(); }
    h2(rx, c.y + 12, "RESAMPLE", "into a new sample");
    const bool busy = recArmed;
    if (D()) { font(fPrint, 11, 1.5f); txt(rx, c.y + 44, "SOURCE", th.print); txt(rx, c.y + 98, "LENGTH", th.print); txt(rx, c.y + 128, "START", th.print); }
    { float x = rx + 70; for (int k = -1; k < NT; k++) { const std::string l = k < 0 ? "MAIN" : "T" + std::to_string(k + 1); const float w = k < 0 ? 52 : 30; const float yy = k < 4 ? c.y + 30 : c.y + 56; if (k == 4) x = rx + 70 + 57;
        if (chip(ID_SMPTAB + 20 + (k + 1), R(x, yy, w, 20), l, recSrc == k, true, busy)) recSrc = k; x += w + 5; } }
    { static const char* const LL[5] = { "PATTERN", "1 BAR", "2", "4", "8 BARS" }; float x = rx + 70;
      for (int k = 0; k < 5; k++) { const float w = chipW(LL[k], true); if (chip(ID_SMPTAB + 30 + k, R(x, c.y + 84, w, 20), LL[k], recLenIdx == k, true, busy)) recLenIdx = k; x += w + 4; } }
    { static const char* const SS[2] = { "NEXT PATTERN", "NOW" }; float x = rx + 70;
      for (int k = 0; k < 2; k++) { const float w = chipW(SS[k], true); if (chip(ID_SMPTAB + 36 + k, R(x, c.y + 114, w, 20), SS[k], recStartIdx == k, true, busy)) recStartIdx = k; x += w + 5; } }
    std::string bl = "RECORD";
    if (busy) { const int st = core->eng->fb.recState.load(); const int64_t n = core->eng->fb.recN.load(), L = std::max<int64_t>(1, core->eng->fb.recLen.load());
      bl = st == REC_ON ? "RECORDING " + std::to_string((int)(100 * n / L)) + "% \xc2\xb7 STOP" : "WAITING \xc2\xb7 CANCEL"; }
    if (btn(ID_SMPTAB + 40, R(rx, c.y + 146, rw, 32), bl, !busy)) { if (busy) recCancel(); else recArm(); }
    para(rx, c.y + 188, rw, "MAIN records the output, T1-T8 one track (after pan and volume, before the FX). The result is saved to Library/Sounds/Resampled and loaded onto the selected track (a free slot), mono if both sides are the same. Up to 60 s.", th.print, 11.5f);
  }

  // ================= LAB > SONG =================
  int songScroll = 0, songSel = 0;
  void songEdited() { core->sendArr(); needRepaint = true; }
  double songBars() const { double st = 0; for (int r = 0; r < core->S->song.n; r++) st += core->S->patterns[core->S->song.rows[r].pat].mlen * core->S->song.rows[r].reps; return st / 16; }
  void labSong(R c) {
    Arrangement& G = S().song; songSel = (int)clampd(songSel, 0, std::max(0, G.n - 1));
    char hb[96]; std::snprintf(hb, sizeof hb, "%d rows \xc2\xb7 %.1f bars%s", G.n, songBars(), S().seq == ARR_SONG ? " \xc2\xb7 plays (SNG)" : " \xc2\xb7 select SNG to play it");
    h2(c.x, c.y + 12, "SONG", hb);
    const int ar = playing && S().seq == ARR_SONG ? core->eng->fb.arrRow.load() : -1, arep = core->eng->fb.arrRep.load();
    if (D()) { font(fPrint, 9.5f, 1.2f); const float y = c.y + 36; txt(c.x, y, "ROW", th.print); txt(c.x + 40, y, "PATTERN", th.print); txt(c.x + 258, y, "REPEAT", th.print); txt(c.x + 346, y, "MUTE TRACKS", th.print); txt(c.x + 520, y, "TEMPO", th.print); }
    const int vis = 10; const R lr(c.x, c.y + 44, 600, vis * 26);
    const int maxScroll = std::max(0, G.n - vis);
    if (scrolled(lr)) songScroll = (int)clampd(songScroll + (wheel > 0 ? -1 : 1), 0, maxScroll);
    songScroll = std::min(songScroll, maxScroll);
    int del = -1;
    for (int r = songScroll; r < G.n && r < songScroll + vis; r++) {
      ArrRow& w = G.rows[r]; const float y = lr.y + (r - songScroll) * 26; const int vk = r - songScroll, id = ID_SONG + vk * 40;
      if (D() && r == ar) { rrect(R(c.x - 4, y - 2, 604, 24), 5, A(th.led, .10f)); }
      char nb[16]; std::snprintf(nb, sizeof nb, "%02d", r + 1);
      if (chip(id, R(c.x, y, 30, 20), nb, r == songSel, true)) { songSel = r; if (G.start != r) { G.start = r; songEdited(); } }
      for (int p = 0; p < NPAT; p++) if (chip(id + 1 + p, R(c.x + 40 + p * 26, y, 24, 20), std::to_string(p + 1), w.pat == p, true)) { if (w.pat != p) { pushUndo(); w.pat = (int8_t)p; songEdited(); } songSel = r; }
      if (chip(id + 10, R(c.x + 258, y, 20, 20), "\xe2\x88\x92", false, true, w.reps <= 1)) { pushUndo(); w.reps--; songEdited(); }
      if (D()) { font(fNum, 11); std::string rs = (r == ar ? std::to_string(arep + 1) + "/" : std::string("\xc3\x97")) + std::to_string(w.reps); txt(c.x + 302, y + 14, rs, r == ar ? th.led : th.printHi, ALIGN_CENTER | ALIGN_BASELINE); }
      if (chip(id + 11, R(c.x + 318, y, 20, 20), "+", false, true, w.reps >= 99)) { pushUndo(); w.reps++; songEdited(); }
      for (int t = 0; t < NT; t++) { const bool m = (w.mute >> t) & 1; if (chip(id + 12 + t, R(c.x + 346 + t * 21, y, 19, 20), std::to_string(t + 1), m, true)) { pushUndo(); w.mute ^= (uint8_t)(1 << t); songEdited(); } }
      { char bb[16]; if (w.bpm > 0) std::snprintf(bb, sizeof bb, "%.1f", w.bpm); else std::snprintf(bb, sizeof bb, "PTN");
        const R br(c.x + 520, y, 50, 20);
        if (chip(id + 21, br, bb, w.bpm > 0, true)) { pushUndo(); w.bpm = w.bpm > 0 ? 0 : (float)S().patterns[w.pat].bpm; songEdited(); }
        if (scrolled(br) && w.bpm > 0) { w.bpm = (float)clampd(jsround((w.bpm + (wheel > 0 ? 1 : -1) * ((mods & kModifierShift) ? 0.1 : 1)) * 10) / 10, 40, 300); songEdited(); } }
      if (chip(id + 22, R(c.x + 578, y, 20, 20), "\xc3\x97", false, true)) del = r;
    }
    if (del >= 0) { pushUndo(); for (int r = del; r + 1 < G.n; r++) G.rows[r] = G.rows[r + 1]; G.n--; if (G.start >= G.n) G.start = std::max(0, G.n - 1); songEdited(); }
    if (G.n == 0 && D()) para(c.x, lr.y + 6, 590, "No rows yet. + ROW adds the current pattern. Each row plays its pattern the number of times shown, can mute tracks and set its own tempo (PTN = the pattern's tempo). Select SNG next to the pattern keys and press PLAY; in Live the song follows the arrangement position.", th.print, 12);
    if (D() && maxScroll > 0) { const float th_ = lr.h * vis / (vis + maxScroll); rrect(R(lr.r() + 4, lr.y, 3, lr.h), 1.5f, th.range); rrect(R(lr.r() + 4, lr.y + (lr.h - th_) * songScroll / maxScroll, 3, th_), 1.5f, th.print); }
    // ---- right column
    const float rx = c.x + 640, rw = c.r() - rx;
    auto ins = [&](int at, const ArrRow& w) { if (G.n >= NROW) { toast("SONG IS FULL (128 ROWS)"); return; } pushUndo(); for (int r = G.n; r > at; r--) G.rows[r] = G.rows[r - 1]; G.rows[at] = w; G.n++; songSel = at; if (songSel >= songScroll + vis) songScroll = songSel - vis + 1; songEdited(); };
    if (btn(ID_SONGBTN, R(rx, c.y + 30, 150, 28), "+ ROW (P" + std::to_string(S().cur + 1) + ")", true)) { ArrRow w; w.pat = (int8_t)S().cur; ins(G.n, w); }
    if (btn(ID_SONGBTN + 1, R(rx + 158, c.y + 30, 150, 28), "DUPLICATE ROW") && G.n) ins(songSel + 1, G.rows[songSel]);
    if (btn(ID_SONGBTN + 2, R(rx, c.y + 64, 100, 28), "MOVE UP") && songSel > 0) { pushUndo(); std::swap(G.rows[songSel], G.rows[songSel - 1]); songSel--; songEdited(); }
    if (btn(ID_SONGBTN + 3, R(rx + 104, c.y + 64, 100, 28), "MOVE DOWN") && songSel + 1 < G.n) { pushUndo(); std::swap(G.rows[songSel], G.rows[songSel + 1]); songSel++; songEdited(); }
    if (btn(ID_SONGBTN + 4, R(rx + 208, c.y + 64, 100, 28), "CLEAR") && G.n && confirm("clearsong")) { pushUndo(); G.n = 0; G.start = 0; songEdited(); toast("SONG CLEARED"); }
    if (btn(ID_SONGBTN + 5, R(rx, c.y + 98, 150, 28), "CHAIN TO SONG") ) {
      if (S().chain.empty()) toast("NO CHAIN"); else { pushUndo(); for (int8_t p : S().chain) { if (G.n >= NROW) break; ArrRow w; w.pat = p; if (G.n && G.rows[G.n - 1].pat == p && G.rows[G.n - 1].reps < 99 && !G.rows[G.n - 1].mute && G.rows[G.n - 1].bpm == 0) G.rows[G.n - 1].reps++; else G.rows[G.n++] = w; } songEdited(); toast("CHAIN COPIED INTO THE SONG"); } }
    if (D()) { font(fPrint, 11, 1.5f); txt(rx, c.y + 150, "AT THE END", th.print); }
    { float x = rx + 96; for (int k = 0; k < 2; k++) { const char* l = k ? "STOP" : "LOOP"; const float w = chipW(l, true); if (chip(ID_SONGBTN + 6 + k, R(x, c.y + 136, w, 20), l, G.loop == (k == 0), true)) { pushUndo(); G.loop = k == 0; songEdited(); } x += w + 5; } }
    if (D()) { font(fPrint, 11, 1.5f); txt(rx, c.y + 180, "PLAY FROM", th.print); font(fNum, 12); txt(rx + 96, c.y + 180, G.n ? "ROW " + std::to_string(G.start + 1) : "\xe2\x80\x94", th.printHi); }
    para(rx, c.y + 196, rw, "Click a row number to play from there. A pattern key pressed while the song plays takes one loop, then the song goes on. Live: the song follows Live's position (bar 1 = row 1); with STOP it is silent after the last row.", th.print, 11.5f);
  }

  void labPerform(R c) {
    h2(c.x, c.y + 12, "PERFORM", "perf pads");
    para(c.x, c.y + 26, c.w, "Press PERF on the face: pads 1\xe2\x80\x93" "4 roll retrigs while held, 5\xe2\x80\x93" "8 play velocities, 9\xe2\x80\x93" "12 and 13\xe2\x80\x93" "16 lock the two parameters below. With REC on and the pattern running, pad hits are written as trigs with those values.", th.print, 12.5f);
    const int mid = S().tracks[isMaster() ? 0 : tr].m;
    for (int which = 0; which < 2; which++) {
      const float y = c.y + 84 + which * 120;
      if (D()) { font(fPrint, 11, 1.5f); txt(c.x, y + 14, which ? "PARAM B \xc2\xb7 PADS 13\xe2\x80\x93" "16" : "PARAM A \xc2\xb7 PADS 9\xe2\x80\x93" "12", th.print); }
      float x = c.x, yy = y + 24;
      for (int k = 0; k < NPERFP; k++) {
        const int i = PERF_PARAMS[k]; const PDef& d = T().track(mid, i); if (!d.valid()) continue;
        const std::string lab = std::string(i < 8 ? "SRC " : i < 16 ? "FLTR " : i < 24 ? "AMP " : i < 32 ? "FX " : "TRIG ") + d.n;
        const float w = chipW(lab, true); if (x + w > c.r()) { x = c.x; yy += 28; }
        int& tgt = which ? S().perfB : S().perfA;
        if (chip((which ? ID_PERFB : ID_PERFA) + k, R(x, yy, w, 22), lab, tgt == i, true)) tgt = i;
        x += w + 5;
      }
    }
  }
  void labProject(R c) {
    h2(c.x, c.y + 12, "PROJECT", "web-compatible .json");
    if (btn(ID_SAVE, R(c.x, c.y + 30, 170, 32), "SAVE PROJECT")) openFile(1);
    if (btn(ID_LOAD, R(c.x + 178, c.y + 30, 170, 32), "LOAD PROJECT")) openFile(2);
    para(c.x, c.y + 70, 390, standalone ? "Saved automatically to Documents/HARDER every few seconds and on exit; the next start continues there. Files from the web groovebox and the plugin load here unchanged."
                                        : "The whole project is also stored inside the Live set. Files saved from the web groovebox load here unchanged, and the other way round.", th.print, 12);
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, c.y + 128, "UI SIZE", th.print); }
    float x = c.x + 70;
    for (int k = 0; k < 6; k++) { const float w = chipW(UI_SCALE_NAMES[k], true); if (chip(ID_SCALE + k, R(x, c.y + 114, w, 22), UI_SCALE_NAMES[k], k == scaleIdx, true)) { scaleIdx = k; saveUiState(); applySize(); } x += w + 5; }
    // export (web: Export loop / Export kit)
    const float ey = c.y + 150; const bool busy = expBusy.load();
    h2(c.x, ey + 12, "EXPORT", "24-bit WAV + MIDI, .zip");
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, ey + 41, "LENGTH", th.print); }
    x = c.x + 70;
    static const char* const LL[4] = { "1 LOOP", "2 LOOPS", "4 LOOPS", "8 LOOPS" };
    for (int k = 0; k < 4; k++) { const float w = chipW(LL[k], true); if (chip(ID_EXLOOPS + k, R(x, ey + 27, w, 22), LL[k], k == expLoopsIdx, true, busy)) expLoopsIdx = k; x += w + 5; }
    check(ID_EXSTEMS, R(x + 12, ey + 27, 140, 22), "Stems per track", expStems);
    if (btn(ID_EXLOOP, R(c.x, ey + 60, 170, 32), busy ? "EXPORTING\xe2\x80\xa6" : "EXPORT LOOP", !busy) && !busy) openFile(3);
    if (btn(ID_EXKIT, R(c.x + 178, ey + 60, 170, 32), "EXPORT KIT") && !busy) openFile(4);
    std::string st; { std::lock_guard<std::mutex> g(expMtx); st = expStatus; }
    if (D()) {
      if (!st.empty()) { font(fPrint, 12, 1.2f); txt(c.x, ey + 113, st, busy ? th.led : th.printHi); }
      para(c.x, ey + (st.empty() ? 102 : 122), 390, "Loop: seamless WAV of the current pattern (the tails wrap to the start), stems, MIDI (C1 = track 1) and the project. Kit: the eight sounds as dry one-shots.", th.print, 11.5f);
    }
    const float rx = c.x + 430;
    if (standalone) {
      h2(rx, c.y + 12, "STANDALONE");
      para(rx, c.y + 28, c.w - 430,
        "Audio output, sample rate, buffer and MIDI inputs: LAB > AUDIO. PLAY runs the clock at the pattern tempo.\n"
        "Computer keyboard and MIDI controller: LAB > CONTROL. Every panel button can have a key and MIDI notes / CCs; knobs and faders "
        "learn CCs (absolute or relative encoders). Default keys, by position (AZERTY works the same): Space play/stop \xc2\xb7 hold 1\xe2\x80\x93" "8 and Q\xe2\x80\x93I = steps "
        "(turn a knob while holding to lock) \xc2\xb7 Z X C V B N M , = track pads \xc2\xb7 Shift = FUNC \xc2\xb7 A = Control All \xc2\xb7 F = fill \xc2\xb7 arrows = page / track \xc2\xb7 "
        "Esc = release held steps \xc2\xb7 L = LAB \xc2\xb7 Ctrl+Z = undo. Default MIDI: C1\xe2\x80\x93G1 tracks, C0\xe2\x80\x93G0 patterns.", th.print, 12.5f);
    } else {
      h2(rx, c.y + 12, "OUTPUTS", "track on its own output pair in Live");
      { float x = rx; for (int t = 0; t < NT; t++) { const std::string l = "T" + std::to_string(t + 1); if (chip(ID_OUTS + t, R(x, c.y + 24, 40, 22), l, S().outs[t] != 0, true)) { pushUndo(); S().outs[t] = S().outs[t] ? 0 : 1; core->sendOuts(); toast(S().outs[t] ? "TRACK " + std::to_string(t + 1) + " ON ITS OWN OUTPUT" : "TRACK " + std::to_string(t + 1) + " IN THE MAIN MIX"); } x += 45; } }
      para(rx, c.y + 52, c.w - 430, "A lit track leaves the main mix and plays on its own output (Track n) once Live uses it: in a Live audio track, Audio From = HARDER, then Track n. Its delay / reverb sends still go to the FX on the main output; it skips the master glue, drive and limiter.", th.print, 11.5f);
      h2(rx, c.y + 124, "IN LIVE");
      para(rx, c.y + 140, c.w - 430,
        "Transport: the sequencer follows Live's play, tempo and position (bar-accurate, also when Live loops or jumps). "
        "With Live stopped, PLAY runs the internal clock at the pattern tempo.\n"
        "MIDI in: C1\xe2\x80\x93G1 (36\xe2\x80\x93" "43) trigger tracks 1\xe2\x80\x93" "8 with velocity. C0\xe2\x80\x93G0 (24\xe2\x80\x93" "31) cue patterns P1\xe2\x80\x93P8 for the next pattern end.\n"
        "Automation: each track exposes Src 1\xe2\x80\x93" "8 and its main filter, amp and send controls; the master FX, mutes, fill and pattern select are automatable too. "
        "Parameter locks stay inside the patterns. Export renders at Live's tempo when Live is playing.", th.print, 11.5f);
    }
  }
  // a full-width selectable row (device names are long and mixed case)
  bool rowBtn(int id, R r, const std::string& lab, bool sel) {
    const bool c = clicked(id, r);
    if (D()) {
      rrect(r, 6, sel ? A(th.led, .10f) : th.cap); rstroke(r, 6, sel ? th.led : th.line);
      font(fPrint, 12.5f, 0.2f); txt(r.x + 10, r.cy() + 4.3f, fit(lab, r.w - 20), sel ? th.led : th.printHi);
    }
    return c;
  }
  void labAudio(R c) {
    if (!audioListed) refreshAudioLists();
    const bool open = isAudioDeviceOpen(); const char* api = getAudioBackendName();
    h2(c.x, c.y + 12, "AUDIO OUTPUT", api && *api ? api : "");
    // devices: system default first, then every output device
    std::vector<std::string> rows; rows.push_back(""); for (auto& d : audioDevs) rows.push_back(d);
    const int vis = 6; const R lr(c.x, c.y + 26, 440, vis * 28 - 4);
    const int maxScroll = std::max(0, (int)rows.size() - vis);
    if (scrolled(lr)) devScroll = (int)clampd(devScroll + (wheel > 0 ? -1 : 1), 0, maxScroll);
    if (devScroll > maxScroll) devScroll = maxScroll;
    for (int k = devScroll; k < (int)rows.size() && k < devScroll + vis; k++) {
      const R r(lr.x, lr.y + (k - devScroll) * 28, lr.w - 10, 24);
      const std::string lab = rows[k].empty() ? "System default" + (currentDev.size() && chosenDev.empty() ? "  (" + currentDev + ")" : std::string()) : rows[k];
      if (rowBtn(k == 0 ? ID_AUDDEF : ID_AUDDEV + std::min(k, 69), r, lab, rows[k] == chosenDev)) applyAudio(rows[k], 0, 0);
    }
    if (D() && maxScroll > 0) { const float th_ = lr.h * vis / (vis + maxScroll); rrect(R(lr.r() - 4, lr.y, 3, lr.h), 1.5f, th.range); rrect(R(lr.r() - 4, lr.y + (lr.h - th_) * devScroll / maxScroll, 3, th_), 1.5f, th.print); }
    // sample rate / buffer
    const float sy = c.y + 26 + vis * 28 + 6;
    if (D()) { font(fPrint, 11, 1.5f); txt(c.x, sy + 15, "SAMPLE RATE", th.print); txt(c.x, sy + 45, "BUFFER", th.print); }
    static const uint SRS[4] = { 44100, 48000, 88200, 96000 }; static const uint BUFS[6] = { 64, 128, 256, 512, 1024, 2048 };
    float x = c.x + 100; const uint csr = audioRate(), cbuf = audioBuffer();
    const bool fixedRate = api && std::strstr(api, "WASAPI");   // shared mode: Windows mixes at the device rate
    if (fixedRate) { if (D()) { char b[160]; std::snprintf(b, sizeof b, "%u Hz \xc2\xb7 the device's rate, set in Windows: Sound settings > device > Advanced", csr); font(fPrint, 12, 0.3f); txt(x, sy + 16, b, th.printHi); } }
    else for (int k = 0; k < 4; k++) { const std::string l = std::to_string(SRS[k]); const float w = chipW(l, true); if (chip(ID_SR + k, R(x, sy + 1, w, 22), l, open && csr == SRS[k], true)) applyAudio(chosenDev, SRS[k], 0); x += w + 5; }
    x = c.x + 100;
    for (int k = 0; k < 6; k++) { const std::string l = std::to_string(BUFS[k]); const float w = chipW(l, true); if (chip(ID_BUF + k, R(x, sy + 31, w, 22), l, open && cbuf == BUFS[k], true)) applyAudio(chosenDev, 0, BUFS[k]); x += w + 5; }
    if (D()) {
      char b[160];
      if (open) std::snprintf(b, sizeof b, "%u Hz \xc2\xb7 %u samples = %.1f ms per buffer \xc2\xb7 DSP load %.1f%%", csr, cbuf, csr ? cbuf * 1000.0 / csr : 0.0, core->cpuPermille.load() / 10.0);
      else std::snprintf(b, sizeof b, "No output is open. Pick a device above.");
      font(fPrint, 12, 0.3f); txt(c.x, sy + 76, b, open ? th.printHi : th.rec);
      if (!audioMsg.empty()) { font(fPrint, 11, 1.2f); txt(c.x, sy + 94, audioMsg, th.rec); }
    }
    // MIDI in / out and clock
    const float rx = c.x + 470, rw = c.w - 470;
    h2(rx, c.y + 12, "MIDI INPUT", "C1\xe2\x80\x93G1 tracks \xc2\xb7 C0\xe2\x80\x93G0 patterns");
    if (midiNames.empty() && D()) { font(fPrint, 12, 0.3f); txt(rx, c.y + 42, "No MIDI input found.", th.print); }
    for (size_t k = 0; k < midiNames.size() && k < 4; k++) { bool on = midiOpen[k]; if (check(ID_MIDI + (int)k, R(rx, c.y + 30 + k * 23, rw, 22), midiNames[k], on)) toggleMidi(k); }
    h2(rx, c.y + 136, "MIDI OUTPUT", "MIDI clock");
    if (midiOutNames.empty() && D()) { font(fPrint, 12, 0.3f); txt(rx, c.y + 166, "No MIDI output found.", th.print); }
    for (size_t k = 0; k < midiOutNames.size() && k < 3; k++) { bool on = midiOutOpen[k]; if (check(ID_MIDIOUT + (int)k, R(rx, c.y + 154 + k * 23, rw, 22), midiOutNames[k], on)) toggleMidiOut(k); }
    { bool a = core->clockIn.load(); if (check(ID_CLOCK, R(rx, c.y + 230, 210, 22), "Follow MIDI clock in", a)) { core->clockIn.store(a); saveSettings(); toast(a ? "FOLLOWS MIDI CLOCK (START / STOP / POSITION)" : "INTERNAL CLOCK"); } }
    { bool a = core->clockOut.load(); if (check(ID_CLOCK + 1, R(rx + 220, c.y + 230, 190, 22), "Send MIDI clock", a)) { core->clockOut.store(a); saveSettings(); toast(a ? "SENDS CLOCK TO THE TICKED OUTPUTS" : "CLOCK OUT OFF"); } }
    if (D()) {
      char b[160]; const double eb = core->extBpm.load();
      if (core->clockIn.load()) std::snprintf(b, sizeof b, "CLOCK IN: %s", eb > 0 ? (toFixed(eb, 1) + " BPM" + (core->extClock.load() ? " \xc2\xb7 RUNNING" : " \xc2\xb7 STOPPED")).c_str() : "NO CLOCK");
      else std::snprintf(b, sizeof b, "CLOCK OUT OFFSET");
      font(fPrint, 10.5f, 1.0f); txt(rx, c.y + 271, b, th.print);
    }
    if (!core->clockIn.load()) {
      const int off = core->clockOffsetMs.load();
      if (chip(ID_CLOCK + 2, R(rx + 130, c.y + 258, 22, 20), "\xe2\x88\x92", false, true, off <= -50)) { core->clockOffsetMs.store(off - 1); saveSettings(); }
      if (D()) { font(fNum, 11); txt(rx + 186, c.y + 272, (off > 0 ? "+" : "") + std::to_string(off) + " MS", th.printHi, ALIGN_CENTER | ALIGN_BASELINE); }
      if (chip(ID_CLOCK + 3, R(rx + 220, c.y + 258, 22, 20), "+", false, true, off >= 100)) { core->clockOffsetMs.store(off + 1); saveSettings(); }
    }
    if (btn(ID_RESCAN, R(rx, c.y + 290, 120, 28), "RESCAN")) { refreshAudioLists(); toast("DEVICES RESCANNED"); }
#if defined(__APPLE__)
    para(rx + 130, c.y + 288, rw - 130, "macOS shares MIDI ports between programs; to other apps on this Mac use the IAC Driver (Audio MIDI Setup). Offset: later (+) if the other device runs ahead.", th.print, 10.5f);
#elif defined(_WIN32)
    para(rx + 130, c.y + 288, rw - 130, "Windows lets one program at a time use a MIDI port: untick it here to use it elsewhere. Offset: later (+) if the other device runs ahead.", th.print, 10.5f);
#else
    para(rx + 130, c.y + 288, rw - 130, "Untick a port here to leave it to other programs. Offset: later (+) if the other device runs ahead.", th.print, 10.5f);
#endif
  }

  // ================= DPF events =================
  void onNanoDisplay() override {
    if (!core) return;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    const float s = (float)totalScale();
    save(); scale(s, s); ph = PH_DRAW; consumed = false; frame(); restore();
    if (stWantPixels) { stWantPixels = false; NanoVG::endFrame(); selfTestReadPixels(); NanoVG::beginFrame(getWidth(), getHeight()); }   // self-test only
  }
  void runPhase(Phase p, float x, float y, uint m) {
    const float s = (float)totalScale(); ph = p; mx = x / s; my = y / s; mods = m; consumed = false;
    shiftDown = (m & kModifierShift) != 0;
    frame(); ph = PH_DRAW; needRepaint = true;
  }
  bool onMouse(const MouseEvent& ev) override {
    if (!core || ev.button != 1) return false;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    if (ev.press) { pressedId = ID_NONE; drag = Drag(); runPhase(PH_PRESS, ev.pos.getX(), ev.pos.getY(), ev.mod); }
    else {
      runPhase(PH_RELEASE, ev.pos.getX(), ev.pos.getY(), ev.mod);
      if (heldStep >= 0 && pressedId >= ID_TRIG && pressedId < ID_TRIG + 16) heldStep = -1;   // released outside the trig
      if (perfHeld >= 0) perfUp(perfHeld);
      endHostEdits(); pressedId = ID_NONE; drag = Drag();
    }
    repaint(); return true;
  }
  bool onMotion(const MotionEvent& ev) override {
    if (!core || drag.id == ID_NONE) return false;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    const float s = (float)totalScale(); const float y = ev.pos.getY() / s, x = ev.pos.getX() / s;
    const bool fine = (ev.mod & kModifierShift) || funcLatch;
    if (drag.kind == 1) {
      const PDef& d = defOf(drag.target); if (!d.valid()) return true;
      const double dy = drag.y - y; drag.y = y; const double range = d.max - d.min;
      drag.acc = clampd(drag.acc + dy * range / (fine ? 900 : 200) * (range < 20 ? 1.6 : 1), d.min, d.max);
      const double v = stepRound(drag.acc, d); if (v != getVal(drag.target)) setVal(drag.target, v, true);
    } else if (drag.kind == 2) {
      const double dy = drag.y - y; drag.y = y;
      drag.acc = drag.acc + dy * (fine ? numFine(drag.id) : numCoarse(drag.id)) / 4;
      const double fs = numFine(drag.id); const double v = jsround(drag.acc / fs) * fs; if (std::fabs(v - numGet(drag.id)) > 1e-9) numSet(drag.id, v);
    } else if (drag.kind == 4) {
      auto it = faderRects.find(drag.id);
      if (it != faderRects.end()) {
        const R& fr = it->second.r; const double cur = drag.id == ID_OFADER ? outDb : volToDb(S().tracks[tr].p[21]);
        if (fine) { const double p = iec(std::max(-70.0, std::min(cur, it->second.maxDb))) / iec(it->second.maxDb) + (x - drag.y) / fr.w * 0.25;
                    applyFader(drag.id, iecInv(clampd(p, 0, 1) * iec(it->second.maxDb))); }
        else applyFader(drag.id, faderDbAt(fr, x, it->second.maxDb));
        drag.y = x;
      }
    } else if (drag.kind == 5) {   // library item being dragged
      dragX = x; dragY = y; if (std::hypot(x - dragX0, y - dragY0) > 5) dragMoved = true;
    } else if (drag.kind == 3) {
      auto it = sliderRects.find(drag.id); int* var = sliderVar(drag.id);
      if (it != sliderRects.end() && var) { mx = x; sliderSet(drag.id, it->second.r, it->second.mn, it->second.mx, *var); }
    }
    needRepaint = true; repaint(); return true;
  }
  bool onScroll(const ScrollEvent& ev) override {
    if (!core) return false;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    wheel = (float)ev.delta.getY(); if (wheel == 0) return false;
    runPhase(PH_SCROLL, ev.pos.getX(), ev.pos.getY(), ev.mod); repaint(); return true;
  }

  // ================= standalone keyboard (web shortcuts, by physical key position) =================
  // Windows gives the set-1 scan code, X11 the evdev code + 8 (for these keys the evdev code equals the scan code),
  // macOS its virtual key code (kVK_*, a position too), translated to the set-1 code (macKeyToScancode, FileSys).
  static int physKeyFromOs(int code) {
#ifdef _WIN32
    return code;
#elif defined(__APPLE__)
    return macKeyToScancode(code);
#else
    return code - 8;
#endif
  }
  static int physKey(const KeyboardEvent& ev) { return physKeyFromOs((int)ev.keycode); }
  enum : int { SC_ESC = 0x01, SC_1 = 0x02, SC_8 = 0x09, SC_Q = 0x10, SC_I = 0x17, SC_A = 0x1E, SC_F = 0x21, SC_L = 0x26, SC_Z = 0x2C, SC_COMMA = 0x33, SC_SPACE = 0x39 };
  // one-line name field: works in the plugin too (keys arrive while the field is open)
  bool textKey(const KeyboardEvent& ev) {
    if (!ev.press) return true;
    const double now = nowMs();   // in a plugin the same key can arrive twice (host forwarding and the window itself)
    if ((int)ev.key == lastKeyCode && now - lastKeyT < 15) return true;
    lastKeyCode = (int)ev.key; lastKeyT = now;
    if (ev.key == kKeyEscape) te = TextEdit();
    else if (ev.key == kKeyEnter || ev.key == '\n') commitEdit();
    else if (ev.key == kKeyBackspace) { std::string& t = te.text; while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); if (!t.empty()) t.pop_back(); }
    needRepaint = true; repaint(); return true;
  }
  bool onCharacterInput(const CharacterInputEvent& ev) override {
    if (!core || te.act == TE_NONE) return false;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    std::string s;
    if (ev.string[0]) { s = ev.string; sawPuglText = true; }
    else { if (sawPuglText) return true;   // the window delivers text itself: ignore the host's ASCII copy
           if (ev.character >= 0x20 && ev.character < 0x7f) s = std::string(1, (char)ev.character); }
    if (s.empty() || (unsigned char)s[0] < 0x20 || s[0] == 0x7f) return true;
    if (te.text.size() + s.size() <= 120) te.text += s;
    needRepaint = true; repaint(); return true;
  }
  bool onKeyboard(const KeyboardEvent& ev) override {
    if (core && te.act != TE_NONE) { std::lock_guard<std::recursive_mutex> g(core->mtx); return textKey(ev); }
    if (!core || !standalone) return false;   // in a host the keys belong to the host
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    if (ev.key == kKeyShiftL || ev.key == kKeyShiftR) { shiftDown = ev.press; needRepaint = true; repaint(); return true; }
    if (ev.key == kKeyControlL || ev.key == kKeyControlR || ev.key == kKeyAltL || ev.key == kKeyAltR || ev.key == kKeySuperL || ev.key == kKeySuperR) return false;
    // Ctrl+Z by the letter printed on the key (on AZERTY that is not the physical Z position)
    if (ev.mod & (kModifierControl | kModifierSuper)) { if (ev.press && (ev.key == 'z' || ev.key == 'Z' || ev.key == 0x1A)) { undo(); needRepaint = true; repaint(); } return true; }
    if (ev.mod & kModifierAlt) return false;
    const int key = ev.key >= 0xE000 ? (int)ev.key : physKey(ev);   // position, or the DGL code for arrows, F-keys...
    if (keyLearn.k != AC_NONE) { if (ev.press) learnKey(key); needRepaint = true; repaint(); return true; }
    if (ev.press) { if (keysDown.count(key)) return true; keysDown.insert(key); }   // auto-repeat
    else keysDown.erase(key);
    const Action a = cmap.actionForKey(key);
    if (a.k == AC_NONE) return false;
    doButton(a, ev.press, key, 110);
    needRepaint = true; repaint(); return true;
  }
  // window lost focus: release everything held from the keyboard (web: blur); controller holds stay
  void uiFocus(bool focus, CrossingMode) override {
    if (focus || !core || !standalone) return;
    std::lock_guard<std::recursive_mutex> g(core->mtx);
    shiftDown = false; keysDown.clear();
    for (auto it = keyHeld.begin(); it != keyHeld.end();) { if (it->first < 0x1000000) it = keyHeld.erase(it); else ++it; }
    for (auto it = keyPerf.begin(); it != keyPerf.end();) { if (it->first < 0x1000000) { perfUp(it->second); it = keyPerf.erase(it); } else ++it; }
    for (std::set<int>* st : { &funcSrc, &allSrc }) for (auto it = st->begin(); it != st->end();) { if (*it < 0x1000000) it = st->erase(it); else ++it; }
    if (fillHeld && !fillLatch) setFill(false);
    needRepaint = true;
  }
#ifdef HARDER_TEST_MIDI_INJECT
  // test builds: Documents/HARDER/midi-inject.txt, one message per line in hex ("90 24 64"), goes to the audio thread
  // like MIDI input; the file is removed once read
  void injectTestMidi() {
    const std::string p = harderConfigDir() + "midi-inject.txt"; std::string txt;
    if (!readTextFile(p, txt)) return;
    removeFile(p);
    std::istringstream in(txt); std::string line;
    while (std::getline(in, line)) { unsigned a = 0, b = 0, c = 0; if (std::sscanf(line.c_str(), "%x %x %x", &a, &b, &c) >= 2) { Core::MidiMsg m; m.st = (uint8_t)a; m.d1 = (uint8_t)b; m.d2 = (uint8_t)c; core->injectPush(m); } }
  }
#endif

  DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarderUI)
};

UI* createUI() { return new HarderUI(); }

END_NAMESPACE_DISTRHO
