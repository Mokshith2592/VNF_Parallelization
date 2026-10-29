/* =============================================================
   LOA Hybrid — DPG search shared by LOA and ROA
   (Algorithm 3/5 outer loop, Algorithm 2/4 inner loop)
   ============================================================= */
#include "paper1/loa.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <queue>
#include <set>

const char* stopName(Stop s) {
  switch (s) {
    case Stop::PrunedBound: return "pruned-bound";
    case Stop::PrunedNext:  return "pruned-next";
    case Stop::Exhausted:   return "dpg-exhausted";
    case Stop::KLimit:      return "k-limit";
  }
  return "?";
}

double schemeCost(const Model& M, const Scheme& s, Objective obj) {
  (void)M;
  if (!s.ok) return INF;
  return obj == Objective::Latency ? s.lat : s.res.norm;
}

namespace {

// ─── DPG weights: DPG_dw (LOA) or DPG_rw (ROA) ────────────────

struct Weights {
  const Model& M;
  Objective o;
  // DPG edge between replicas on MEC nodes x and y (0 when x == y)
  double edge(int x, int y) const {
    if (M.d[x][y] == INF) return INF;
    if (o == Objective::Latency) return M.d[x][y];
    return M.cfg.wBw * M.cfg.breq * M.hop[x][y] / M.cbwMax;
  }
  // DPG node: VNF q placed on MEC node y
  double node(int q, int y) const {
    if (o == Objective::Latency) return M.V[q].proc;
    return (1 - M.cfg.wBw) * (canReuse(M, q, y) ? 0.0 : M.V[q].dem) / M.ccpuMax;
  }
  bool hops() const { return o == Objective::Resource; }
  // Secondary key (ROA): delay of the step, so equal-cost paths come out fastest first
  double tie(int x, int y, int q) const {
    if (o == Objective::Latency) return 0;
    return M.dh[x][y] + (q >= 0 ? M.V[q].proc : 0);
  }
  const char* unit() const { return o == Objective::Latency ? " ms" : ""; }
};

// ─── k cheapest DPG paths (A* with an exact heuristic) ────────

struct KResult { std::vector<Path> paths; bool exhausted; };

struct HeapItem {
  double f, g; std::vector<Hop> seq; int l, mask, host; bool done; long c;
  double t;   // ROA only: delay so far, used to order paths of equal resource cost
};
struct HeapCmp {
  bool operator()(const HeapItem& x, const HeapItem& y) const {
    if (std::fabs(x.f - y.f) > EPS) return x.f > y.f;   // min-heap on f
    if (std::fabs(x.t - y.t) > EPS) return x.t > y.t;   // equal cost: faster first
    return x.c > y.c;                                     // FIFO on ties
  }
};

KResult kShortest(const Model& M, const Weights& W, long k) {
  int maxW = 0;
  for (const auto& l : M.layers) maxW = std::max(maxW, static_cast<int>(l.size()));
  const std::size_t masks = std::size_t(1) << maxW;
  std::vector<double> memo(M.L * masks * M.n, std::nan(""));
  const int dst = M.cfg.dst;

  std::function<double(int, int, int)> H = [&](int l, int mask, int host) -> double {
    const std::size_t key = (static_cast<std::size_t>(l) * masks + mask) * M.n + host;
    if (!std::isnan(memo[key])) return memo[key];
    double best = INF;
    const auto& layer = M.layers[l];
    for (int qi = 0; qi < static_cast<int>(layer.size()); qi++) if (!((mask >> qi) & 1)) {
      const int q = layer[qi];
      for (int y = 0; y < M.n; y++) {
        const double c = W.edge(host, y) + W.node(q, y);
        if (c < INF) best = std::min(best, c + H(l, mask | (1 << qi), y));
      }
    }
    if (l == M.L - 1) best = std::min(best, W.edge(host, dst));
    else for (int qi = 0; qi < static_cast<int>(M.layers[l + 1].size()); qi++) {
      const int q = M.layers[l + 1][qi];
      for (int y = 0; y < M.n; y++) {
        const double c = W.edge(host, y) + W.node(q, y);
        if (c < INF) best = std::min(best, c + H(l + 1, 1 << qi, y));
      }
    }
    memo[key] = best;
    return best;
  };

  std::priority_queue<HeapItem, std::vector<HeapItem>, HeapCmp> heap;
  long counter = 0;
  KResult out;

  auto pushState = [&](double g, double t, std::vector<Hop> seq, int l, int mask, int host) {
    const double h = H(l, mask, host);
    if (h < INF) heap.push({ g + h, g, std::move(seq), l, mask, host, false, counter++, t });
  };

  for (int qi = 0; qi < static_cast<int>(M.layers[0].size()); qi++) {
    const int q = M.layers[0][qi];
    for (int y = 0; y < M.n; y++) {
      const double c = W.edge(M.cfg.src, y) + W.node(q, y);
      if (c < INF) pushState(c, W.tie(M.cfg.src, y, q), { { q, y } }, 0, 1 << qi, y);
    }
  }

  while (!heap.empty() && static_cast<long>(out.paths.size()) < k) {
    HeapItem it = heap.top(); heap.pop();
    if (it.done) {
      Path p; p.id = static_cast<int>(out.paths.size()) + 1; p.seq = std::move(it.seq); p.cost = it.f;
      out.paths.push_back(std::move(p));
      continue;
    }
    const auto& layer = M.layers[it.l];
    for (int qi = 0; qi < static_cast<int>(layer.size()); qi++) if (!((it.mask >> qi) & 1)) {
      const int q = layer[qi];
      for (int y = 0; y < M.n; y++) {
        const double c = W.edge(it.host, y) + W.node(q, y);
        if (c < INF) { auto s = it.seq; s.push_back({ q, y }); pushState(it.g + c, it.t + W.tie(it.host, y, q), std::move(s), it.l, it.mask | (1 << qi), y); }
      }
    }
    if (it.l == M.L - 1) {
      const double c = W.edge(it.host, dst);
      if (c < INF) heap.push({ it.g + c, it.g + c, it.seq, it.l, it.mask, it.host, true, counter++, it.t + W.tie(it.host, dst, -1) });
    } else {
      for (int qi = 0; qi < static_cast<int>(M.layers[it.l + 1].size()); qi++) {
        const int q = M.layers[it.l + 1][qi];
        for (int y = 0; y < M.n; y++) {
          const double c = W.edge(it.host, y) + W.node(q, y);
          if (c < INF) { auto s = it.seq; s.push_back({ q, y }); pushState(it.g + c, it.t + W.tie(it.host, y, q), std::move(s), it.l + 1, 1 << qi, y); }
        }
      }
    }
  }
  out.exhausted = static_cast<long>(out.paths.size()) < k;
  return out;
}

// ─── Path-level filters ───────────────────────────────────────

/** Capacity (new instances only), reliability, and the path's own delay against T_max. */
bool pathFeasible(const Model& M, const Weights& W, const Path& p) {
  std::vector<int> hosts(M.V.size(), -1);
  std::vector<int> load(M.n, 0);
  for (const auto& s : p.seq) { hosts[s.f] = s.h; load[s.h]++; }
  std::vector<double> used; std::vector<char> reuse;
  placeInstances(M, hosts, used, reuse);
  for (int h = 0; h < M.n; h++) if (used[h] > M.cfg.nodes[h].cap + EPS) return false;
  if (M.cfg.rmin > 0)   // necessary condition: load on this path alone
    for (const auto& s : p.seq) if (vnfReliability(M, s.f, s.h, load[s.h]) < M.cfg.rmin - EPS) return false;
  const auto& D = W.hops() ? M.dh : M.d;
  double delay = 0; int prev = M.cfg.src;
  for (const auto& s : p.seq) { delay += D[prev][s.h] + M.V[s.f].proc; prev = s.h; }
  delay += D[prev][M.cfg.dst];
  return delay <= M.cfg.tmax + EPS;
}

std::set<long> orderPairs(const Model& M, const Path& p) {
  std::set<long> s;
  for (std::size_t i = 0; i < p.seq.size(); i++)
    for (std::size_t j = i + 1; j < p.seq.size(); j++) {
      const int a = p.seq[i].f, b = p.seq[j].f;
      if (M.V[a].layer == M.V[b].layer) s.insert(static_cast<long>(a) * 64 + b);
    }
  return s;
}

// Greedy in cost order: drop a path that orders two same-layer VNFs
// opposite to an already kept (cheaper) path.
std::vector<Path> stabilityFilter(const Model& M, const std::vector<Path>& feas, long& removed) {
  std::vector<Path> kept;
  std::vector<std::set<long>> keptOp;
  for (const auto& p : feas) {
    auto op = orderPairs(M, p);
    bool hit = false;
    for (const auto& q : keptOp) {
      for (long key : op) if (q.count((key % 64) * 64 + key / 64)) { hit = true; break; }
      if (hit) break;
    }
    if (hit) removed++;
    else { kept.push_back(p); keptOp.push_back(std::move(op)); }
  }
  return kept;
}

// ─── Algorithm 2/4 lines 10-12: S_parallel around one anchor ──

struct AnchorEval {
  double evaluated = 0, feasible = 0; bool capped = false;
  bool found = false; double cost = INF, lat = INF;
  std::vector<int> hosts; Isfg g;
};

AnchorEval evaluateAnchor(const Model& M, const Weights& W, const std::vector<Path>& A, const Path& anchor) {
  AnchorEval out;
  std::map<int, int> fixed;
  for (const auto& s : anchor.seq) fixed[s.f] = s.h;

  std::vector<std::vector<int>> hostC(M.V.size());
  for (const auto& p : A)
    for (const auto& s : p.seq)
      if (!fixed.count(s.f) && std::find(hostC[s.f].begin(), hostC[s.f].end(), s.h) == hostC[s.f].end())
        hostC[s.f].push_back(s.h);

  // Structures that keep the anchor's order: consecutive anchor VNFs of a level stay ancestor → descendant
  std::vector<std::vector<int>> layerOpts(M.L);
  for (int li = 0; li < M.L; li++) {
    std::vector<int> order;
    for (const auto& s : anchor.seq) if (M.V[s.f].layer == li) order.push_back(s.f);
    for (std::size_t i = 0; i < M.structs[li].size(); i++)
      if (structOK(M, M.structs[li][i], order)) layerOpts[li].push_back(static_cast<int>(i));
    if (layerOpts[li].empty()) return out;
  }

  std::vector<int> freeV;
  for (const auto& v : M.V) if (!fixed.count(v.idx)) freeV.push_back(v.idx);
  double structCombos = 1;
  for (const auto& o : layerOpts) structCombos *= static_cast<double>(o.size());

  std::vector<int> hosts(M.V.size(), -1);
  for (const auto& kv : fixed) hosts[kv.first] = kv.second;
  std::vector<int> structIdx(M.L, 0);
  const double cap = static_cast<double>(M.cfg.candCap);
  std::vector<Isfg> combos;   // the t-th structure combination, built once

  auto tryHosts = [&]() {
    if (!hostsFeasible(M, hosts)) { out.evaluated += structCombos; return; }
    std::fill(structIdx.begin(), structIdx.end(), 0);
    for (std::size_t t = 0; ; t++) {
      if (out.evaluated >= cap) { out.capped = true; return; }
      if (t == combos.size()) {
        std::vector<int> pick(M.L);
        for (int li = 0; li < M.L; li++) pick[li] = layerOpts[li][structIdx[li]];
        combos.push_back(makeIsfg(M, pick));
      }
      const Isfg& g = combos[t];
      EvalResult ev = evalScheme(M, hosts, g, W.hops());
      out.evaluated += 1;
      if (ev.lat <= M.cfg.tmax + EPS && ev.lat < INF) {
        out.feasible += 1;
        const double c = W.o == Objective::Latency ? ev.lat : resourceCost(M, hosts, g, true).norm;
        // ROA: equal resource cost → keep the faster scheme
        if (!out.found || c < out.cost - EPS || (std::fabs(c - out.cost) <= EPS && ev.lat < out.lat - EPS)) {
          out.found = true; out.cost = c; out.lat = ev.lat; out.hosts = hosts; out.g = g;
        }
      }
      int li = M.L - 1;
      while (li >= 0 && ++structIdx[li] >= static_cast<int>(layerOpts[li].size())) { structIdx[li] = 0; li--; }
      if (li < 0) break;
    }
  };

  std::function<void(std::size_t)> rec = [&](std::size_t i) {
    if (out.capped) return;
    if (i == freeV.size()) { tryHosts(); return; }
    const int f = freeV[i];
    for (int h : hostC[f]) { hosts[f] = h; rec(i + 1); if (out.capped) return; }
    hosts[f] = -1;
  };
  rec(0);
  return out;
}

} // namespace

// ─── Algorithm 3 / 5 (outer loop) ─────────────────────────────

LoaResult runSearch(const Model& M, Objective obj, const Scheme* seed) {
  const double t0 = nowMs();
  const Weights W{ M, obj };
  const std::string name = obj == Objective::Latency ? "LOA" : "ROA";
  const std::string u = W.unit();
  LoaResult R;
  R.obj = obj;
  double best = INF;
  const double seedCost = seed ? schemeCost(M, *seed, obj) : INF;
  if (seedCost < INF) { best = seedCost; R.best = *seed; R.bestCost = best; }

  long k = M.n + 1;
  long warm = 0;
  std::vector<int> prevStable;
  const long kmax = M.cfg.kmax;
  auto ev = [&](const std::string& ph, const std::string& txt) {
    R.trace.push_back({ ph, R.st.rounds, k, best, txt });
  };

  ev("init", seedCost < INF
    ? "Seeded with " + seed->source + ": best starts at " + fmt(best) + u + " instead of inf."
    : "No seed: best starts at inf.");

  for (;;) {
    R.st.rounds++;
    R.st.finalK = k;
    KResult sp = kShortest(M, W, k);
    const auto& raw = sp.paths;
    R.st.pathsGenerated += static_cast<long>(raw.size());
    ev("kpaths", "Took the " + std::to_string(raw.size()) + " cheapest DPG paths" +
       (raw.empty() ? "." : " (" + fmt(raw.front().cost) + " .. " + fmt(raw.back().cost) + u + ").") +
       (sp.exhausted ? " The DPG has no more paths." : ""));

    // Line 7: feasibility (capacity with reuse, T_max, reliability)
    std::vector<Path> feas;
    long inf = 0;
    for (const auto& p : raw) { if (pathFeasible(M, W, p)) feas.push_back(p); else inf++; }
    R.st.infeasiblePaths += inf;
    ev("feasible", std::to_string(inf) + " infeasible, " + std::to_string(feas.size()) + " remain.");

    // Lines 8-19: partition-stability
    long removed = 0;
    std::vector<Path> P = stabilityFilter(M, feas, removed);
    R.st.stabilityRemoved += removed;
    ev("stable", std::to_string(removed) + " removed by the stability filter, " + std::to_string(P.size()) + " stable.");

    // Lines 20-23: coverage
    std::set<int> cov;
    for (const auto& p : P) for (const auto& s : p.seq) cov.insert(s.f);
    if (cov.size() < M.V.size()) {
      const bool stop = sp.exhausted || k >= kmax;
      ev("coverage", "Stable paths miss some VNFs." + std::string(stop ? " Window cannot grow." : " k doubles to " + std::to_string(2 * k) + "."));
      if (stop) { R.stop = sp.exhausted ? Stop::Exhausted : Stop::KLimit; break; }
      k *= 2; continue;
    }

    // Algorithm 2/4 lines 1-5: smallest covering prefix A* and its lower bound
    std::set<int> acc; std::size_t pre = 0;
    for (std::size_t i = 0; i < P.size(); i++) {
      for (const auto& s : P[i].seq) acc.insert(s.f);
      pre = i + 1;
      if (acc.size() == M.V.size()) break;
    }
    const double cmax = P[pre - 1].cost;
    if (cmax > best + EPS) {
      ev("prune", "Lower bound " + fmt(cmax) + u + " > best " + fmt(best) + u + ": nothing can beat it.");
      R.stop = Stop::PrunedBound; break;
    }
    ev("a2prefix", "A* = first " + std::to_string(pre) + " stable paths, lower bound " + fmt(cmax) + u + ".");

    // Warm start: anchors already evaluated in the previous round give the same candidates
    bool prefixOK = !prevStable.empty() && prevStable.size() <= P.size();
    for (std::size_t i = 0; prefixOK && i < prevStable.size(); i++) prefixOK = P[i].id == prevStable[i];
    std::size_t sizeA = pre;
    bool needEval = true;
    if (R.st.rounds > 1 && prefixOK && warm >= static_cast<long>(pre)) { sizeA = static_cast<std::size_t>(warm); needEval = false; }

    enum { NONE, PRUNE, FINAL, DOUBLE } outcome = NONE;
    for (;;) {
      if (needEval) {
        std::vector<Path> Aset(P.begin(), P.begin() + sizeA);
        AnchorEval ae = evaluateAnchor(M, W, Aset, Aset.back());
        R.st.anchors++;
        R.st.candidates += ae.evaluated;
        R.st.feasibleCandidates += ae.feasible;
        if (ae.capped) R.st.capped = true;
        if (ae.found && ae.cost < best - EPS) {
          best = ae.cost;
          R.best = makeScheme(M, ae.hosts, ae.g, name, W.hops());
          R.bestCost = best;
          R.improvedSeed = true;
          ev("update", "Anchor #" + std::to_string(Aset.back().id) + ": new best " + fmt(best) + u + " (" + isfgText(M, ae.g) + ").");
        }
      }
      needEval = true;

      if (sizeA == P.size()) {
        const bool stop = sp.exhausted || k >= kmax;
        warm = static_cast<long>(sizeA);
        prevStable.clear(); for (const auto& p : P) prevStable.push_back(p.id);
        ev("exhaust", "Window used up before the bound was reached." + std::string(stop ? "" : " k doubles to " + std::to_string(2 * k) + "."));
        outcome = stop ? FINAL : DOUBLE;
        break;
      }
      const Path& pnew = P[sizeA];
      if (pnew.cost > best + EPS) {
        warm = static_cast<long>(sizeA);
        prevStable.clear(); for (const auto& p : P) prevStable.push_back(p.id);
        ev("prune", "Next path #" + std::to_string(pnew.id) + " costs " + fmt(pnew.cost) + u + " > best " + fmt(best) + u + ": rest pruned (Lemma 4.3).");
        outcome = PRUNE;
        break;
      }
      sizeA++;
    }

    if (outcome == PRUNE) { R.stop = Stop::PrunedNext; break; }
    if (outcome == FINAL) { R.stop = sp.exhausted ? Stop::Exhausted : Stop::KLimit; break; }
    k *= 2;
  }

  R.st.ms = nowMs() - t0;
  ev("done", std::string("Stopped: ") + stopName(R.stop) + ". Best = " + fmt(best) + u + ".");
  return R;
}

json::Value loaJson(const Model& M, const LoaResult& r) {
  json::Value o = json::Value::object();
  o.set("objective", r.obj == Objective::Latency ? "latency" : "resource");
  o.set("stop", stopName(r.stop));
  o.set("improvedSeed", r.improvedSeed);
  o.set("cost", r.bestCost);
  json::Value st = json::Value::object();
  st.set("rounds", r.st.rounds);
  st.set("finalK", r.st.finalK);
  st.set("pathsGenerated", r.st.pathsGenerated);
  st.set("infeasiblePaths", r.st.infeasiblePaths);
  st.set("stabilityRemoved", r.st.stabilityRemoved);
  st.set("anchors", r.st.anchors);
  st.set("candidates", r.st.candidates);
  st.set("feasibleCandidates", r.st.feasibleCandidates);
  st.set("capped", r.st.capped);
  st.set("ms", r.st.ms);
  o.set("stats", st);
  o.set("scheme", schemeJson(M, r.best));
  json::Value tr = json::Value::array();
  for (const auto& e : r.trace) {
    json::Value t = json::Value::object();
    t.set("phase", e.phase); t.set("round", e.round); t.set("k", e.k); t.set("best", e.best); t.set("text", e.text);
    tr.push(t);
  }
  o.set("trace", tr);
  return o;
}
