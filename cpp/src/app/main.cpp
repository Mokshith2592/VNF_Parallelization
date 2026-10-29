/* =============================================================
   LOA Hybrid — Entry point
     loa_hybrid                      start the web server on :8080
     loa_hybrid --serve [--port N] [--web DIR]
     loa_hybrid config.json          run once, print the report JSON
     loa_hybrid -                    same, config read from stdin
     loa_hybrid --selftest [N]       full search vs brute force on N random instances
     loa_hybrid --feasibility [--runs R] [--budget MS] [--out FILE]
                                     how large n can get (see bench.hpp)
   ============================================================= */
#include "hybrid/bench.hpp"
#include "hybrid/hybrid.hpp"
#include "common/json.hpp"
#include "app/server.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// Default web root: <exe dir>/../../web  (exe lives in loa-hybrid/cpp/bin)
std::string defaultWebRoot(const char* argv0) {
  std::string p(argv0);
  const std::size_t s = p.find_last_of("/\\");
  const std::string dir = s == std::string::npos ? "." : p.substr(0, s);
  return dir + "/../../web";
}

int runOnce(const std::string& src) {
  std::string text;
  if (src == "-") {
    std::ostringstream ss; ss << std::cin.rdbuf(); text = ss.str();
  } else {
    std::ifstream f(src.c_str(), std::ios::binary);
    if (!f) { std::fprintf(stderr, "Cannot open %s\n", src.c_str()); return 1; }
    std::ostringstream ss; ss << f.rdbuf(); text = ss.str();
  }
  try {
    json::Value report = runHybrid(json::parse(text));
    std::cout << report.dump() << "\n";
    return report.boolOr("ok", false) ? 0 : 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}

} // namespace

int main(int argc, char** argv) {
  int port = 8080;
  std::string web = defaultWebRoot(argv[0]);
  std::string file, out = "feasibility.json";
  int selftest = -1, runs = 5;
  bool feas = false;
  double budget = 10000;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--serve") continue;
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--web"  && i + 1 < argc) web  = argv[++i];
    else if (a == "--selftest") selftest = (i + 1 < argc && argv[i + 1][0] != '-') ? std::atoi(argv[++i]) : 300;
    else if (a == "--feasibility") feas = true;
    else if (a == "--runs"   && i + 1 < argc) runs   = std::atoi(argv[++i]);
    else if (a == "--budget" && i + 1 < argc) budget = std::atof(argv[++i]);
    else if (a == "--out"    && i + 1 < argc) out    = argv[++i];
    else if (a == "--help" || a == "-h") {
      std::printf("usage: loa_hybrid [--serve] [--port N] [--web DIR]\n"
                  "       loa_hybrid config.json | -\n"
                  "       loa_hybrid --selftest [N]\n"
                  "       loa_hybrid --feasibility [--runs R] [--budget MS] [--out FILE]\n");
      return 0;
    }
    else file = a;
  }
  if (selftest >= 0) return runSelfTest(selftest);
  if (feas) return runFeasibility(runs, budget, out);
  if (!file.empty()) return runOnce(file);
  return serve(port, web);
}
