// Tests for the plugin glue without a host: command link, host-parameter mirror, state round trip,
// web project import, and a two-thread stress run (UI thread hammering edits while audio renders).
#include "../plugins/Harder/HarderShared.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <random>
#include <ctime>
#include <array>
#include <algorithm>
#include "ftz.hpp"
using namespace hd;
namespace hd { Core* harderCore(void*) { return nullptr; } }
static int fails = 0;
// allocation tracker: counts heap allocations made while the 'audio thread' is inside a block
static thread_local bool inAudio = false; static std::atomic<long> audioAllocs{ 0 };
void* operator new(size_t n) { if (inAudio) audioAllocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { if (inAudio) audioAllocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); } void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); } void operator delete[](void* p, size_t) noexcept { std::free(p); }
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static std::string readFile(const char* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static double cpuUs() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

static void render(Engine& E, std::vector<float>& out, int n) { out.resize(2 * n); std::vector<float> L(128), R(128); for (int p = 0; p < n; p += 128) { E.process(L.data(), R.data(), 128); for (int i = 0; i < 128; i++) { out[2 * (p + i)] = L[i]; out[2 * (p + i) + 1] = R[i]; } } }

int main(int argc, char** argv) {
  const int uiRate = argc > 2 ? std::atoi(argv[2]) : 0;   // UI edits per second (0 = as fast as possible)
  harderFtz();
  // 1. project through the command link == project loaded directly
  {
    std::unique_ptr<Core> C(new Core()); defaultProject(*C->S);
    std::unique_ptr<Engine> A(new Engine()), B(new Engine()); A->init(48000); B->init(48000);
    C->sendAll(true); C->link.drainAll(*A);
    const Project& P = *C->S;
    for (int t = 0; t < NT; t++) B->loadTrack(t, P.tracks[t].m, P.tracks[t].p);
    B->loadMaster(P.master); for (int i = 0; i < NPAT; i++) B->setPattern(i, P.patterns[i]); B->loadDone(P.cur, P.bpm, P.swing, P.mutes);
    A->play(); B->play(); std::vector<float> a, b; render(*A, a, 48000 * 8); render(*B, b, 48000 * 8);
    double md = 0; for (size_t i = 0; i < a.size(); i++) md = std::max(md, (double)std::fabs(a[i] - b[i]));
    CHECK(md == 0, "link vs direct differ %g", md); std::printf("link == direct load: max diff %g over 8 s\n", md);
  }
  // 2. state round trip and web import (argv[1] = JS default project JSON from webcore)
  {
    std::unique_ptr<Project> P(new Project()), Q(new Project()); defaultProject(*P);
    const std::string s1 = projectToString(*P); CHECK(projectFromString(s1, *Q), "round trip parse"); const std::string s2 = projectToString(*Q);
    CHECK(s1 == s2, "state round trip changed the project"); std::printf("state round trip: %zu bytes, identical: %s\n", s1.size(), s1 == s2 ? "yes" : "NO");
    if (argc > 1) {
      std::unique_ptr<Project> W(new Project()); const bool ok = projectFromString(readFile(argv[1]), *W);
      CHECK(ok, "web project rejected"); const std::string sw = projectToString(*W);
      std::printf("web harder-project.json import: %s, equals C++ default project: %s\n", ok ? "ok" : "REJECTED", sw == s1 ? "yes" : "no");
      CHECK(sw == s1, "web default project differs from C++ default project");
    }
    CHECK(!projectFromString("{\"v\":3}", *Q) && !projectFromString("garbage", *Q) && !projectFromString("", *Q), "invalid state accepted");
  }
  // 3. host parameters: host -> engine, host -> model, model -> host
  {
    std::unique_ptr<Core> C(new Core()); defaultProject(*C->S); C->refreshHost(nullptr, false);
    std::unique_ptr<Engine> E(new Engine()); E->init(48000); C->sendAll(true); C->link.drainAll(*E);
    int bad = 0;
    for (int idx = 0; idx < HP_COUNT; idx++) {
      const HostParamInfo h = hostParamInfo(idx); double mn, mx, df; hostRange(idx, mn, mx, df);
      const float hv = (float)(mn + (mx - mn) * 0.37); C->setHost(idx, hv); C->applyHost(*E);
      if (h.kind == 0) { const double want = h.i < 8 ? srcToEngine(E->tracks[h.tr].m, h.i, hv) : hv; if (std::fabs(E->tracks[h.tr].p[h.i] - want) > 1e-6) bad++; }
      if (h.kind == 1 && std::fabs(E->master[h.i] - hv) > 1e-6) bad++;
      if (h.kind == 2 && E->mutes[h.tr] != (hv > 0.5f)) bad++;
    }
    CHECK(bad == 0, "%d host params did not reach the engine", bad);
    CHECK(C->pullHost(), "pullHost saw no change");
    int bad2 = 0; for (int idx = 0; idx < HP_COUNT; idx++) { const HostParamInfo h = hostParamInfo(idx); if (h.kind >= 2) continue; if (h.kind == 0 && h.i < 8 && T().mach[C->S->tracks[h.tr].m][h.i].max - T().mach[C->S->tracks[h.tr].m][h.i].min < 20) continue;
      if (std::fabs(C->hostFromModel(idx) - C->hostVal[idx].load()) > 2e-3) { bad2++; std::printf("  mirror %d kind %d tr %d i %d: model %g host %g\n", idx, h.kind, h.tr, h.i, C->hostFromModel(idx), C->hostVal[idx].load()); } }
    CHECK(bad2 == 0, "%d host params not mirrored into the model", bad2);
    std::vector<int> ch; C->refreshHost(&ch); std::vector<int> ch2; C->refreshHost(&ch2); CHECK(ch2.empty(), "mirror not stable (%zu)", ch2.size());
    std::printf("host params: %d exposed, host->engine ok: %s, host->model ok: %s\n", HP_COUNT, bad ? "NO" : "yes", bad2 ? "NO" : "yes");
    // values the plugin just set itself are not taken back by stale host values; echoes do nothing
    { const int idx = 5; double mn, mx, df; hostRange(idx, mn, mx, df); const float mine = (float)(mn + (mx - mn) * 0.8), old = (float)(mn + (mx - mn) * 0.2);
      for (auto& d : C->dirty) d.store(0);
      C->own(idx, mine); const bool refused = !C->setHost(idx, old); const bool kept = C->hostVal[idx].load() == mine;
      const bool echo = C->setHost(idx, mine) && C->dirty[0].load() == 0;
      C->ownUntil[idx].store(0); const bool later = C->setHost(idx, old) && C->hostVal[idx].load() == old;
      CHECK(refused && kept && echo && later, "ownership: refused %d kept %d echo %d later %d", refused, kept, echo, later);
      std::printf("plugin-owned values: stale host value refused %s, echo ignored %s, automation after the window %s\n", refused && kept ? "yes" : "NO", echo ? "yes" : "NO", later ? "yes" : "NO"); }
  }
  // 4. two threads: UI thread edits as fast as it can, audio thread renders 64-sample blocks
  {
    std::unique_ptr<Core> C(new Core()); defaultProject(*C->S); C->refreshHost(nullptr, false);
    std::unique_ptr<Engine> E(new Engine()); E->init(48000); C->sendAll(true); C->link.drainAll(*E); E->play();
    std::atomic<bool> stop{ false }; std::atomic<long> edits{ 0 };
    std::thread ui([&, uiRate] {
      std::mt19937 r(7); long k = 0;
      while (!stop.load()) {
        std::lock_guard<std::recursive_mutex> g(C->mtx);
        const int t = r() % NT; const int op = r() % 6;
        if (op == 0) { const int i = r() % 30; C->S->tracks[t].p[i] = r() % 128 * (T().track(C->S->tracks[t].m, i).max - T().track(C->S->tracks[t].m, i).min) / 127 + T().track(C->S->tracks[t].m, i).min; C->sendParam(t, i, C->S->tracks[t].p[i]); }
        else if (op == 1) { const int s = r() % 32; Step& st = C->S->patterns[C->S->cur].steps[t][s]; st.on = !st.on; if (st.on) st.l.set(41, r() % 127 + 1); C->sendStep(C->S->cur, t, s); }
        else if (op == 2) { C->sendPattern(r() % NPAT); }
        else if (op == 3) { C->sendAud(t, 100, nullptr); }
        else if (op == 4) { C->setHost(r() % HP_COUNT, (float)(r() % 100) / 100.f); }
        else if (op == 5 && k % 500 == 0) { const int m = r() % NMACH; C->S->tracks[t].m = m; defaultTrackParams(m, C->S->tracks[t].p); C->sendMachine(t); }
        k++; edits++;
        if (uiRate > 0) std::this_thread::sleep_for(std::chrono::microseconds(1000000 / uiRate));
        if (k % 20000 == 0) C->syncAllIfOverflow();
      }
    });
    std::vector<std::array<double,3>> top;
    std::vector<float> L(64), R(64); double worst = 0, tot = 0; long blocks = 0, skipped = 0; bool nan = false;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(6)) {
      const double a = cpuUs();
      inAudio = true;
      const size_t qn = C->link.q.size(); const bool wasPlaying = E->playing;
      if (!C->link.drain(*E)) skipped++;
      C->applyHost(*E); E->hostBlock(blocks % 4000 < 2000, blocks * 64 / 48000.0 * 2, 120 + (blocks % 500) * 0.1); E->process(L.data(), R.data(), 64);
      inAudio = false;
      const double us = cpuUs() - a; worst = std::max(worst, us); tot += us; blocks++;
      top.push_back({ us, (double)qn, (double)(wasPlaying != E->playing) }); std::sort(top.begin(), top.end(), [](const std::array<double,3>& x, const std::array<double,3>& y) { return x[0] > y[0]; }); if (top.size() > 6) top.pop_back();
      for (int i = 0; i < 64; i++) if (L[i] != L[i] || R[i] != R[i]) nan = true;
    }
    stop = true; ui.join();
    for (auto& t : top) std::printf("  slow block %.0f us: queue %.0f cmds (approx), transport change %s\n", t[0], t[1], t[2] ? "yes" : "no");
    CHECK(!nan, "NaN under concurrent edits");
    CHECK(audioAllocs.load() == 0, "%ld heap allocations on the audio thread", audioAllocs.load());
    std::printf("heap allocations on the audio thread during the run: %ld\n", audioAllocs.load());
    std::printf("concurrency: %ld UI edits vs %ld audio blocks in 6 s (%ld blocks had nothing to apply or found the link busy), audio avg %.0f us, worst %.0f us per 64-sample block (budget 1333), output clean: %s\n",
                edits.load(), blocks, skipped, tot / blocks, worst, nan ? "NO" : "yes");
  }
  std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL CORE TESTS PASSED", fails);
  return fails ? 1 : 0;
}
