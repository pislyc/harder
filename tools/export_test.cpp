// C++ side of the export test: same projects as export_ref.js -> <case>.loop.cpp.zip / <case>.kit.cpp.zip
// usage: export_test <dir> <case>...
#include "../plugins/Harder/Export.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>
using namespace hd;
static std::string slurp(const std::string& f) { std::ifstream i(f, std::ios::binary); std::stringstream s; s << i.rdbuf(); return s.str(); }
static void spit(const std::string& f, const std::vector<uint8_t>& d) { std::ofstream o(f, std::ios::binary); o.write((const char*)d.data(), (std::streamsize)d.size()); }
int main(int argc, char** argv) {
  const std::string dir = argv[1]; int fails = 0;
  for (int a = 2; a < argc; a++) {
    const std::string c = argv[a]; std::unique_ptr<Project> P(new Project());
    if (!projectFromString(slurp(dir + "/" + c + ".project.json"), *P)) { std::printf("%s: project not loaded\n", c.c_str()); fails++; continue; }
    JVal o; parseJson(slurp(dir + "/" + c + ".opts.json"), o);
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> z; exportLoopZip(*P, (int)o["loops"].num(1), o["stems"].truthy(), z, nullptr); spit(dir + "/" + c + ".loop.cpp.zip", z);
    const double tl = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::vector<uint8_t> k; exportKitZip(*P, k, nullptr); spit(dir + "/" + c + ".kit.cpp.zip", k);
    const std::string want = slurp(dir + "/" + c + ".loop.name"), got = lower(loopBaseName(*P)) + ".zip";
    if (want != got) { std::printf("%s: zip name %s vs web %s\n", c.c_str(), got.c_str(), want.c_str()); fails++; }
    std::printf("%s: loop export %.2f s\n", c.c_str(), tl);
  }
  return fails ? 1 : 0;
}
