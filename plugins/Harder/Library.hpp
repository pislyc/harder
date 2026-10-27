// The shared user library: Documents/HARDER/Library on Windows (~/.config/HARDER/Library on Linux), used by the
// standalone, the plugin and any other program that writes into it. Folder = category. See docs/LIBRARY.md.
//
//   Library/Sounds/<category>/<name>.json   HARDER sound (any machine; a SAMPLE sound names its WAV or embeds it)
//   Library/Sounds/<category>/<name>.wav    plain WAV: loads as a SAMPLE sound with default settings
//   Library/Sounds/<name>.*                 files directly in Sounds/ are in INBOX
//   Library/Grooves/<category>/<name>.json  HARDER groove (one pattern)
//   Library/Trash/                          what HARDER deletes goes here
// A WAV with the same stem as a sound .json next to it belongs to that sound and is not listed on its own.
// Main thread only.
#pragma once
#include "Model.hpp"
#include "FileSys.hpp"
#include <algorithm>
#include <set>

namespace hd {

std::string harderConfigDir();   // HarderPlugin.cpp

enum LibKind : uint8_t { LK_SOUND = 0, LK_WAV = 1, LK_GROOVE = 2, LK_KIT = 3 };
struct LibItem { std::string name, path, cat; int kind = LK_SOUND; int64_t mtime = 0; uint64_t size = 0; };
struct LibIndex {
  std::vector<std::string> cats;   // sorted, case-insensitive
  std::vector<LibItem> items;      // sorted by category, then name
  std::string sig;                 // changes when anything on disk changed
};

inline std::string libRoot() { const std::string d = harderConfigDir(); return d.empty() ? d : joinPath(d, "Library"); }
inline std::string libSoundsDir() { return joinPath(libRoot(), "Sounds"); }
inline std::string libGroovesDir() { return joinPath(libRoot(), "Grooves"); }
inline std::string libKitsDir() { return joinPath(libRoot(), "Kits"); }
inline std::string libTrashDir() { return joinPath(libRoot(), "Trash"); }
// a kit's samples live in a folder next to it: Kits/<category>/<name>.json + <name>.samples/
inline std::string kitSamplesDir(const std::string& kitJson) { return joinPath(dirName(kitJson), stemOf(fileName(kitJson)) + ".samples"); }
inline std::string lowerAscii(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }
inline bool isSamplesDir(const std::string& n) { return n.size() > 8 && lowerAscii(n.substr(n.size() - 8)) == ".samples"; }

static const char* const LIB_README =
  "HARDER library\r\n"
  "==============\r\n"
  "Shared by the HARDER standalone program, the HARDER plugin and any program that writes here.\r\n"
  "Each folder inside Sounds/ and Grooves/ is a category; files directly in Sounds/ show up as INBOX.\r\n\r\n"
  "Sounds/<category>/<name>.wav   a WAV file plays on the SAMPLE machine (mono mix, any rate, PCM 8-32 bit or float, up to 60 s)\r\n"
  "Sounds/<category>/<name>.json  a HARDER sound: {\"format\":\"harder-sound\",\"version\":1,\"name\":...,\"machine\":\"SUB\"|...|\"SMP\",\r\n"
  "                               \"p\":[56 parameter values], \"sample\":{\"file\":\"<name>.wav\"}}  (sample: SAMPLE machine only)\r\n"
  "Grooves/<category>/<name>.json {\"format\":\"harder-groove\",\"version\":1,\"name\":...,\"pattern\":{...}}\r\n"
  "Kits/<category>/<name>.json    {\"format\":\"harder-kit\",\"version\":1,\"name\":...,\"tracks\":[8 sounds],...}; its samples in <name>.samples/\r\n\r\n"
  "Write a file as <name>.tmp and rename it when it is complete: HARDER lists it once its size stops changing.\r\n"
  "Full description: docs/LIBRARY.md in the source (https://github.com/pislyc/harder).\r\n";

inline bool ensureLibrary() {
  const std::string r = libRoot(); if (r.empty()) return false;
  const bool ok = makeDir(r) && makeDir(libSoundsDir()) && makeDir(libGroovesDir()) && makeDir(libKitsDir());
  if (ok && !pathExists(joinPath(r, "README.txt"))) writeFileAtomic(joinPath(r, "README.txt"), std::string(LIB_README));
  return ok;
}

// file-name-safe version of a user name (keeps UTF-8, drops characters Windows refuses, trims dots and spaces)
inline std::string safeFileName(const std::string& in) {
  std::string o;
  for (unsigned char c : in) { if (c < 0x20 || std::strchr("\\/:*?\"<>|", c)) continue; o += (char)c; }
  while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
  size_t a = 0; while (a < o.size() && (o[a] == ' ' || o[a] == '.')) a++; o = o.substr(a);
  if (o.size() > 80) { size_t n = 80; while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80) n--; o = o.substr(0, n); }   // whole UTF-8 characters
  static const char* const RES[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3" };
  for (const char* r : RES) if (lowerAscii(o) == r) o += "_";
  return o.empty() ? std::string("untitled") : o;
}
// a path in dir for name + ext that does not exist yet ("name", "name 2", ...); skip: a path that may be reused
inline std::string uniquePath(const std::string& dir, const std::string& name, const std::string& ext, const std::string& skip = "") {
  const std::string base = safeFileName(name);
  for (int k = 1; k < 1000; k++) {
    const std::string p = joinPath(dir, (k == 1 ? base : base + " " + std::to_string(k)) + ext);
    if (p == skip || (!pathExists(p) && !(ext == ".json" && pathExists(joinPath(dir, stemOf(fileName(p)) + ".wav"))))) return p;
  }
  return joinPath(dir, base + " " + std::to_string((int)(std::rand() % 100000)) + ext);
}

inline bool catLess(const std::string& a, const std::string& b) { return lowerAscii(a) < lowerAscii(b); }

// sounds: .json and .wav; grooves and kits: .json. Category "" = INBOX (files directly in the root).
// A kit's <name>.samples folder is neither a category nor listed.
inline void scanLibrary(const std::string& root, bool sounds, LibIndex& out, bool kits = false) {
  out.cats.clear(); out.items.clear(); out.sig.clear();
  std::vector<DirEnt> top; if (!listDir(root, top)) return;
  auto addFiles = [&](const std::string& dir, const std::string& cat, const std::vector<DirEnt>& ents) {
    std::set<std::string> jsonStems;
    if (sounds) for (const DirEnt& e : ents) if (!e.dir && extOf(e.name) == ".json") jsonStems.insert(lowerAscii(stemOf(e.name)));
    for (const DirEnt& e : ents) {
      if (e.dir) continue; const std::string ext = extOf(e.name);
      int kind;
      if (ext == ".json") kind = sounds ? LK_SOUND : kits ? LK_KIT : LK_GROOVE;
      else if (sounds && ext == ".wav") { if (jsonStems.count(lowerAscii(stemOf(e.name)))) continue; kind = LK_WAV; }
      else continue;
      LibItem it; it.name = stemOf(e.name); it.path = joinPath(dir, e.name); it.cat = cat; it.kind = kind; it.mtime = e.mtime; it.size = e.size;
      if (kind == LK_SOUND) for (const DirEnt& w : ents) if (!w.dir && extOf(w.name) == ".wav" && lowerAscii(stemOf(w.name)) == lowerAscii(it.name)) { it.mtime = std::max(it.mtime, w.mtime); it.size += w.size; }   // the sound's own WAV counts (a re-rendered sample is a change)
      out.items.push_back(it);
    }
  };
  addFiles(root, "", top);
  for (const DirEnt& e : top) {
    if (!e.dir || isSamplesDir(e.name)) continue;
    out.cats.push_back(e.name);
    std::vector<DirEnt> sub; if (listDir(joinPath(root, e.name), sub)) addFiles(joinPath(root, e.name), e.name, sub);
  }
  std::sort(out.cats.begin(), out.cats.end(), catLess);
  std::stable_sort(out.items.begin(), out.items.end(), [](const LibItem& a, const LibItem& b) { if (a.cat != b.cat) return catLess(a.cat, b.cat); return catLess(a.name, b.name); });
  for (const std::string& c : out.cats) { out.sig += c; out.sig += '\x1f'; }
  for (const LibItem& it : out.items) { out.sig += it.path; out.sig += '\x1f'; out.sig += std::to_string(it.mtime); out.sig += '\x1f'; out.sig += std::to_string(it.size); out.sig += '\x1e'; }
}

// ---------- sounds ----------
struct SoundDef { std::string name; int m = M_SUB; double p[NP]; SampleRef smp; };

// files up to 128 MB are read whole; longer ones only up to the header and 60 s of audio (a long recording is not loaded whole)
inline bool loadWavFile(const std::string& path, SampleRef& out, std::string& err, bool* truncated = nullptr) {
  try {
    std::string data; uint64_t total = 0; bool cut = false;
    if (!readFilePrefix(path, 4u << 20, data, &total)) { err = "could not read the file"; return false; }
    if (total > data.size() && total <= (128ull << 20)) {   // whole file: cue points (slices) usually follow the audio
      if (!readFilePrefix(path, total, data, &total)) { err = "could not read the file"; return false; }
    } else if (total > data.size()) {
      uint64_t need = wavBytesNeeded((const uint8_t*)data.data(), data.size(), &cut);
      if (!need) need = total <= (1ull << 30) ? total : 0;   // the audio starts after 4 MB of other chunks: read it all, up to 1 GB
      if (!need) { err = "file too large"; return false; }
      if (need > data.size() && !readFilePrefix(path, need, data, &total)) { err = "could not read the file"; return false; }
    }
    out = decodeWav(stemOf(fileName(path)), (const uint8_t*)data.data(), data.size(), &err, truncated);
    if (out && truncated && cut) *truncated = true;
    return out != nullptr;
  } catch (const std::bad_alloc&) { err = "not enough memory for this file"; out.reset(); return false; }
}
inline void wavSound(const SampleRef& s, SoundDef& d) { d.name = s->name; d.m = M_SMP; defaultTrackParams(M_SMP, d.p); d.smp = s; }

inline bool soundFromJson(const JVal& j, const std::string& dir, SoundDef& d, SampleCache* cache, std::string& err) {
  if (!j.isObj() || j["format"].str() != "harder-sound") { err = "not a HARDER sound file"; return false; }
  if (j["version"].num(1) > 1) { err = "made by a newer HARDER"; return false; }
  const int mid = machineIndex(j["machine"].str().c_str()); if (mid < 0) { err = "unknown machine " + j["machine"].str(); return false; }
  d.m = mid; paramsFromJson(j["p"], mid, d.p); d.name = j["name"].isStr() ? j["name"].str() : std::string();
  for (int i = 0; i < NP; i++) { const PDef& pd = T().track(mid, i); if (pd.valid()) d.p[i] = clampd(d.p[i], pd.min, pd.max); }
  d.smp.reset();
  if (mid == M_SMP) {
    const JVal& s = j["sample"];
    if (s["file"].isStr()) {
      const std::string f = s["file"].str();
      if (f.find("..") != std::string::npos || f.find_first_of("/\\:") != std::string::npos) { err = "the sample must be next to the sound file"; return false; }
      SampleRef r; if (!loadWavFile(joinPath(dir, f), r, err)) { err = f + ": " + err; return false; }
      d.smp = cache ? cache->add(r) : r;
    } else if (s["data"].isStr()) { d.smp = sampleFromJson(s, cache, &err); if (!d.smp) return false; }
    else { err = "SAMPLE sound without a sample"; return false; }
  }
  return true;
}
inline bool loadSoundItem(const LibItem& it, SoundDef& d, SampleCache* cache, std::string& err, bool* truncated = nullptr) {
  if (it.kind == LK_WAV) { SampleRef s; if (!loadWavFile(it.path, s, err, truncated)) return false; if (cache) s = cache->add(s); wavSound(s, d); d.name = it.name; return true; }
  std::string txt; JVal j; if (!readTextFile(it.path, txt) || !parseJson(txt, j)) { err = "not a HARDER sound file"; return false; }
  if (!soundFromJson(j, dirName(it.path), d, cache, err)) return false;
  if (d.name.empty()) d.name = it.name;
  return true;
}
// writes <dir>/<name>.json (+ <name>.wav for a SAMPLE sound); returns the .json path
inline std::string saveSound(const std::string& dir, const std::string& name, int m, const double* p, const SampleRef& smp, std::string& err) {
  if (!makeDir(dir)) { err = "could not create the folder"; return ""; }
  const std::string jp = uniquePath(dir, name, ".json"), stem = stemOf(fileName(jp));
  JVal o = JVal::Obj(); o.set("format", JVal::Str("harder-sound")); o.set("version", JVal::Num(1)); o.set("name", JVal::Str(name));
  o.set("machine", JVal::Str(MACH_IDS[m]));
  JVal a = JVal::Arr(); for (int i = 0; i < NP; i++) a.push(JVal::Num(p[i])); o.set("p", std::move(a));
  if (m == M_SMP) {
    if (!smp) { err = "no sample on this track"; return ""; }
    const std::vector<uint8_t> w = encodeWav(*smp); const std::string wp = joinPath(dir, stem + ".wav");
    if (pathExists(wp) || !writeFileAtomic(wp, w.data(), w.size())) { err = "could not write the WAV file"; return ""; }
    JVal s = JVal::Obj(); s.set("file", JVal::Str(stem + ".wav")); s.set("id", JVal::Str(smp->id)); o.set("sample", std::move(s));
  }
  std::string out; writeJson(out, o);
  if (!writeFileAtomic(jp, out)) { err = "could not write the file"; return ""; }
  return jp;
}

// ---------- grooves ----------
inline bool loadGrooveItem(const LibItem& it, Pattern& P, std::string& name, std::string& err) {
  std::string txt; JVal j;
  if (!readTextFile(it.path, txt) || !parseJson(txt, j) || !j.isObj() || j["format"].str() != "harder-groove" || !j["pattern"].isObj()) { err = "not a HARDER groove file"; return false; }
  if (j["version"].num(1) > 1) { err = "made by a newer HARDER"; return false; }
  patternFromJson(j["pattern"], P); name = j["name"].isStr() ? j["name"].str() : it.name; return true;
}
inline std::string saveGroove(const std::string& dir, const std::string& name, const Pattern& P, std::string& err) {
  if (!makeDir(dir)) { err = "could not create the folder"; return ""; }
  const std::string jp = uniquePath(dir, name, ".json");
  JVal o = JVal::Obj(); o.set("format", JVal::Str("harder-groove")); o.set("version", JVal::Num(1)); o.set("name", JVal::Str(name)); o.set("pattern", patternToJson(P));
  std::string out; writeJson(out, o);
  if (!writeFileAtomic(jp, out)) { err = "could not write the file"; return ""; }
  return jp;
}

// ---------- kits: the eight sounds (and optionally the master FX) ----------
struct UserKit { std::string name; SoundDef t[NT]; int roles[NT]; bool hasMaster = false; double master[NM]; };
inline bool loadKitItem(const LibItem& it, UserKit& k, SampleCache* cache, std::string& err) {
  std::string txt; JVal j;
  if (!readTextFile(it.path, txt) || !parseJson(txt, j) || !j.isObj() || j["format"].str() != "harder-kit" || !j["tracks"].isArr()) { err = "not a HARDER kit file"; return false; }
  if (j["version"].num(1) > 1) { err = "made by a newer HARDER"; return false; }
  k.name = j["name"].isStr() ? j["name"].str() : it.name;
  const std::string dir = dirName(it.path), sdir = stemOf(fileName(it.path)) + ".samples";
  for (int t = 0; t < NT; t++) {
    const JVal& x = j["tracks"][t]; SoundDef& d = k.t[t];
    if (!x.isObj()) { d.m = M_SUB; defaultTrackParams(M_SUB, d.p); d.name.clear(); d.smp.reset(); continue; }
    JVal sx = x; sx.set("format", JVal::Str("harder-sound"));
    // a kit names its samples inside its own folder: "<kit>.samples/<file>.wav"
    std::string sub;
    if (x["sample"]["file"].isStr()) {
      const std::string f = x["sample"]["file"].str(); const size_t sl = f.find_first_of("/\\");
      if (sl == std::string::npos || f.substr(0, sl) != sdir || f.find("..") != std::string::npos || f.find_first_of("/\\:", sl + 1) != std::string::npos) { err = "track " + std::to_string(t + 1) + ": the sample must be in " + sdir; return false; }
      for (auto& kv : sx.o) if (kv.first == "sample") kv.second.set("file", JVal::Str(f.substr(sl + 1)));
      sub = joinPath(dir, sdir);
    }
    if (!soundFromJson(sx, sub.empty() ? dir : sub, d, cache, err)) { err = "track " + std::to_string(t + 1) + ": " + err; return false; }
  }
  for (int t = 0; t < NT; t++) { k.roles[t] = t; const int r = roleIndex(j["roles"][t].str().c_str()); if (r >= 0) k.roles[t] = r; }
  k.hasMaster = j["master"].isArr();
  defaultMaster(k.master); if (k.hasMaster) for (int i = 0; i < NM; i++) { const JVal& v = j["master"][i]; if (v.isNum() && std::isfinite(v.n)) { const PDef& pd = T().master[i]; k.master[i] = pd.valid() ? clampd(v.n, pd.min, pd.max) : 0; } }
  return true;
}
// writes <dir>/<name>.json and, for SAMPLE tracks, <name>.samples/<n> <sound>.wav; returns the .json path
inline std::string saveKit(const std::string& dir, const std::string& name, const Project& S, bool withMaster, std::string& err) {
  if (!makeDir(dir)) { err = "could not create the folder"; return ""; }
  std::string jp; for (int k = 1; k < 1000; k++) { jp = uniquePath(dir, k == 1 ? name : name + " " + std::to_string(k), ".json"); if (!pathExists(kitSamplesDir(jp))) break; }
  const std::string stem = stemOf(fileName(jp)), sdir = kitSamplesDir(jp);
  JVal o = JVal::Obj(); o.set("format", JVal::Str("harder-kit")); o.set("version", JVal::Num(1)); o.set("name", JVal::Str(name));
  JVal tr = JVal::Arr();
  for (int t = 0; t < NT; t++) {
    const TrackM& tk = S.tracks[t]; JVal x = JVal::Obj();
    x.set("name", JVal::Str(tk.pre.size() ? tk.pre : std::string(MACH_NAMES[tk.m]))); x.set("machine", JVal::Str(MACH_IDS[tk.m]));
    JVal a = JVal::Arr(); for (int i = 0; i < NP; i++) a.push(JVal::Num(tk.p[i])); x.set("p", std::move(a));
    if (tk.m == M_SMP) {
      const SampleRef& smp = S.smp[(int)clampd(jsround(tk.p[2]), 0, NSLOT - 1)];
      if (smp) {
        if (!makeDir(sdir)) { err = "could not create the samples folder"; return ""; }
        const std::string wf = safeFileName(std::to_string(t + 1) + " " + (tk.pre.size() ? tk.pre : smp->name)) + ".wav";
        const std::vector<uint8_t> w = encodeWav(*smp);
        if (!writeFileAtomic(joinPath(sdir, wf), w.data(), w.size())) { err = "could not write a WAV file"; return ""; }
        JVal sm = JVal::Obj(); sm.set("file", JVal::Str(stem + ".samples/" + wf)); sm.set("id", JVal::Str(smp->id)); x.set("sample", std::move(sm));
      } else { x.set("machine", JVal::Str("SUB")); JVal d = JVal::Arr(); double p[NP]; defaultTrackParams(M_SUB, p); for (int i = 0; i < NP; i++) d.push(JVal::Num(p[i])); x.set("p", std::move(d)); }
    }
    tr.push(std::move(x));
  }
  o.set("tracks", std::move(tr));
  JVal ro = JVal::Arr(); for (int t = 0; t < NT; t++) ro.push(JVal::Str(ROLES[S.roles[t]])); o.set("roles", std::move(ro));
  if (withMaster) { JVal m = JVal::Arr(); for (int i = 0; i < NM; i++) m.push(JVal::Num(S.master[i])); o.set("master", std::move(m)); }
  std::string out; writeJson(out, o);
  if (!writeFileAtomic(jp, out)) { err = "could not write the file"; return ""; }
  return jp;
}

// ---------- moving things around (deleting = moving to Library/Trash) ----------
// an item and the WAV that belongs to it
inline std::vector<std::string> itemFiles(const LibItem& it) {
  std::vector<std::string> f{ it.path };
  if (it.kind == LK_SOUND) { const std::string w = joinPath(dirName(it.path), stemOf(fileName(it.path)) + ".wav"); if (pathExists(w)) f.push_back(w); }
  return f;
}
// moves an item into dir, renaming on a clash (name 2, ...); returns the new path of the main file
inline std::string moveItem(const LibItem& it, const std::string& dir, std::string& err, const std::string& newName = "") {
  if (!makeDir(dir)) { err = "could not create the folder"; return ""; }
  const std::string ext = extOf(it.path), oldStem = stemOf(fileName(it.path));
  const std::string want = newName.empty() ? oldStem : newName;
  const bool sameDir = dirName(it.path) == dir;
  const std::string dst = uniquePath(dir, want, ext, sameDir && safeFileName(want) == oldStem ? it.path : "");
  if (dst == it.path) return dst;
  const std::string newStem = stemOf(fileName(dst));
  if (it.kind == LK_SOUND) {
    const std::string w = joinPath(dirName(it.path), oldStem + ".wav");
    if (pathExists(w)) {
      const std::string wd = joinPath(dir, newStem + ".wav");
      if (!movePath(w, wd)) { err = "could not move the WAV file"; return ""; }
      // the sound names its WAV: keep the reference right
      std::string txt; JVal j;
      if (readTextFile(it.path, txt) && parseJson(txt, j) && j.isObj() && j["sample"]["file"].isStr()) {
        for (auto& kv : j.o) if (kv.first == "sample") kv.second.set("file", JVal::Str(newStem + ".wav"));
        if (!newName.empty()) j.set("name", JVal::Str(newName));
        std::string out; writeJson(out, j); writeFileAtomic(it.path, out);
      }
    } else if (!newName.empty()) {
      std::string txt; JVal j;
      if (readTextFile(it.path, txt) && parseJson(txt, j) && j.isObj()) { j.set("name", JVal::Str(newName)); std::string out; writeJson(out, j); writeFileAtomic(it.path, out); }
    }
  } else if (it.kind == LK_KIT) {
    const std::string sd = kitSamplesDir(it.path), nsd = joinPath(dir, newStem + ".samples");
    std::string txt; JVal j; const bool ok = readTextFile(it.path, txt) && parseJson(txt, j) && j.isObj();
    if (pathExists(sd) && sd != nsd) {
      if (pathExists(nsd) || !movePath(sd, nsd)) { err = "could not move the kit's samples"; return ""; }
      if (ok) for (auto& kv : j.o) if (kv.first == "tracks") for (JVal& x : kv.second.a) for (auto& kx : x.o) if (kx.first == "sample") {
        const std::string f = kx.second["file"].str(); const size_t sl = f.find_first_of("/\\");
        if (sl != std::string::npos) kx.second.set("file", JVal::Str(newStem + ".samples/" + f.substr(sl + 1)));
      }
    }
    if (ok) { if (!newName.empty()) j.set("name", JVal::Str(newName)); std::string out; writeJson(out, j); writeFileAtomic(it.path, out); }
  } else if (it.kind == LK_GROOVE && !newName.empty()) {
    std::string txt; JVal j;
    if (readTextFile(it.path, txt) && parseJson(txt, j) && j.isObj()) { j.set("name", JVal::Str(newName)); std::string out; writeJson(out, j); writeFileAtomic(it.path, out); }
  }
  if (!movePath(it.path, dst)) { err = "could not move the file"; return ""; }
  return dst;
}
inline bool trashItem(const LibItem& it, std::string& err) { return !moveItem(it, libTrashDir(), err).empty(); }
inline bool trashFolder(const std::string& dir, std::string& err) {
  std::vector<DirEnt> ents; listDir(dir, ents);
  if (ents.empty()) { if (removeDir(dir)) return true; }
  if (!makeDir(libTrashDir())) { err = "could not create the Trash folder"; return false; }
  const std::string dst = uniquePath(libTrashDir(), fileName(dir), "");
  if (!movePath(dir, dst)) { err = "could not move the folder"; return false; }
  return true;
}

} // namespace hd
