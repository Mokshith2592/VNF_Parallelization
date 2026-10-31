/* =============================================================
   LOA Hybrid — Experiment runner (reproduces paper 1's Section V)
   Separate executable: bin/loa_experiments.exe

   Setting (paper 1, Section V-A, defaults in brackets):
     GT-ITM-style random topology (Waxman), N nodes            [60]
     link delay U[10,15] ms, link bandwidth U[100, bwMax] Mbps   [300]
     node capacity U[100, capMax] units                          [300]
     VNF demand c_n U[20,50], per-packet processing μ_n U[0.045,0.2] ms
     request: data l_u U[50,100] packets, bandwidth b_u U[30,60] Mbps
     SFC length                                                  [7]
     parallelism p (prob. a VNF joins the current level)         [0.5]
     requests embedded one after another                         [30]
     10 runs per point
   Each algorithm embeds the same request sequence on its own copy
   of the network, so residual capacity / bandwidth evolve per algorithm.

   VNF instances are shared between requests (paper 1, Section III-B):
   an instance of capacity C_n^max = 100 serves requests with load
   q_u^cpu ~ U[10,30] until it is full; only new instances take node capacity.
   Not reproducible here (documented in RESULTS.md): the DNF / ParaDSP / PODPR baselines.
   ============================================================= */
#include "common/model.hpp"
#include "paper2/sca_vda.hpp"
#include "paper1/loa.hpp"
#include "hybrid/full.hpp"
#include "hybrid/sync.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

long g_cand = 20000, g_kmax = 4096;
int g_reqOverride = 0, g_sfcOverride = 0;
bool g_verbose = false;
bool g_diag = false;

// --paired: every request is solved by plain LOA, the seeded hybrid and the full search on
// the same model (same residual network). The network then evolves with the best scheme.
bool g_paired = false;
double g_fullMs = 2000;
struct PairStats {
  int reqs = 0, loaFound = 0, bestFound = 0, complete = 0, both = 0, better = 0, worse = 0;
  int loaMissed = 0, hybMissed = 0, fallbacks = 0, fallbackHelped = 0;
  double loaSum = 0, bestSum = 0, maxGainPct = 0, loaMs = 0, pipeMs = 0, pipeMaxMs = 0;
  int syncN = 0;                                // requests whose scheme has a skewed merge
  double skewB = 0, skewA = 0, bufB = 0, bufA = 0, latB = 0, latA = 0, cpuB = 0, cpuA = 0;
} g_pair;
bool g_allIsfg = true;   // --staged: only stage sequences inside a level (the earlier model)

// ─── Parameters of one experiment point ───────────────────────
struct Params {
  int    nodes = 60;
  double capMax = 300, bwMax = 300;
  int    sfcLen = 7;
  double par = 0.5;
  int    requests = 30;
  bool   heavyTail = false;
  double tauRep = 0.02, tauInt = 0.02;   // ms per packet (paper leaves τ unspecified)
  int    catalog = 12;                   // VNF types in the system
  double instCap = 100;                  // C_n^max (paper leaves it unspecified)
  double wBw = 0.5;                      // α; β = 1 − α (paper default 0.5 / 0.5)
};

struct Topo {
  std::vector<double> x, y, cap;
  std::vector<LinkCfg> links;
  std::vector<double> bw;
};

struct VnfType { double c, mu; };

struct Request {
  int src, dst;
  double l, b, qcpu;
  std::vector<int> types;
  std::vector<int> levels;
};

// ─── Generators ───────────────────────────────────────────────

/** Waxman graph (GT-ITM "flat random" model) made connected by a nearest-neighbour tree. */
Topo genTopo(const Params& P, std::mt19937& rng) {
  std::uniform_real_distribution<double> U(0, 1);
  Topo t;
  const int n = P.nodes;
  for (int i = 0; i < n; i++) { t.x.push_back(U(rng)); t.y.push_back(U(rng)); t.cap.push_back(100 + U(rng) * (P.capMax - 100)); }
  std::map<std::pair<int, int>, bool> has;
  auto add = [&](int a, int b) {
    if (a == b) return;
    auto k = std::make_pair(std::min(a, b), std::max(a, b));
    if (has[k]) return;
    has[k] = true;
    LinkCfg l; l.a = k.first; l.b = k.second; l.delay = 10 + U(rng) * 5;
    t.links.push_back(l);
    t.bw.push_back(100 + U(rng) * (P.bwMax - 100));
  };
  auto dist = [&](int a, int b) { return std::hypot(t.x[a] - t.x[b], t.y[a] - t.y[b]); };
  for (int i = 1; i < n; i++) {             // connectivity
    int best = 0;
    for (int j = 1; j < i; j++) if (dist(i, j) < dist(i, best)) best = j;
    add(i, best);
  }
  const double alpha = 0.25, beta = 0.25, Lmax = std::sqrt(2.0);
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (U(rng) < alpha * std::exp(-dist(i, j) / (beta * Lmax))) add(i, j);
  return t;
}

std::vector<VnfType> genCatalog(const Params& P, std::mt19937& rng) {
  std::uniform_real_distribution<double> U(0, 1);
  std::vector<VnfType> c;
  for (int i = 0; i < P.catalog; i++) c.push_back({ 20 + U(rng) * 30, 0.045 + U(rng) * 0.155 });
  return c;
}

std::vector<Request> genRequests(const Params& P, std::mt19937& rng) {
  std::uniform_real_distribution<double> U(0, 1);
  std::vector<Request> R;
  for (int r = 0; r < P.requests; r++) {
    Request q;
    q.src = static_cast<int>(U(rng) * P.nodes) % P.nodes;
    do q.dst = static_cast<int>(U(rng) * P.nodes) % P.nodes; while (q.dst == q.src);
    if (P.heavyTail) {
      // Pareto (shape 1.5): most requests small, a few very large
      const double s = std::pow(1.0 - U(rng), -1.0 / 1.5);
      q.l = std::min(50.0 * s, 400.0);
      q.b = std::min(30.0 * s, 150.0);
    } else {
      q.l = 50 + U(rng) * 50;
      q.b = 30 + U(rng) * 30;
    }
    q.qcpu = 10 + U(rng) * 20;
    std::vector<int> ids(P.catalog);
    for (int i = 0; i < P.catalog; i++) ids[i] = i;
    std::shuffle(ids.begin(), ids.end(), rng);
    q.types.assign(ids.begin(), ids.begin() + P.sfcLen);
    int lvl = 1, inLvl = 0;
    for (int i = 0; i < P.sfcLen; i++) {
      if (i > 0 && !(U(rng) < P.par && inLvl < 5)) { lvl++; inLvl = 0; }
      q.levels.push_back(lvl); inLvl++;
    }
    R.push_back(q);
  }
  return R;
}

// ─── One algorithm over one request sequence ──────────────────

enum Algo { A_LOA, A_HYBRID, A_ROA, A_HROA, A_VDA, A_SERIAL, A_PARALLEL, A_COUNT };
const char* ALGO_NAME[A_COUNT] = { "LOA", "Hybrid", "ROA", "Hybrid-ROA", "VDA", "Serial*", "Parallel*" };

struct RunStats {
  double delaySum = 0, bwSum = 0, cpuSum = 0, resSum = 0;
  int accepted = 0, total = 0;
  double ms = 0, jain = 0, cv = 0;
  long proven = 0, limited = 0;   // limited: search stopped on kmax or the candidate cap
};

RunStats runSequence(Algo algo, const Params& P, const Topo& T0, const std::vector<VnfType>& cat,
                     const std::vector<Request>& R) {
  RunStats st;
  std::vector<double> cap = T0.cap, bw = T0.bw;
  std::vector<InstanceCfg> inst;   // running VNF instances, shared by later requests

  for (const auto& q : R) {
    st.total++;
    // Config for this request on the current residual network
    Config c;
    for (int i = 0; i < P.nodes; i++) {
      NodeCfg nc; nc.name = "n" + std::to_string(i); nc.cap = cap[i]; nc.x = T0.x[i]; nc.y = T0.y[i];
      c.nodes.push_back(nc);
    }
    std::vector<int> linkIdx;
    for (std::size_t e = 0; e < T0.links.size(); e++)
      if (bw[e] >= q.b - EPS) { c.links.push_back(T0.links[e]); linkIdx.push_back(static_cast<int>(e)); }
    for (std::size_t i = 0; i < q.types.size(); i++) {
      VnfCfg v; v.name = "f" + std::to_string(i + 1); v.type = "t" + std::to_string(q.types[i]); v.level = q.levels[i];
      v.proc = cat[q.types[i]].mu; v.dem = cat[q.types[i]].c;   // μ_n per packet; × l_u via pkt
      c.vnfs.push_back(v);
    }
    c.src = q.src; c.dst = q.dst;
    c.trep = P.tauRep; c.tint = P.tauInt; c.pkt = q.l; c.tmax = 1e6;
    c.allIsfg = g_allIsfg; c.full = false; c.fullMs = g_fullMs;
    c.kmax = g_kmax; c.candCap = g_cand;
    c.seed = algo == A_HYBRID || algo == A_HROA; c.order = true; c.compare = false; c.roa = false;
    c.breq = q.b; c.qcpu = q.qcpu; c.instCap = P.instCap; c.wBw = P.wBw;
    c.instances = inst;
    const std::string bad = validateConfig(c);
    if (!bad.empty()) {
      // say so once instead of silently rejecting every request
      static bool warned = false;
      if (!warned) { std::printf("  WARNING: request rejected by validateConfig: %s\n", bad.c_str()); warned = true; }
      continue;
    }

    const double t0 = nowMs();
    Model M = buildModel(c);
    if (M.totalPaths.isZero()) { st.ms += nowMs() - t0; continue; }   // src/dst cut off by bandwidth
    Scheme s;
    if (algo == A_LOA || algo == A_HYBRID) {
      Scheme seed;
      if (algo == A_HYBRID) {
        ScaResult sca = runSCA(M);
        VdaResult v = runVDA(M, sca.g);
        seed = v.scheme;
      }
      LoaResult r = runLOA(M, algo == A_HYBRID && seed.ok ? &seed : nullptr);
      s = r.best;
      if (g_paired && algo == A_HYBRID) {
        // Pipeline: VDA seed, full search, LOA (r, already seeded) only if the search ran out of time
        double t1 = nowMs();
        const LoaResult plain = runLOA(M, nullptr);
        g_pair.loaMs += nowMs() - t1;
        t1 = nowMs();
        const FullResult fr = runFull(M, Objective::Latency, seed.ok ? &seed : nullptr);
        double pipeMs = nowMs() - t1;
        Scheme best = fr.best;
        if (!fr.complete) {
          g_pair.fallbacks++;
          pipeMs += r.st.ms;
          if (r.best.ok && (!best.ok || r.best.lat < best.lat - 1e-6)) { best = r.best; g_pair.fallbackHelped++; }
        }
        g_pair.pipeMs += pipeMs; g_pair.pipeMaxMs = std::max(g_pair.pipeMaxMs, pipeMs);
        g_pair.reqs++;
        if (plain.best.ok) g_pair.loaFound++;
        if (best.ok) g_pair.bestFound++;
        if (fr.complete) g_pair.complete++;
        if (best.ok && !plain.best.ok) g_pair.loaMissed++;
        if (plain.best.ok && !best.ok) g_pair.hybMissed++;
        if (best.ok && plain.best.ok) {
          g_pair.both++;
          g_pair.loaSum += plain.best.lat; g_pair.bestSum += best.lat;
          if (best.lat < plain.best.lat - 1e-6) {
            g_pair.better++;
            g_pair.maxGainPct = std::max(g_pair.maxGainPct, (plain.best.lat - best.lat) / plain.best.lat * 100);
          } else if (best.lat > plain.best.lat + 1e-6) g_pair.worse++;
        }
        if (best.ok) {
          s = best;
          const SyncResult y = syncMerges(M, best);
          if (y.maxSkewBefore > 1e-6) {
            g_pair.syncN++;
            g_pair.skewB += y.maxSkewBefore; g_pair.skewA += y.maxSkewAfter;
            g_pair.bufB += y.maxBufBefore;   g_pair.bufA += y.maxBufAfter;
            g_pair.latB += y.latBefore;      g_pair.latA += y.latAfter;
            g_pair.cpuB += y.cpuBefore;      g_pair.cpuA += y.cpuAfter;
          }
        }
      }
      if (g_verbose) std::printf("    [%s] L=%d rounds=%d k=%ld paths=%ld anchors=%ld cand=%.0f removed=%ld stop=%s lat=%.1f ms=%.0f\n", ALGO_NAME[algo], M.L, r.st.rounds, r.st.finalK, r.st.pathsGenerated, r.st.anchors, r.st.candidates, r.st.stabilityRemoved, stopName(r.stop), s.lat, r.st.ms);
      if (r.stop != Stop::KLimit && !r.st.capped && r.st.stabilityRemoved == 0) st.proven++;
      if (r.stop == Stop::KLimit || r.st.capped) st.limited++;
    } else if (algo == A_ROA || algo == A_HROA) {
      Scheme seed;
      if (algo == A_HROA) {
        VdaResult v = runVDA(M, runSCA(M).g);
        if (v.scheme.ok) seed = makeScheme(M, v.scheme.hosts, v.scheme.g, "VDA", true);
      }
      LoaResult r = runROA(M, algo == A_HROA && seed.ok ? &seed : nullptr);
      s = r.best;
      if (g_diag && algo == A_HROA) {
        LoaResult l = runLOA(M, nullptr);
        Scheme lh = l.best.ok ? makeScheme(M, l.best.hosts, l.best.g, "LOA", true) : Scheme();
        std::printf("    diag: ROA cost %.3f (bw %.0f cpu %.0f lat %.0f, %s) | LOA-scheme as ROA cost %.3f (bw %.0f cpu %.0f) | LOA own routing bw %.0f | improved=%d stop=%s\n",
          r.bestCost, s.res.bw, s.res.cpu, s.lat, stopName(r.stop), lh.ok ? lh.res.norm : -1.0, lh.res.bw, lh.res.cpu, l.best.res.bw, (int)r.improvedSeed, stopName(r.stop));
      }
      if (g_verbose) std::printf("    [%s] L=%d rounds=%d k=%ld paths=%ld infeas=%ld anchors=%ld cand=%.0f removed=%ld stop=%s cost=%.3f ms=%.0f\n", ALGO_NAME[algo], M.L, r.st.rounds, r.st.finalK, r.st.pathsGenerated, r.st.infeasiblePaths, r.st.anchors, r.st.candidates, r.st.stabilityRemoved, stopName(r.stop), r.bestCost, r.st.ms);
      if (r.stop != Stop::KLimit && !r.st.capped && r.st.stabilityRemoved == 0) st.proven++;
      if (r.stop == Stop::KLimit || r.st.capped) st.limited++;
    } else if (algo == A_VDA) {
      s = runVDA(M, runSCA(M).g).scheme;
    } else {
      std::vector<int> pick(M.L);
      for (int li = 0; li < M.L; li++) pick[li] = algo == A_SERIAL ? serialStructure(M, li) : parallelStructure(M, li);
      s = runVDA(M, makeIsfg(M, pick)).scheme;
    }
    st.ms += nowMs() - t0;
    if (!s.ok) continue;

    // Accept: charge node capacity (new instances only), instance load, and link bandwidth
    st.accepted++;
    st.delaySum += s.lat;
    st.resSum += s.res.norm;   // α·C̃bw + β·C̃cpu with the request's fixed reference scale
    std::vector<double> newUse; std::vector<char> reuse;
    st.cpuSum += placeInstances(M, s.hosts, newUse, reuse);
    for (int h = 0; h < P.nodes; h++) cap[h] -= newUse[h];
    for (std::size_t f = 0; f < s.hosts.size(); f++) {
      const std::string& ty = M.V[f].type;
      int pick = -1;
      for (std::size_t i = 0; i < inst.size(); i++)
        if (inst[i].node == s.hosts[f] && inst[i].type == ty && inst[i].residual >= q.qcpu - EPS &&
            (pick < 0 || inst[i].residual > inst[pick].residual)) pick = static_cast<int>(i);
      if (reuse[f] && pick >= 0) inst[pick].residual -= q.qcpu;
      else { InstanceCfg ic; ic.node = s.hosts[f]; ic.type = ty; ic.residual = P.instCap - q.qcpu; inst.push_back(ic); }
    }
    std::map<std::pair<int, int>, int> pairToLink;
    for (std::size_t k = 0; k < linkIdx.size(); k++) {
      const LinkCfg& l = T0.links[linkIdx[k]];
      pairToLink[{ std::min(l.a, l.b), std::max(l.a, l.b) }] = linkIdx[k];
    }
    std::vector<char> used(T0.links.size(), 0);
    auto charge = [&](int a, int b) {
      std::vector<int> r = route(M, a, b, s.hop);
      for (std::size_t i = 0; i + 1 < r.size(); i++) {
        auto it = pairToLink.find({ std::min(r[i], r[i + 1]), std::max(r[i], r[i + 1]) });
        if (it != pairToLink.end()) used[it->second] = 1;
      }
    };
    for (int f : s.g.topo) for (int p : s.g.pred[f]) charge(p < 0 ? c.src : s.hosts[p], s.hosts[f]);
    for (int p : s.g.last) charge(s.hosts[p], c.dst);
    for (std::size_t e = 0; e < used.size(); e++) if (used[e]) { bw[e] -= q.b; st.bwSum += q.b; }
  }

  // Jain's fairness index of node utilisation at the end
  double s1 = 0, s2 = 0;
  for (int i = 0; i < P.nodes; i++) { const double u = (T0.cap[i] - cap[i]) / T0.cap[i]; s1 += u; s2 += u * u; }
  st.jain = s2 > 0 ? s1 * s1 / (P.nodes * s2) : 1;
  const double mu = s1 / P.nodes, var = s2 / P.nodes - mu * mu;
  st.cv = mu > 0 ? std::sqrt(std::max(0.0, var)) / mu : 0;
  return st;
}

// ─── Experiment definitions ───────────────────────────────────

struct Experiment {
  std::string id, title, xlabel;
  std::vector<double> xs;
  std::function<void(Params&, double)> apply;
};

std::vector<Experiment> experiments() {
  return {
    { "capacity",  "Impact of computing resources at the nodes (Fig. 6a)", "Max node capacity",
      { 100, 150, 200, 250, 300 }, [](Params& p, double x) { p.capMax = x; } },
    { "bandwidth", "Impact of link bandwidth (Fig. 7a)", "Max link bandwidth (Mbps)",
      { 100, 150, 200, 250, 300 }, [](Params& p, double x) { p.bwMax = x; } },
    { "requests",  "Impact of number of requests (Fig. 8a)", "Number of requests",
      { 10, 20, 30, 40, 50 }, [](Params& p, double x) { p.requests = static_cast<int>(x); } },
    { "netsize",   "Impact of network size (Fig. 9a)", "Number of edge nodes",
      // paper 1 uses 20..100; 10, 30, 50, 70, 90 and 120..300 are extra points of ours
      { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 120, 150, 200, 250, 300 }, [](Params& p, double x) { p.nodes = static_cast<int>(x); } },
    { "sfclen",    "Impact of SFC length (Fig. 10a)", "SFC length",
      { 5, 6, 7, 8, 9, 10 }, [](Params& p, double x) { p.sfcLen = static_cast<int>(x); } },
    { "parallel",  "Impact of VNF parallelism (Fig. 12a)", "Degree of parallelism",
      { 0.1, 0.3, 0.5, 0.7, 0.9 }, [](Params& p, double x) { p.par = x; } },
    { "heavytail", "Impact of heavy-tailed workloads (Fig. 13a)", "Number of requests",
      { 10, 20, 30, 40, 50 }, [](Params& p, double x) { p.requests = static_cast<int>(x); p.heavyTail = true; } },
    { "weights",   "Impact of the computing / bandwidth weights (Fig. 11)", "Bandwidth weight α",
      { 0.1, 0.3, 0.5, 0.7, 0.9 }, [](Params& p, double x) { p.wBw = x; } },
  };
}

// JSON has no NaN: a point where an algorithm accepted nothing is written as null
std::string num(double x) { if (!std::isfinite(x)) return "null"; char b[32]; std::snprintf(b, sizeof b, "%.4f", x); return b; }

} // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  int runs = 10;
  std::string only, out = "results.json";
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
    else if (a == "--only" && i + 1 < argc) only = argv[++i];
    else if (a == "--out" && i + 1 < argc) out = argv[++i];
    else if (a == "--cand" && i + 1 < argc) g_cand = std::atol(argv[++i]);
    else if (a == "--kmax" && i + 1 < argc) g_kmax = std::atol(argv[++i]);
    else if (a == "--verbose") g_verbose = true;
    else if (a == "--diag") g_diag = true;
    else if (a == "--paired") g_paired = true;
    else if (a == "--sfc" && i + 1 < argc) g_sfcOverride = std::atoi(argv[++i]);
    else if (a == "--full-ms" && i + 1 < argc) g_fullMs = std::atof(argv[++i]);
    else if (a == "--reqs" && i + 1 < argc) g_reqOverride = std::atoi(argv[++i]);
    else if (a == "--staged") g_allIsfg = false;
    else { std::printf("usage: loa_experiments [--runs N] [--only id] [--out file.json]\n"); return 0; }
  }

  // Write to <out>.tmp and rename at the end, so a run that is stopped halfway
  // leaves the previous results file untouched.
  const std::string tmp = out + ".tmp";
  std::ofstream js(tmp.c_str());
  js << "{\"runs\":" << runs << ",\"candCap\":" << g_cand << ",\"kmax\":" << g_kmax << ",\"algorithms\":[";
  for (int a = 0; a < A_COUNT; a++) js << (a ? "," : "") << "\"" << ALGO_NAME[a] << "\"";
  js << "],\"experiments\":[";
  bool firstExp = true;

  const auto all = experiments();
  for (std::size_t ei = 0; ei < all.size(); ei++) {
    const auto& E = all[ei];   // ei also seeds the RNG: same numbers on every compiler
    if (!only.empty() && E.id != only) continue;
    std::printf("== %s\n", E.title.c_str()); std::fflush(stdout);
    js << (firstExp ? "" : ",") << "{\"id\":\"" << E.id << "\",\"title\":\"" << E.title << "\",\"xlabel\":\"" << E.xlabel << "\",\"points\":[";
    firstExp = false;

    for (std::size_t xi = 0; xi < E.xs.size(); xi++) {
      const double x = E.xs[xi];
      Params P; E.apply(P, x);
      g_pair = PairStats();
      if (g_reqOverride) P.requests = g_reqOverride;
      if (g_sfcOverride) { P.sfcLen = g_sfcOverride; P.catalog = std::max(P.catalog, P.sfcLen); }
      // per algorithm, per run: average delay of that run
      std::vector<std::vector<double>> delay(A_COUNT), acc(A_COUNT), ms(A_COUNT), jain(A_COUNT), prov(A_COUNT),
                                       bwv(A_COUNT), cpuv(A_COUNT), cvv(A_COUNT), resv(A_COUNT), lim(A_COUNT);
      for (int r = 0; r < runs; r++) {
        std::mt19937 rng(static_cast<unsigned>(1000003u * (r + 1) + 7919u * (xi + 1) + static_cast<unsigned>(ei)));
        Topo T = genTopo(P, rng);
        auto cat = genCatalog(P, rng);
        auto reqs = genRequests(P, rng);
        for (int a = 0; a < A_COUNT; a++) {
          if (g_paired && a != A_HYBRID) continue;
          RunStats s = runSequence(static_cast<Algo>(a), P, T, cat, reqs);
          if (s.accepted) delay[a].push_back(s.delaySum / s.accepted);
          acc[a].push_back(static_cast<double>(s.accepted) / s.total);
          ms[a].push_back(s.ms / s.total);
          jain[a].push_back(s.jain);
          cvv[a].push_back(s.cv);
          if (s.accepted) { bwv[a].push_back(s.bwSum / s.accepted); cpuv[a].push_back(s.cpuSum / s.accepted); resv[a].push_back(s.resSum / s.accepted); }
          if (a == A_LOA || a == A_HYBRID || a == A_ROA || a == A_HROA) {
            prov[a].push_back(s.accepted ? static_cast<double>(s.proven) / s.accepted : 0);
            lim[a].push_back(static_cast<double>(s.limited) / s.total);
          }
        }
      }
      auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? NAN : s / v.size(); };
      auto ci95 = [&](const std::vector<double>& v) {
        if (v.size() < 2) return 0.0;
        const double m = mean(v); double s = 0;
        for (double x : v) s += (x - m) * (x - m);
        return 1.96 * std::sqrt(s / (v.size() - 1)) / std::sqrt(static_cast<double>(v.size()));
      };
      // Normalised resource consumption (paper 1, Eq. 16): divide by the largest value
      // any compared algorithm used at this point, weight with α / β
      double maxBw = 0, maxCpu = 0;
      for (int a = 0; a < A_COUNT; a++) {
        if (!bwv[a].empty())  maxBw  = std::max(maxBw,  mean(bwv[a]));
        if (!cpuv[a].empty()) maxCpu = std::max(maxCpu, mean(cpuv[a]));
      }
      auto normRes = [&](int a) {
        if (bwv[a].empty()) return static_cast<double>(NAN);
        return P.wBw * (maxBw > 0 ? mean(bwv[a]) / maxBw : 0) + (1 - P.wBw) * (maxCpu > 0 ? mean(cpuv[a]) / maxCpu : 0);
      };
      js << (xi ? "," : "") << "{\"x\":" << num(x) << ",\"algos\":{";
      std::printf("  x=%-6g", x);
      for (int a = 0; a < A_COUNT; a++) {
        js << (a ? "," : "") << "\"" << ALGO_NAME[a] << "\":{\"delay\":" << num(mean(delay[a])) << ",\"ci\":" << num(ci95(delay[a]))
           << ",\"accept\":" << num(mean(acc[a])) << ",\"ms\":" << num(mean(ms[a])) << ",\"jain\":" << num(mean(jain[a]))
           << ",\"cv\":" << num(mean(cvv[a])) << ",\"bw\":" << num(mean(bwv[a])) << ",\"cpu\":" << num(mean(cpuv[a]))
           << ",\"res\":" << num(mean(resv[a])) << ",\"resci\":" << num(ci95(resv[a])) << ",\"resMax\":" << num(normRes(a));
        if (!prov[a].empty()) js << ",\"proven\":" << num(mean(prov[a])) << ",\"limited\":" << num(mean(lim[a]));
        js << "}";
        std::printf("  %s %.1f/%.3f (acc %.2f, %.0f ms)", ALGO_NAME[a], mean(delay[a]), mean(resv[a]), mean(acc[a]), mean(ms[a]));
        if (!lim[a].empty()) std::printf(" [limit hit %.0f%%]", mean(lim[a]) * 100);
      }
      js << "}}";
      std::printf("\n");
      if (g_paired) {
        const PairStats& q = g_pair;
        const double b = q.both ? q.both : 1, n = q.reqs ? q.reqs : 1;
        std::printf("  PAIRED x=%g requests %d | LOA found %d, hybrid found %d, search finished %d | on %d both found: LOA %.2f, hybrid %.2f ms | better %d, worse %d, best gain %.1f%% | LOA missed %d, hybrid missed %d | fallback used %d, helped %d | time LOA %.0f ms, hybrid mean %.1f ms, max %.0f ms\n",
                    x, q.reqs, q.loaFound, q.bestFound, q.complete, q.both, q.loaSum / b, q.bestSum / b, q.better, q.worse, q.maxGainPct, q.loaMissed, q.hybMissed, q.fallbacks, q.fallbackHelped, q.loaMs / n, q.pipeMs / n, q.pipeMaxMs);
        const double y = q.syncN ? q.syncN : 1;
        std::printf("  SYNC x=%g skewed requests %d | skew %.2f -> %.2f ms | buffer %.1f -> %.1f kbit | latency %.2f -> %.2f ms | cpu %.1f -> %.1f\n",
                    x, q.syncN, q.skewB / y, q.skewA / y, q.bufB / y, q.bufA / y, q.latB / y, q.latA / y, q.cpuB / y, q.cpuA / y);
      }
      std::fflush(stdout);
    }
    js << "]}";
    js.flush();
  }
  js << "]}\n";
  js.close();
  std::remove(out.c_str());
  if (std::rename(tmp.c_str(), out.c_str()) != 0) { std::printf("could not write %s (results are in %s)\n", out.c_str(), tmp.c_str()); return 1; }
  return 0;
}
