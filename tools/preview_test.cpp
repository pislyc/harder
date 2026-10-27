// Plugin UI preview (HarderUI::renderPreview, one Engine reused for every render) vs the web renderOneShot.
#include "../plugins/Harder/Model.hpp"
#include <cstdio>
using namespace hd;
int main(int argc, char** argv) {
  const double PSR = 22050; const int N = (int)(PSR * 2.4); // UI: (int)(PSR*2.4)
  std::vector<float> ref((size_t)NPRESETS * N); FILE* f = fopen(argv[1], "rb"); if (!f || fread(ref.data(), 4, ref.size(), f) != ref.size()) { std::puts("no ref"); return 2; } fclose(f);
  std::unique_ptr<Engine> prev(new Engine()); prev->init(PSR); prev->previewReset(125);
  int bad = 0; double worst = 0;
  for (int pass = 0; pass < 2; pass++)   // pass 1: browse forward, pass 2: browse again (state left by every other preset)
  for (int pi = 0; pi < NPRESETS; pi++) {
    double p[NP]; presetParams(pi, p); std::vector<float> wave(N, 0.f);
    Engine& E = *prev; E.previewReset(125); E.loadTrack(0, PRESETS[pi].m, p); E.stems[0] = wave.data();
    E.audition(0, p[41] > 0 ? p[41] : 120, nullptr);
    float dl[512], dr[512];
    for (int q = 0; q < N; q += 512) { const int n = std::min(512, N - q); E.stems[0] = wave.data() + q; E.process(dl, dr, n); }
    E.stems[0] = nullptr;
    double md = 0, pk = 0; for (int i = 0; i < N; i++) { md = std::max(md, (double)std::fabs(wave[i] - ref[(size_t)pi * N + i])); pk = std::max(pk, (double)std::fabs(ref[(size_t)pi * N + i])); }
    if (md > 1e-4) { bad++; if (bad <= 60) std::printf("pass %d %-14s maxdiff %.4g (peak %.3f)\n", pass, PRESETS[pi].n, md, pk); }
    worst = std::max(worst, md);
  }
  std::printf("preview: %d of %d renders differ from the web preview, worst %.4g\n", bad, 2 * NPRESETS, worst);
  return bad ? 1 : 0;
}
