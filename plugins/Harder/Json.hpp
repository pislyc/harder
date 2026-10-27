// Minimal JSON DOM: enough for HARDER project files (web-compatible harder-project.json) and test scenarios.
// Never used on the audio thread.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace hd {

struct JVal {
  enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
  bool b = false; double n = 0; std::string s;
  std::vector<JVal> a;
  std::vector<std::pair<std::string, JVal>> o;   // insertion order kept
  static const JVal& null() { static JVal z; return z; }
  bool isNum() const { return kind == NUM; }
  bool isArr() const { return kind == ARR; }
  bool isObj() const { return kind == OBJ; }
  bool isStr() const { return kind == STR; }
  const JVal& operator[](const char* k) const { if (kind == OBJ) for (auto& p : o) if (p.first == k) return p.second; return null(); }
  const JVal& operator[](size_t i) const { return (kind == ARR && i < a.size()) ? a[i] : null(); }
  bool has(const char* k) const { if (kind == OBJ) for (auto& p : o) if (p.first == k) return true; return false; }
  size_t size() const { return kind == ARR ? a.size() : kind == OBJ ? o.size() : 0; }
  double num(double d = 0) const { return kind == NUM ? n : kind == BOOL ? (b ? 1 : 0) : d; }
  bool truthy() const { return kind == BOOL ? b : kind == NUM ? n != 0 : kind == STR ? !s.empty() : kind != NUL; }
  const std::string& str() const { static std::string e; return kind == STR ? s : e; }
  // builders
  static JVal Num(double v) { JVal j; j.kind = NUM; j.n = v; return j; }
  static JVal Str(const std::string& v) { JVal j; j.kind = STR; j.s = v; return j; }
  static JVal Str(std::string&& v) { JVal j; j.kind = STR; j.s = std::move(v); return j; }
  static JVal Bool(bool v) { JVal j; j.kind = BOOL; j.b = v; return j; }
  static JVal Arr() { JVal j; j.kind = ARR; return j; }
  static JVal Obj() { JVal j; j.kind = OBJ; return j; }
  JVal& push(JVal v) { a.push_back(std::move(v)); return a.back(); }
  JVal& set(const std::string& k, JVal v) { for (auto& p : o) if (p.first == k) { p.second = std::move(v); return p.second; } o.emplace_back(k, std::move(v)); return o.back().second; }
};

struct JParser {
  const char* p; const char* end; bool ok = true; int depth = 0;
  JParser(const char* s, size_t n) : p(s), end(s + n) {}
  void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
  bool lit(const char* w) { size_t n = 0; while (w[n]) n++; if ((size_t)(end - p) < n) return false; for (size_t i = 0; i < n; i++) if (p[i] != w[i]) return false; p += n; return true; }
  static void utf8(std::string& out, unsigned c) {
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
    else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 63)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
  }
  bool hex4(unsigned& c) { if (end - p < 4) return false; c = 0; for (int i = 0; i < 4; i++) { char h = *p++; c <<= 4; if (h >= '0' && h <= '9') c |= h - '0'; else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10; else return false; } return true; }
  bool string(std::string& out) {
    if (p >= end || *p != '"') return false; p++;
    while (p < end && *p != '"') {
      if (*p == '\\') {
        p++; if (p >= end) return false; char c = *p++;
        switch (c) {
          case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
          case 'u': { unsigned u; if (!hex4(u)) return false; if (u >= 0xD800 && u < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') { p += 2; unsigned l; if (!hex4(l)) return false; u = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00); } utf8(out, u); break; }
          default: out += c;
        }
      } else { const char* q = p; while (q < end && *q != '"' && *q != '\\') q++; out.append(p, q); p = q; }   // plain run at once (embedded samples are MB long)
    }
    if (p >= end) return false; p++; return true;
  }
  bool value(JVal& v) {
    if (++depth > 64) return false;
    ws(); if (p >= end) return false;
    bool r = true;
    if (*p == '{') {
      p++; v.kind = JVal::OBJ; ws();
      if (p < end && *p == '}') { p++; }
      else for (;;) {
        ws(); std::string k; if (!string(k)) { r = false; break; }
        ws(); if (p >= end || *p != ':') { r = false; break; } p++;
        JVal c; if (!value(c)) { r = false; break; } v.o.emplace_back(std::move(k), std::move(c));
        ws(); if (p < end && *p == ',') { p++; continue; } if (p < end && *p == '}') { p++; break; } r = false; break;
      }
    } else if (*p == '[') {
      p++; v.kind = JVal::ARR; ws();
      if (p < end && *p == ']') { p++; }
      else for (;;) {
        JVal c; if (!value(c)) { r = false; break; } v.a.push_back(std::move(c));
        ws(); if (p < end && *p == ',') { p++; continue; } if (p < end && *p == ']') { p++; break; } r = false; break;
      }
    } else if (*p == '"') { v.kind = JVal::STR; r = string(v.s); }
    else if (lit("true")) { v.kind = JVal::BOOL; v.b = true; }
    else if (lit("false")) { v.kind = JVal::BOOL; v.b = false; }
    else if (lit("null")) { v.kind = JVal::NUL; }
    else {
      char buf[64]; int k = 0;
      while (p < end && k < 63 && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) buf[k++] = *p++;
      buf[k] = 0; if (!k) r = false; else { char* e; v.kind = JVal::NUM; v.n = std::strtod(buf, &e); if (*e) r = false; }
    }
    depth--; return r;
  }
};

inline bool parseJson(const std::string& s, JVal& out) {
  JParser P(s.data(), s.size()); out = JVal(); if (!P.value(out)) return false; P.ws(); return P.p == P.end;
}

inline void jsonNum(std::string& o, double v) {
  if (!std::isfinite(v)) { o += "0"; return; }
  if (v == std::floor(v) && std::fabs(v) < 1e15) { char b[32]; std::snprintf(b, sizeof b, "%.0f", v); o += b; return; }
  char b[40]; std::snprintf(b, sizeof b, "%.15g", v); o += b;
}
inline void jsonStr(std::string& o, const std::string& s) {
  o += '"';
  if (s.size() > 4096) {   // long strings (base64 sample data): copy plain runs at once
    size_t i = 0;
    while (i < s.size()) {
      size_t j = i; while (j < s.size() && (unsigned char)s[j] >= 0x20 && s[j] != '"' && s[j] != '\\') j++;
      o.append(s, i, j - i); if (j >= s.size()) break;
      const unsigned char c = (unsigned char)s[j];
      if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
      else { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
      i = j + 1;
    }
    o += '"'; return;
  }
  for (unsigned char c : s) {
    if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
  }
  o += '"';
}
inline void writeJson(std::string& o, const JVal& v) {
  switch (v.kind) {
    case JVal::NUL: o += "null"; break;
    case JVal::BOOL: o += v.b ? "true" : "false"; break;
    case JVal::NUM: jsonNum(o, v.n); break;
    case JVal::STR: jsonStr(o, v.s); break;
    case JVal::ARR: o += '['; for (size_t i = 0; i < v.a.size(); i++) { if (i) o += ','; writeJson(o, v.a[i]); } o += ']'; break;
    case JVal::OBJ: o += '{'; for (size_t i = 0; i < v.o.size(); i++) { if (i) o += ','; jsonStr(o, v.o[i].first); o += ':'; writeJson(o, v.o[i].second); } o += '}'; break;
  }
}

} // namespace hd
