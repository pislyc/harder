// File helpers, see FileSys.hpp.
#include "FileSys.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
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
#ifdef __APPLE__
# define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0   // no check() / verify() / require() macros
# include <Carbon/Carbon.h>
# include <crt_externs.h>
# include <spawn.h>
# include <chrono>
# include <map>
# include <objc/runtime.h>
# include <objc/message.h>
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
bool readFilePrefix(const std::string& path, uint64_t maxBytes, std::string& out, uint64_t* total) {
#ifdef _WIN32
  FILE* f = openUtf8(path, L"rb");
#else
  FILE* f = openUtf8(path, "rb");
#endif
  if (!f) return false;
#ifdef _WIN32
  _fseeki64(f, 0, SEEK_END); const int64_t sz = _ftelli64(f); _fseeki64(f, 0, SEEK_SET);
#else
  fseeko(f, 0, SEEK_END); const int64_t sz = (int64_t)ftello(f); fseeko(f, 0, SEEK_SET);
#endif
  if (total) *total = sz > 0 ? (uint64_t)sz : 0;
  const uint64_t want = sz > 0 && (uint64_t)sz < maxBytes ? (uint64_t)sz : maxBytes;
  out.clear(); out.reserve((size_t)want);
  std::vector<char> buf(1 << 16); uint64_t got = 0; size_t n;
  while (got < want && (n = std::fread(buf.data(), 1, (size_t)std::min<uint64_t>(buf.size(), want - got), f)) > 0) { out.append(buf.data(), n); got += n; }
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
#ifdef __APPLE__
// kVK_* (the key's position on an ANSI / ISO Apple keyboard) -> PC set-1 scan code at the same position.
// Keypad / and Enter carry the extended bit like on Windows (0x135, 0x11C). Unknown keys: 0x200 + vk.
static int macKvkToSet1(int vk, bool iso) {
  static const int16_t T[0x80] = {
    0x1E, 0x1F, 0x20, 0x21, 0x23, 0x22, 0x2C, 0x2D, 0x2E, 0x2F, 0x56, 0x30, 0x10, 0x11, 0x12, 0x13,   // 00 A S D F H G Z X C V §(ISO) B Q W E R
    0x15, 0x14, 0x02, 0x03, 0x04, 0x05, 0x07, 0x06, 0x0D, 0x0A, 0x08, 0x0C, 0x09, 0x0B, 0x1B, 0x18,   // 10 Y T 1 2 3 4 6 5 = 9 7 - 8 0 ] O
    0x16, 0x1A, 0x17, 0x19, 0x1C, 0x26, 0x24, 0x28, 0x25, 0x27, 0x2B, 0x33, 0x35, 0x31, 0x32, 0x34,   // 20 U [ I P Return L J ' K ; \ , / N M .
    0x0F, 0x39, 0x29, 0x0E, 0,    0x01, 0,    0,    0x2A, 0x3A, 0x38, 0x1D, 0x36, 0x38, 0x1D, 0,      // 30 Tab Space ` Delete - Esc Cmd Cmd Shift Caps Opt Ctrl RShift ROpt RCtrl Fn
    0,    0x53, 0,    0x37, 0,    0x4E, 0,    0x45, 0,    0,    0,    0x135, 0x11C, 0, 0x4A, 0,     // 40 F17 KP. - KP* - KP+ - Clear ... KP/ KPEnter - KP-
    0,    0x59, 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0,    0x48, 0x49, 0x7D, 0x73, 0x7E,   // 50 F18 KP= KP0..KP7 F20 KP8 KP9 Yen _ KP,
    0,    0,    0,    0,    0,    0,    0x7B, 0,    0x70, 0,    0,    0,    0,    0,    0,    0,      // 60 (F-keys arrive as DGL codes) Eisu Kana
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0 };
  if (vk < 0 || vk >= 0x80) return vk > 0 ? 0x200 + vk : 0;
  // ISO keyboards: the key left of 1 sends kVK_ISO_Section and the key next to the left Shift kVK_ANSI_Grave
  if (iso && vk == 0x0A) return 0x29;
  if (iso && vk == 0x32) return 0x56;
  return T[vk] ? T[vk] : 0x200 + vk;
}
static bool macIsoKeyboard() { return KBGetLayoutType(LMGetKbdType()) == kKeyboardISO; }
int macKeyToScancode(int vk) { return macKvkToSet1(vk, macIsoKeyboard()); }
bool requestAppQuit() {
  Class cls = (Class)objc_getClass("NSApplication"); if (!cls) return false;
  id app = ((id(*)(Class, SEL))objc_msgSend)(cls, sel_registerName("sharedApplication"));
  if (!app) return false;
  ((void(*)(id, SEL, id))objc_msgSend)(app, sel_registerName("terminate:"), (id)nullptr);
  return true;
}
// Holding a letter key (a step key held for a parameter lock) would open macOS's accent popup: off for this program
// (application domain; accented letters are still typed with Option + key)
void macStandaloneInit() { CFPreferencesSetAppValue(CFSTR("ApplePressAndHoldEnabled"), kCFBooleanFalse, kCFPreferencesCurrentApplication); }
// the character the active layout prints on a key (cached; the layout is checked again after a second)
std::string keyNameForScancode(int sc) {
  static std::map<int, std::string> cache; static auto built = std::chrono::steady_clock::time_point();
  const auto now = std::chrono::steady_clock::now();
  if (now - built > std::chrono::seconds(1)) {
    cache.clear(); built = now;
    const bool iso = macIsoKeyboard();
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    if (!src) src = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    const CFDataRef data = src ? (CFDataRef)TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData) : nullptr;
    const UCKeyboardLayout* lay = data ? (const UCKeyboardLayout*)CFDataGetBytePtr(data) : nullptr;
    for (int vk = 0; lay && vk < 0x80; vk++) {
      const int k = macKvkToSet1(vk, iso);
      // only the keys that print a character (letters, digits, punctuation): the others keep their US names
      if (!((k >= 0x02 && k <= 0x0D) || (k >= 0x10 && k <= 0x1B) || (k >= 0x1E && k <= 0x29) || (k >= 0x2B && k <= 0x35) || k == 0x56)) continue;
      UInt32 dead = 0; UniChar ch[8]; UniCharCount n = 0;
      if (UCKeyTranslate(lay, (UInt16)vk, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask, &dead, 8, &n, ch) != noErr || n == 0) continue;
      if (ch[0] < 0x20 || ch[0] == 0x7F) continue;
      CFStringRef str = CFStringCreateWithCharacters(kCFAllocatorDefault, ch, (CFIndex)n); if (!str) continue;
      CFMutableStringRef up = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, str); CFRelease(str);
      if (!up) continue;
      CFStringUppercase(up, nullptr);
      char buf[32];
      if (CFStringGetCString(up, buf, sizeof buf, kCFStringEncodingUTF8)) cache[k] = buf;
      CFRelease(up);
    }
    if (src) CFRelease(src);
  }
  const auto it = cache.find(sc); return it == cache.end() ? std::string() : it->second;
}
#else
int macKeyToScancode(int vk) { return vk; }
bool requestAppQuit() { return false; }
void macStandaloneInit() {}
std::string keyNameForScancode(int sc) {
#ifdef _WIN32
  wchar_t w[64] = { 0 };
  if (GetKeyNameTextW((LONG)((sc & 0xFF) << 16), w, 64) > 0) return narrow(w);
  return std::string();
#else
  (void)sc; return std::string();
#endif
}
#endif
void openInFileManager(const std::string& path) {
#ifdef _WIN32
  ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  // Finder; posix_spawn rather than fork (a forked child of a multithreaded Cocoa program may only exec)
  pid_t pid = 0; char* const argv[] = { (char*)"/usr/bin/open", (char*)path.c_str(), nullptr };
  if (posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, *_NSGetEnviron()) == 0) { int st; waitpid(pid, &st, 0); }
#else
  const pid_t p = fork();
  if (p == 0) { const pid_t q = fork(); if (q == 0) { execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr); _exit(127); } _exit(0); }
  if (p > 0) { int st; waitpid(p, &st, 0); }
#endif
}

} // namespace hd
