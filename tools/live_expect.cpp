// Expected output of the standalone after pressing PLAY: project file, engine idle for `pre` samples, then the UI's
// play() commands (BPM, SWING, PLAY). Writes float32 stereo interleaved.  usage: live_expect project.json pre n out.f32
#include "../plugins/Harder/Export.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace hd;
int main(int argc, char** argv) {
  std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf();
  std::unique_ptr<Project> P(new Project()); if (!projectFromString(ss.str(), *P)) return 2;
  const long pre = atol(argv[2]), n = atol(argv[3]); const double sr = 48000;
  std::unique_ptr<Engine> E(new Engine()); E->init(sr); loadEngine(*E, *P, P->master);
  std::vector<float> L(n), R(n); long pos = 0;
  while (pos < pre) { const int k = (int)std::min(512L, pre - pos); E->process(L.data(), R.data(), k); pos += k; }
  E->msgBpm(P->bpm); E->msgSwing(P->swing); E->internalPlay();
  E->process(L.data(), R.data(), (int)n);
  FILE* o = fopen(argv[4], "wb"); for (long i = 0; i < n; i++) { fwrite(&L[i], 4, 1, o); fwrite(&R[i], 4, 1, o); } fclose(o);
  return 0;
}
