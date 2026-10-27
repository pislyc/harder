// File helpers, see FileSys.hpp.
#include "FileSys.hpp"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
# include <shellapi.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
# include <cerrno>
# include <sys/wait.h>
#endif

namespace hd {

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); if (n <= 0) return std::wstring();
  std::wstring w((size_t)n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize((size_t)n - 1); return w;
}
static std::string narrow(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr); if (n <= 0) return std::string();
  std::string s((size_t)n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr); s.resize((size_t)n - 1); return s;
}
static FILE* openUtf8(const std::string& p, const wchar_t* mode) { return _wfopen(widen(p).c_str(), mode); }
static int64_t ftToUnix(const FILETIME& ft) { const uint64_t t = (uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime; return (int64_t)(t / 10000) - 11644473600000LL; }   // ms
#else
static FILE* openUtf8(const std::string& p, const char* mode) { return std::fopen(p.c_str(), mode); }
#endif

bool readTextFile(const std::string& path, std::string& out) {
#ifdef _WIN32
  FILE* f = openUtf8(path, L"rb");
#else
  FILE* f = openUtf8(path, "rb");
#endif
  if (!f) return false;
  out.clear(); std::vector<char> buf(1 << 16); size_t n; while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) out.append(buf.data(), n);
  std::fclose(f); return true;
}
bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
  const std::string tmp = path + ".tmp";
#ifdef _WIN32
  FILE* f = openUtf8(tmp, L"wb");
#else
  FILE* f = openUtf8(tmp, "wb");
#endif
  if (!f) return false;
  const bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
#ifdef _WIN32
  if (std::fclose(f) != 0 || !ok) { _wremove(widen(tmp).c_str()); return false; }
  return MoveFileExW(widen(tmp).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); return false; }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
#endif
}

bool listDir(const std::string& dir, std::vector<DirEnt>& out) {
  out.clear();
#ifdef _WIN32
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return false;
  do {
    if (!std::wcscmp(fd.cFileName, L".") || !std::wcscmp(fd.cFileName, L"..")) continue;
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
    DirEnt e; e.name = narrow(fd.cFileName); e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.mtime = ftToUnix(fd.ftLastWriteTime); e.size = (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
    if (!e.name.empty() && e.name[0] != '.') out.push_back(e);
  } while (FindNextFileW(h, &fd));
  FindClose(h); return true;
#else
  DIR* d = opendir(dir.c_str()); if (!d) return false;
  while (dirent* de = readdir(d)) {
    if (de->d_name[0] == '.') continue;   // also . and ..
    DirEnt e; e.name = de->d_name; struct stat st;
    if (stat(joinPath(dir, e.name).c_str(), &st) != 0) continue;
    e.dir = S_ISDIR(st.st_mode); e.mtime = (int64_t)st.st_mtime * 1000; e.size = (uint64_t)st.st_size;
    out.push_back(e);
  }
  closedir(d); return true;
#endif
}
bool pathExists(const std::string& path, bool* isDir) {
#ifdef _WIN32
  const DWORD a = GetFileAttributesW(widen(path).c_str()); if (a == INVALID_FILE_ATTRIBUTES) return false;
  if (isDir) *isDir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0; return true;
#else
  struct stat st; if (stat(path.c_str(), &st) != 0) return false; if (isDir) *isDir = S_ISDIR(st.st_mode); return true;
#endif
}
bool makeDir(const std::string& path) {
  bool d = false; if (pathExists(path, &d)) return d;
#ifdef _WIN32
  CreateDirectoryW(widen(path).c_str(), nullptr);
#else
  mkdir(path.c_str(), 0755);
#endif
  return pathExists(path, &d) && d;
}
bool movePath(const std::string& from, const std::string& to) {
  if (pathExists(to)) return false;
#ifdef _WIN32
  return MoveFileExW(widen(from).c_str(), widen(to).c_str(), 0) != 0;
#else
  return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}
bool removeFile(const std::string& path) {
#ifdef _WIN32
  return DeleteFileW(widen(path).c_str()) != 0;
#else
  return unlink(path.c_str()) == 0;
#endif
}
bool removeDir(const std::string& path) {
#ifdef _WIN32
  return RemoveDirectoryW(widen(path).c_str()) != 0;
#else
  return rmdir(path.c_str()) == 0;
#endif
}
bool copyFile(const std::string& from, const std::string& to) {
  std::string data; if (!readTextFile(from, data)) return false;
  return writeFileAtomic(to, data.data(), data.size());
}
std::string keyNameForScancode(int sc) {
#ifdef _WIN32
  wchar_t w[64] = { 0 };
  if (GetKeyNameTextW((LONG)((sc & 0xFF) << 16), w, 64) > 0) return narrow(w);
  return std::string();
#else
  (void)sc; return std::string();
#endif
}
void openInFileManager(const std::string& path) {
#ifdef _WIN32
  ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  const pid_t p = fork();
  if (p == 0) { const pid_t q = fork(); if (q == 0) { execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr); _exit(127); } _exit(0); }
  if (p > 0) { int st; waitpid(p, &st, 0); }
#endif
}

} // namespace hd
