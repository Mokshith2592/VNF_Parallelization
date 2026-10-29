/* =============================================================
   LOA Hybrid — Self-test and feasibility benchmark (see bench.hpp)
   ============================================================= */
#include "hybrid/bench.hpp"
#include "common/model.hpp"
#include "paper2/sca_vda.hpp"
#include "paper1/loa.hpp"
#include "hybrid/full.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>

namespace {

double U01(std::mt19937& rng) { return std::uniform_real_distribution<double>(0, 1)(rng); }
double Ur(std::mt19937& rng, double a, double b) { return a + (b - a) * U01(rng); }
int    Ui(std::mt19937& rng, int a, int b) { return a + static_cast<int>(rng() % static_cast<unsigned>(b - a + 1)); }

// Dependency levels: each VNF joins the current level with probability `par`
std::vector<int> randomLevels(std::mt19937& rng, int F, double par, int maxPer) {
  std::vector<int> lv;
  int l = 1, in = 0;
  for (int i = 0; i < F; i++) {
    if (i > 0 && !(U01(rng) < par && in < maxPer)) { l++; in = 0; }
    lv.push_back(l); in++;
  }
  return lv;
}

void connect(Config& c, std::mt19937& rng, double extra, double dLo, double dHi, double bwLo, double bwHi) {
  const int n = static_cast<int>(c.nodes.size());
  std::set<std::pair<int, int>> has;
  auto add = [&](int a, int b) {
    if (a == b || has.count({ std::min(a, b), std::max(a, b) })) return;
    has.insert({ std::min(a, b), std::max(a, b) });
    LinkCfg l; l.a = a; l.b = b; l.delay = std::round(Ur(rng, dLo, dHi) * 10) / 10; l.bw = Ur(rng, bwLo, bwHi);
    c.links.push_back(l);
  };
  auto dist = [&](int a, int b) { return std::hypot(c.nodes[a].x - c.nodes[b].x, c.nodes[a].y - c.nodes[b].y); };
  for (int i = 1; i < n; i++) {               // nearest-neighbour tree: connected
    int best = 0;
    for (int j = 1; j < i; j++) if (dist(i, j) < dist(i, best)) best = j;
    add(i, best);
  }
  for (int i = 0; i < n; i++)                 // Waxman extra links
    for (int j = i + 1; j < n; j++)
      if (U01(rng) < extra * std::exp(-dist(i, j) / (0.25 * std::sqrt(2.0)))) add(i, j);
}

// ─── Small random instances for the self-test ─────────────────
Config smallConfig(std::mt19937& rng) {
  Config c;
  const int n = Ui(rng, 3, 5), F = Ui(rng, 2, 5);
  for (int i = 0; i < n; i++) {
    NodeCfg nc; nc.name = std::string(1, static_cast<char>('A' + i));
    nc.cap = Ur(rng, 20, 120); nc.x = U01(rng); nc.y = U01(rng);
    nc.fault = U01(rng) < 0.3 ? Ur(rng, 0, 0.05) : 0;
    c.nodes.push_back(nc);
  }
  connect(c, rng, 0.6, 1, 15, 50, 300);
  const std::vector<int> lv = randomLevels(rng, F, 0.6, 4);
  for (int i = 0; i < F; i++) {
    VnfCfg v; v.name = "f" + std::to_string(i + 1); v.level = lv[i];
    v.proc = std::round(Ur(rng, 0, 20)); v.dem = std::round(Ur(rng, 10, 40));
    v.fault = U01(rng) < 0.3 ? Ur(rng, 0, 0.05) : 0;
    v.type = (i > 0 && U01(rng) < 0.15) ? c.vnfs[Ui(rng, 0, i - 1)].type : v.name;   // sometimes a repeated type
    c.vnfs.push_back(v);
  }
  c.src = Ui(rng, 0, n - 1); c.dst = Ui(rng, 0, n - 1);
  c.trep = std::round(Ur(rng, 0, 6)); c.tint = std::round(Ur(rng, 0, 6));
  const double pk[] = { 1, 1.5, 2 };
  c.pkt = pk[Ui(rng, 0, 2)];
  if (U01(rng) < 0.3) { c.rmin = Ur(rng, 0.85, 0.97); c.alpha = Ur(rng, 0, 0.04); }
  if (U01(rng) < 0.5) c.breq = Ur(rng, 40, 180);
  c.qcpu = Ur(rng, 0, 40); c.instCap = 100; c.wBw = U01(rng);
  for (int k = Ui(rng, 0, 3); k > 0; k--) {
    InstanceCfg ic; ic.node = Ui(rng, 0, n - 1); ic.type = c.vnfs[Ui(rng, 0, F - 1)].type; ic.residual = Ur(rng, 0, 100);
    c.instances.push_back(ic);
  }
  c.tmax = U01(rng) < 0.2 ? Ur(rng, 40, 120) : 1e9;
  c.kmax = 2048; c.candCap = 30000; c.fullMs = 600000;
  c.allIsfg = U01(rng) < 0.85;
  return c;
}

// ─── Paper 1's experimental setting (Section V-A) ─────────────
// tight = false: node capacity U[100,300] as in the paper; one request fits almost anywhere.
// tight = true:  node capacity U[40,80], so a node holds one or two VNFs (a network already
//                loaded by earlier requests). This is the hard case for any exact search.
Config paperConfig(std::mt19937& rng, int n, int F, bool tight) {
  Config c;
  for (int i = 0; i < n; i++) {
    NodeCfg nc; nc.name = "n" + std::to_string(i); nc.x = U01(rng); nc.y = U01(rng);
    nc.cap = tight ? Ur(rng, 40, 80) : Ur(rng, 100, 300);
    c.nodes.push_back(nc);
  }
  connect(c, rng, 0.25, 10, 15, 100, 300);
  const std::vector<int> lv = randomLevels(rng, F, 0.5, 5);
  for (int i = 0; i < F; i++) {
    VnfCfg v; v.name = "f" + std::to_string(i + 1); v.type = v.name; v.level = lv[i];
    v.proc = Ur(rng, 0.045, 0.2);           // μ_n, ms per packet
    v.dem = Ur(rng, 20, 50);
    c.vnfs.push_back(v);
  }
  c.src = Ui(rng, 0, n - 1);
  do c.dst = Ui(rng, 0, n - 1); while (c.dst == c.src);
  c.pkt = Ur(rng, 50, 100);                 // l_u, packets
  c.trep = c.tint = 0.02;                   // ms per packet (paper 1 leaves τ open)
  c.breq = Ur(rng, 30, 60); c.qcpu = Ur(rng, 10, 30); c.instCap = 100;
  c.tmax = 1e6;
  c.kmax = 256; c.candCap = 2000;           // paper-1 search limits used in our experiments
  return c;
}

// The config as the website would send it (failing self-test cases are saved with it)
json::Value configJson(const Config& c) {
  json::Value o = json::Value::object(), nodes = json::Value::array(), links = json::Value::array(), vnfs = json::Value::array(), inst = json::Value::array();
  for (const auto& n : c.nodes) { json::Value x = json::Value::object(); x.set("name", n.name); x.set("cap", n.cap); x.set("fault", n.fault); x.set("x", n.x * 500 + 50); x.set("y", n.y * 260 + 40); nodes.push(x); }
  for (const auto& l : c.links) { json::Value x = json::Value::object(); x.set("a", l.a); x.set("b", l.b); x.set("delay", l.delay); x.set("bw", l.bw); links.push(x); }
  for (const auto& v : c.vnfs) { json::Value x = json::Value::object(); x.set("name", v.name); x.set("type", v.type); x.set("level", v.level); x.set("proc", v.proc); x.set("dem", v.dem); x.set("fault", v.fault); vnfs.push(x); }
  for (const auto& i : c.instances) { json::Value x = json::Value::object(); x.set("node", i.node); x.set("type", i.type); x.set("residual", i.residual); inst.push(x); }
  o.set("nodes", nodes); o.set("links", links); o.set("vnfs", vnfs); o.set("instances", inst);
  o.set("src", c.src); o.set("dst", c.dst); o.set("trep", c.trep); o.set("tint", c.tint); o.set("pkt", c.pkt); o.set("tmax", c.tmax);
  o.set("rmin", c.rmin); o.set("alpha", c.alpha); o.set("breq", c.breq); o.set("qcpu", c.qcpu); o.set("instCap", c.instCap); o.set("wBw", c.wBw);
  o.set("isfg", c.allIsfg ? "all" : "staged"); o.set("kmax", static_cast<double>(c.kmax)); o.set("candCap", static_cast<double>(c.candCap)); o.set("fullMs", c.fullMs);
  return o;
}

bool sameLat(const Scheme& a, const Scheme& b) {
  return a.ok == b.ok && (!a.ok || std::fabs(a.lat - b.lat) < 1e-6);
}

} // namespace

// ─── Self-test ────────────────────────────────────────────────

int runSelfTest(int instances) {
  int fails = 0;
  const Config* cur = nullptr;
  int savedFor = -1, it = -1;
  auto fail = [&](const std::string& what) {
    fails++; std::printf("  FAIL %s\n", what.c_str());
    if (cur && savedFor != it) {   // keep the failing config so it can be run on its own
      savedFor = it;
      const std::string fn = "selftest_fail_" + std::to_string(it) + ".json";
      std::ofstream(fn.c_str()) << configJson(*cur).dump() << "\n";
      std::printf("       config saved to %s\n", fn.c_str());
    }
  };

  // 1. Structure counts: series-parallel (N-free) posets, checked by brute force over all
  //    labeled posets (tests/count_sp.py); ordered set partitions (Fubini numbers)
  const double spCount[] = { 1, 3, 19, 195, 2791 }, stCount[] = { 1, 3, 13, 75, 541 };
  for (int m = 1; m <= 5; m++) {
    std::vector<int> items; for (int i = 0; i < m; i++) items.push_back(i);
    const double a = static_cast<double>(layerStructures(items, true).size());
    const double b = static_cast<double>(layerStructures(items, false).size());
    std::printf("level of %d VNFs: %.0f I-SFGs, %.0f stage sequences\n", m, a, b);
    if (a != spCount[m - 1] || b != stCount[m - 1]) fail("structure count for " + std::to_string(m) + " VNFs");
  }

  std::mt19937 rng(20261028u);
  int ran = 0, skipped = 0, loaProofs = 0, loaProofWrong = 0, roaProofs = 0, roaProofWrong = 0, loaSub = 0, loaFound = 0;
  double fullMs = 0, bruteMs = 0;
  for (it = 0; it < instances; it++) {
    const Config c = smallConfig(rng);
    cur = &c;
    if (!validateConfig(c).empty()) { skipped++; continue; }
    const Model M = buildModel(c);
    if (M.totalPaths.isZero()) { skipped++; continue; }
    const BruteResult B = runBrute(M, 4e5);
    if (!B.ran) { skipped++; continue; }
    ran++;
    bruteMs += B.ms;
    const std::string tag = "instance " + std::to_string(it);

    const FullResult fl = runFull(M, Objective::Latency, nullptr);
    const FullResult fr = runFull(M, Objective::Resource, nullptr);
    fullMs += fl.ms + fr.ms;
    if (!fl.complete || !fr.complete) fail(tag + ": full search did not finish");
    if (!sameLat(fl.best, B.best)) fail(tag + ": latency " + fmt(fl.best.lat) + " vs brute force " + fmt(B.best.lat));
    if (fl.best.ok && std::fabs(fl.best.lat - fl.bestCost) > 1e-6) fail(tag + ": incremental latency differs from evalScheme");
    if (fr.best.ok != B.bestRes.ok ||
        (fr.best.ok && (std::fabs(fr.best.res.norm - B.bestRes.res.norm) > 1e-9 || std::fabs(fr.best.lat - B.bestRes.lat) > 1e-6)))
      fail(tag + ": resource " + fmt(fr.best.res.norm) + "/" + fmt(fr.best.lat) + " vs brute force " + fmt(B.bestRes.res.norm) + "/" + fmt(B.bestRes.lat));

    // Seeded run must reach the same optimum; heuristics never beat it
    const LoaResult loa = runLOA(M, nullptr);
    const FullResult fs = runFull(M, Objective::Latency, loa.best.ok ? &loa.best : nullptr);
    if (!sameLat(fs.best, B.best)) fail(tag + ": seeded full search " + fmt(fs.best.lat) + " vs " + fmt(B.best.lat));
    if (loa.best.ok && (!B.best.ok || loa.best.lat < B.best.lat - 1e-6)) fail(tag + ": LOA beats the optimum");
    const VdaResult vda = runVDA(M, runSCA(M).g);
    if (vda.scheme.ok && (!B.best.ok || vda.scheme.lat < B.best.lat - 1e-6)) fail(tag + ": VDA beats the optimum");
    if (loa.best.ok) { loaFound++; if (loa.best.lat > B.best.lat + 1e-6) loaSub++; }
    const bool loaProof = loa.stop != Stop::KLimit && !loa.st.capped && loa.st.stabilityRemoved == 0;
    if (loaProof && B.best.ok) { loaProofs++; if (!sameLat(loa.best, B.best)) loaProofWrong++; }
    if (c.roa) {
      const LoaResult roa = runROA(M, nullptr);
      if (roa.best.ok && B.bestRes.ok && roa.best.res.norm < B.bestRes.res.norm - 1e-9) fail(tag + ": ROA beats the optimum");
      if (roa.stop != Stop::KLimit && !roa.st.capped && roa.st.stabilityRemoved == 0 && B.bestRes.ok) {
        roaProofs++;
        if (!roa.best.ok || std::fabs(roa.best.res.norm - B.bestRes.res.norm) > 1e-9) roaProofWrong++;
      }
    }
    // Placement-free head of an I-SFG = sum of its level scores (what SCA and the bound rely on)
    if (B.best.ok) {
      double sum = 0;
      for (int li = 0; li < M.L; li++) sum += M.structScore[li][B.best.g.pick[li]];
      if (std::fabs(placementFreeTails(M, B.best.g).head - sum) > 1e-6) fail(tag + ": placement-free head differs from the level scores");
    }
  }
  std::printf("\n%d instances checked against brute force, %d skipped (too large / disconnected)\n", ran, skipped);
  std::printf("time: brute force %.0f ms, full search %.0f ms (both objectives)\n", bruteMs, fullMs);
  std::printf("LOA (paper 1) found a scheme in %d, above the optimum in %d\n", loaFound, loaSub);
  std::printf("LOA 'proven' stops: %d, of which not optimal: %d\n", loaProofs, loaProofWrong);
  std::printf("ROA 'proven' stops: %d, of which not optimal: %d\n", roaProofs, roaProofWrong);
  std::printf(fails ? "\nSELFTEST FAILED: %d check(s)\n" : "\nSELFTEST OK\n", fails);
  return fails ? 1 : 0;
}

// ─── Feasibility benchmark ────────────────────────────────────

int runFeasibility(int runs, double budgetMs, const std::string& out) {
  const std::vector<int> vnfGrid  = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12 };
  const std::vector<int> nodeGrid = { 6, 10, 15, 20, 30, 60 };
  auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0.0 : s / v.size(); };
  auto mx = [](const std::vector<double>& v) { double s = 0; for (double x : v) s = std::max(s, x); return s; };
  json::Value scenarios = json::Value::array();

  for (int sc = 0; sc < 2; sc++) {
    const bool tight = sc == 1;
    json::Value points = json::Value::array();
    std::map<int, int> maxInteractive, maxFeasible;
    std::printf("== scenario: %s\n", tight ? "tight capacity" : "paper 1 setting");
    for (int n : nodeGrid) {
      bool stop = false;
      for (int F : vnfGrid) {
        json::Value p = json::Value::object();
        p.set("vnfs", F); p.set("nodes", n);
        if (stop) { p.set("verdict", "not run"); points.push(p); continue; }
        std::vector<double> ms, nodes, leaves, space, isfg, brute, loaMs, loaGap, vdaGap;
        int completed = 0, loaSubopt = 0, loaFound = 0, loaMiss = 0, feasible = 0;
        for (int r = 0; r < runs; r++) {
          std::mt19937 rng(static_cast<unsigned>(7919u * n + 104729u * F + 15485863u * (r + 1) + 2u * sc));
          Config c = paperConfig(rng, n, F, tight);
          c.fullMs = budgetMs;
          const Model M = buildModel(c);
          if (M.totalPaths.isZero()) continue;
          double t = nowMs();
          const LoaResult loa = runLOA(M, nullptr);
          loaMs.push_back(nowMs() - t);
          const VdaResult vda = runVDA(M, runSCA(M).g);
          const Scheme* seed = loa.best.ok ? &loa.best : nullptr;
          if (vda.scheme.ok && (!seed || vda.scheme.lat < seed->lat)) seed = &vda.scheme;
          const FullResult fr = runFull(M, Objective::Latency, seed);
          ms.push_back(fr.ms); nodes.push_back(fr.nodes); leaves.push_back(fr.leaves);
          space.push_back(fr.space); isfg.push_back(fr.isfgTotal);
          // Plain enumeration estimate: measured time of one evaluation × size of the space
          {
            std::mt19937 r2(1);
            std::vector<Isfg> gs;
            std::vector<int> pick(M.L), hosts(M.V.size());
            for (int k = 0; k < 16; k++) {
              for (int li = 0; li < M.L; li++) pick[li] = static_cast<int>(r2() % M.structs[li].size());
              gs.push_back(makeIsfg(M, pick));
            }
            const int reps = 4000;
            double sink = 0;
            const double t1 = nowMs();
            for (int k = 0; k < reps; k++) {
              for (auto& h : hosts) h = static_cast<int>(r2() % M.n);
              sink += evalScheme(M, hosts, gs[k % gs.size()]).lat;
            }
            const double per = (nowMs() - t1) / reps;
            brute.push_back(sink == -1 ? 0 : per * fr.space);   // `sink` keeps the loop from being optimised away
          }
          if (fr.complete) {
            completed++;
            if (fr.best.ok) {
              feasible++;
              if (loa.best.ok) {
                loaFound++;
                loaGap.push_back((loa.best.lat - fr.best.lat) / fr.best.lat * 100);
                if (loa.best.lat > fr.best.lat + 1e-6) loaSubopt++;
              } else loaMiss++;
              if (vda.scheme.ok) vdaGap.push_back((vda.scheme.lat - fr.best.lat) / fr.best.lat * 100);
            }
          }
        }
        const int got = static_cast<int>(ms.size());
        p.set("runs", got); p.set("completed", completed); p.set("feasible", feasible);
        p.set("completeFrac", got ? static_cast<double>(completed) / got : 0.0);
        p.set("msMean", mean(ms)); p.set("msMax", mx(ms));
        p.set("nodesMean", mean(nodes)); p.set("leavesMean", mean(leaves));
        p.set("spaceMean", mean(space)); p.set("isfgMean", mean(isfg));
        p.set("bruteMsEst", mean(brute));
        p.set("loaMsMean", mean(loaMs));
        p.set("loaGapPct", mean(loaGap)); p.set("loaGapMaxPct", mx(loaGap));
        p.set("loaSubopt", loaSubopt); p.set("loaFound", loaFound); p.set("loaMiss", loaMiss);
        p.set("vdaGapPct", mean(vdaGap));
        std::string verdict;
        if (got && completed == got && mx(ms) <= 1000) verdict = "interactive";
        else if (got && completed == got) verdict = "feasible";
        else if (completed > 0) verdict = "partial";
        else verdict = "infeasible";
        p.set("verdict", verdict);
        if (verdict == "interactive") maxInteractive[n] = F;
        if (verdict == "interactive" || verdict == "feasible") maxFeasible[n] = F;
        if (completed == 0) stop = true;   // larger chains on this network will not finish either
        std::printf("nodes %3d  VNFs %d  complete %d/%d  full %8.1f ms (max %8.1f)  brute est %.3g ms  LOA %8.1f ms, gap %5.2f%% (%d/%d suboptimal, %d missed)  %s\n",
                    n, F, completed, got, mean(ms), mx(ms), mean(brute), mean(loaMs), mean(loaGap), loaSubopt, loaFound, loaMiss, verdict.c_str());
        std::fflush(stdout);
        points.push(p);
      }
    }
    json::Value S = json::Value::object();
    S.set("id", tight ? "tight" : "paper");
    S.set("title", tight ? "Tight capacity: node capacity U[40,80], one or two VNFs per node" : "Paper 1 setting: node capacity U[100,300]");
    S.set("points", points);
    json::Value lim = json::Value::array();
    for (int n : nodeGrid) {
      json::Value x = json::Value::object();
      x.set("nodes", n);
      x.set("maxInteractive", maxInteractive.count(n) ? maxInteractive[n] : 0);
      x.set("maxFeasible", maxFeasible.count(n) ? maxFeasible[n] : 0);
      lim.push(x);
    }
    S.set("limits", lim);
    scenarios.push(S);
  }

  json::Value o = json::Value::object();
  o.set("runs", runs);
  o.set("budgetMs", budgetMs);
  json::Value vg = json::Value::array(); for (int F : vnfGrid) vg.push(F);
  json::Value ng = json::Value::array(); for (int n : nodeGrid) ng.push(n);
  o.set("vnfGrid", vg); o.set("nodeGrid", ng);
  o.set("setting", "Paper 1, Section V-A: Waxman topology, link delay U[10,15] ms, VNF demand U[20,50], "
                   "mu U[0.045,0.2] ms/packet, l_u U[50,100] packets, tau_rep = tau_int = 0.02 ms/packet, parallelism 0.5 "
                   "(at most 5 VNFs per level), all I-SFGs. LOA limits k <= 256, 2000 candidates per anchor. "
                   "Full search seeded with the better of LOA and VDA, latency objective.");
  o.set("scenarios", scenarios);
  // Structures of one level of m VNFs: every I-SFG vs stage sequences only
  json::Value counts = json::Value::array();
  for (int m = 1; m <= 6; m++) {
    std::vector<int> items; for (int i = 0; i < m; i++) items.push_back(i);
    json::Value x = json::Value::object();
    x.set("vnfs", m);
    x.set("isfg", static_cast<double>(layerStructures(items, true).size()));
    x.set("staged", static_cast<double>(layerStructures(items, false).size()));
    counts.push(x);
  }
  o.set("structureCounts", counts);
  std::ofstream f(out.c_str(), std::ios::binary);
  if (!f) { std::fprintf(stderr, "Cannot write %s\n", out.c_str()); return 1; }
  f << o.dump() << "\n";
  std::printf("wrote %s\n", out.c_str());
  return 0;
}
