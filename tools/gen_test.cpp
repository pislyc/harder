// Dumps generator output for the matrix in gen_test.js (C++ side).
#include "../plugins/Harder/Model.hpp"
#include <cstdio>
using namespace hd;
int main() {
  std::unique_ptr<Project> S(new Project()); JVal out = JVal::Arr();
  defaultProject(*S); out.push(projectToJson(*S));
  const double dens[3] = { 0.3, 0.55, 0.9 }, varis[3] = { 0.1, 0.6, 0.95 };
  std::unique_ptr<Pattern> P(new Pattern());
  for (int k = 0; k < NKITS; k++) {
    defaultProject(*S); kitTracks(k, *S); kitMaster(k, S->master, S->master);
    for (int st = 0; st < NSTYLES; st++) for (int sd = 1; sd <= 3; sd++) {
      GenOpts o; o.den = dens[(st + sd) % 3]; o.vari = varis[(st + 2 * sd + k) % 3]; o.locks = (sd != 2); o.fills = (sd != 3); o.half = ((st + k) % 4 == 0);
      Mulberry r(k * 1000 + st * 10 + sd); const double keep[2] = { 111, 61 };
      generatePattern(r, st, o, sd == 3 ? keep : nullptr, *S, *P); out.push(patternToJson(*P));
      Mulberry r2(k * 7 + st * 13 + sd); mutatePattern(*P, r2, o.vari, *S); out.push(patternToJson(*P));
    }
    for (int sd = 0; sd < 5; sd++) { GenOpts o; o.den = dens[sd % 3]; o.vari = varis[(sd + k) % 3]; o.locks = sd % 2 == 0; Mulberry r(50000 + k * 10 + sd); polyPattern(r, o, 120 + sd, 50 + sd, *S, *P); out.push(patternToJson(*P)); }
  }
  std::string s; writeJson(s, out); std::fwrite(s.data(), 1, s.size(), stdout); return 0;
}
