// File helpers with UTF-8 paths on every OS (wide-character API on Windows). Main thread / worker threads only.
// Implemented in FileSys.cpp (no windows.h in the UI translation unit: it clashes with DGL names).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hd {

#ifdef _WIN32
constexpr char PATH_SEP = '\\';
#else
constexpr char PATH_SEP = '/';
#endif

struct DirEnt { std::string name; bool dir = false; int64_t mtime = 0; uint64_t size = 0; };

bool readTextFile(const std::string& path, std::string& out);                 // binary-safe
bool writeFileAtomic(const std::string& path, const void* data, size_t size); // path.tmp, then replaces path
inline bool writeFileAtomic(const std::string& path, const std::string& s) { return writeFileAtomic(path, s.data(), s.size()); }
bool listDir(const std::string& dir, std::vector<DirEnt>& out);   // without . and .., hidden/system entries skipped
bool makeDir(const std::string& path);                            // true if it exists afterwards
bool pathExists(const std::string& path, bool* isDir = nullptr);
bool movePath(const std::string& from, const std::string& to);    // rename; fails if `to` exists
bool removeFile(const std::string& path);
bool removeDir(const std::string& path);                          // empty directories only
bool copyFile(const std::string& from, const std::string& to);
void openInFileManager(const std::string& path);                  // Explorer / the desktop's file manager
std::string keyNameForScancode(int sc);                           // the key's name in the active layout (Windows), "" if unknown

inline std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b; const char c = a.back(); return (c == '/' || c == '\\') ? a + b : a + PATH_SEP + b;
}
inline std::string fileName(const std::string& p) { const size_t s = p.find_last_of("/\\"); return s == std::string::npos ? p : p.substr(s + 1); }
inline std::string dirName(const std::string& p) { const size_t s = p.find_last_of("/\\"); return s == std::string::npos ? std::string() : p.substr(0, s); }
inline std::string stemOf(const std::string& name) { const size_t d = name.find_last_of('.'); return d == std::string::npos || d == 0 ? name : name.substr(0, d); }
inline std::string extOf(const std::string& name) {
  const size_t d = name.find_last_of('.'); if (d == std::string::npos) return std::string();
  std::string e = name.substr(d); for (char& c : e) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return e;
}

} // namespace hd
