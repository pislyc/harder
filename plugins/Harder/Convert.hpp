// JSON <-> engine structures, in the web app's format (harder-project.json / engine 'init' state).
#pragma once
#include "Engine.hpp"
#include "Json.hpp"

namespace hd {

inline void locksFromJson(const JVal& l, Locks& L) {
  L.clear();
  if (!l.isObj()) return;
  for (auto& kv : l.o) {
    char* e; long k = std::strtol(kv.first.c_str(), &e, 10);
    if (*e || k < 0 || k >= NP || !kv.second.isNum() || !std::isfinite(kv.second.n)) continue;
    L.set((int)k, jsonParam(kv.second.n));
  }
}
inline JVal locksToJson(const Locks& L) {
  JVal o = JVal::Obj();
  for (int i = 0; i < NP; i++) if (L.has(i)) { char b[8]; std::snprintf(b, sizeof b, "%d", i); o.set(b, JVal::Num(L.v[i])); }
  return o;
}
inline void stepFromJson(const JVal& j, Step& st) {
  st.on = 0; st.tl = 0; st.l.clear();
  if (!j.isObj()) return;
  st.on = 1; if (j["tl"].truthy()) st.tl = 1;
  locksFromJson(j["l"], st.l);
}
inline JVal stepToJson(const Step& st) {
  if (!st.on) return JVal();
  JVal o = JVal::Obj(); if (st.tl) o.set("tl", JVal::Num(1)); if (st.l.any()) o.set("l", locksToJson(st.l)); return o;
}
inline int clampi(double v, int a, int b, int def) { return jsonInt(v, a, b, def); }
// web validProject() rules
inline void patternFromJson(const JVal& q, Pattern& P) {
  P.reset();
  // JS: clamp(+q.bpm || 125, 40, 300)
  { const double b = q["bpm"].isNum() && std::isfinite(q["bpm"].n) && q["bpm"].n != 0 ? q["bpm"].n : 125; P.bpm = std::min(300.0, std::max(40.0, b)); }
  { const double w = q["swing"].isNum() && std::isfinite(q["swing"].n) && q["swing"].n != 0 ? q["swing"].n : 50; P.swing = std::min(80.0, std::max(50.0, w)); }
  { const int m = jsonInt(q["mlen"].num(0), -1000000, 1000000, 0); P.mlen = clampi(m ? m : 16, 1, 64, 16); }
  for (int tr = 0; tr < NTR; tr++) {
    const int l = jsonInt(q["len"][tr].num(0), -1000000, 1000000, 0); P.len[tr] = clampi(l ? l : 16, 1, 64, 16);
    const JVal& s = q["scl"][tr]; P.scl[tr] = (s.isNum() && s.n == std::floor(s.n)) ? clampi(s.n, 0, 6, 4) : 4;
    const JVal& row = q["steps"][tr];
    for (int s2 = 0; s2 < NSTEP; s2++) stepFromJson(row[s2], P.steps[tr][s2]);
  }
}
inline JVal patternToJson(const Pattern& P) {
  JVal o = JVal::Obj();
  o.set("mlen", JVal::Num(P.mlen));
  JVal len = JVal::Arr(), scl = JVal::Arr(), steps = JVal::Arr();
  for (int tr = 0; tr < NTR; tr++) {
    len.push(JVal::Num(P.len[tr])); scl.push(JVal::Num(P.scl[tr]));
    JVal row = JVal::Arr(); for (int s = 0; s < NSTEP; s++) row.push(stepToJson(P.steps[tr][s])); steps.push(std::move(row));
  }
  o.set("len", std::move(len)); o.set("scl", std::move(scl)); o.set("steps", std::move(steps));
  o.set("bpm", JVal::Num(P.bpm)); o.set("swing", JVal::Num(P.swing));
  return o;
}
inline bool paramsFromJson(const JVal& a, int mid, double* p) {
  defaultTrackParams(mid, p);
  if (!a.isArr()) return false;
  for (int i = 0; i < NP; i++) { const JVal& v = a[i]; if (v.isNum() && std::isfinite(v.n)) p[i] = jsonParam(v.n); }
  return true;
}

} // namespace hd
