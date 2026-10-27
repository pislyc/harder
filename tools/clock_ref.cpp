// MIDI clock end-to-end test (see tools/jack_clock_test.c): reference with the standalone's absolute engine time: engine time = recording frame + D (D a multiple of
// the JACK period); D is searched. Then the whole recording is compared.
#include "../plugins/Harder/Model.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace hd;
int main(int, char** v) {
  std::ifstream fi(v[1]); std::stringstream ss; ss << fi.rdbuf(); std::unique_ptr<Project> P(new Project()); projectFromString(ss.str(), *P);
  const double bpm = atof(v[2]), t0 = atof(v[3]); const long BS = atol(v[5]), lat = atol(v[6]);   // lat: recording latency found before
  std::vector<float> r; { std::ifstream f2(v[4], std::ios::binary); f2.seekg(0, std::ios::end); size_t n = f2.tellg() / 4; f2.seekg(0); r.resize(n); f2.read((char*)r.data(), n * 4); }
  const long N = (long)r.size() / 2 - lat; const double tick = 48000.0 * 60 / bpm / 24;
  // optional Stop at stopT, Continue at contT from song position spp (16ths): the transport a host would report
  const double stopT = v[7] ? atof(v[7]) : 1e18, contT = v[8] ? atof(v[8]) : 1e18, spp = v[9] ? atof(v[9]) : 0;
  auto render = [&](long D, long upto, std::vector<float>& L, std::vector<float>& R) {
    std::unique_ptr<Engine> E(new Engine()); E->init(48000); for (int t = 0; t < NT; t++) E->loadTrack(t, P->tracks[t].m, P->tracks[t].p); E->loadMaster(P->master); for (int i = 0; i < NPAT; i++) E->setPattern(i, P->patterns[i]); E->loadDone(P->cur, P->bpm, P->swing, P->mutes);
    L.assign(D + upto, 0.f); R.assign(D + upto, 0.f);
    for (long pos = 0; pos < D + upto; pos += BS) { const double rp = pos - D; const bool cont = rp + BS > contT; const bool pl = cont || (rp + BS > t0 && rp + BS <= stopT);
      const double ppq = cont ? spp / 4 + (rp - contT) / tick / 24 : (rp - t0) / tick / 24;
      if (pl || E->hostSync) E->hostBlock(pl, ppq, bpm); E->process(&L[pos], &R[pos], (int)std::min(BS, D + upto - pos)); }
  };
  std::vector<float> L, R; long found = -1;
  for (long D = 0; D <= 48000L * 30; D += BS) {
    render(D, (long)t0 + 48000 * 3, L, R); double m = 0;
    for (long i = (long)t0; i < (long)t0 + 48000 * 3; i++) m = std::max(m, (double)std::fabs(r[(i + lat) * 2] - L[D + i]));
    if (m == 0) { found = D; break; }
  }
  if (found < 0) { std::puts("no engine time offset gives an exact match"); return 1; }
  render(found, N, L, R); double md = 0; long nd = 0;
  for (long i = 0; i < N; i++) { const double d = std::max(std::fabs(r[(i + lat) * 2] - L[found + i]), std::fabs(r[(i + lat) * 2 + 1] - R[found + i])); md = std::max(md, d); if (d > 0) nd++; }
  std::printf("engine time offset %ld samples (%.2f s); whole recording (%.1f s): max diff %.3g, %ld samples differ\n", found, found / 48000.0, N / 48000.0, md, nd);
}
