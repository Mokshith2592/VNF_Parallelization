/* =============================================================
   LOA Hybrid — Full-combination search (branch and bound over
   every I-SFG × every placement) and the plain enumeration used
   to test it.
   ============================================================= */
#include "hybrid/full.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace {

struct Search {
  const Model& M;
  const Objective obj;
  const bool hops;                                   // ROA routes on fewest-hop paths
  const std::vector<std::vector<double>>& D;         // routing delays used by the scheme
  FullResult& R;
  const double t0, deadline;
  bool timeUp = false;
  long tick = 0;

  double best = INF, bestLat = INF;                  // incumbent cost and its latency

  // Current I-SFG
  const Isfg* g = nullptr;
  Tails T;
  std::vector<double> repOut, intIn;                 // per VNF: (out − 1)·τ_rep·l_u, (in − 1)·τ_int·l_u
  double repSrc = 0, intDst = 0;

  // Partial placement
  std::vector<int> hosts;
  std::vector<double> comp;                          // completion time of each placed VNF
  std::vector<int> load;
  std::vector<std::vector<int>> onNode;
  std::vector<double> usedLB;                        // lower bound of new-instance demand per node
  double cpuLB = 0;
  std::vector<std::vector<char>> reusable;           // [f][h]: an existing instance of f's type has room
  std::vector<double> minDem;                        // [f]: smallest demand of any VNF of f's type
  std::vector<int> linkCnt;                          // ROA: how many I-SFG edges use each link
  int distinct = 0;
  std::vector<std::vector<std::vector<int>>> routeLinks;   // ROA: link ids of the route a → b

  Search(const Model& m, Objective o, FullResult& r, double budget)
    : M(m), obj(o), hops(o == Objective::Resource), D(hops ? m.dh : m.d), R(r),
      t0(nowMs()), deadline(t0 + budget) {
    const int F = static_cast<int>(M.V.size());
    hosts.assign(F, -1); comp.assign(F, 0);
    load.assign(M.n, 0); onNode.assign(M.n, {}); usedLB.assign(M.n, 0);
    reusable.assign(F, std::vector<char>(M.n, 0));
    for (int f = 0; f < F; f++) for (int h = 0; h < M.n; h++) reusable[f][h] = canReuse(M, f, h);
    minDem.assign(F, 0);
    for (int f = 0; f < F; f++) {
      minDem[f] = M.V[f].dem;
      for (int q = 0; q < F; q++) if (M.V[q].type == M.V[f].type) minDem[f] = std::min(minDem[f], M.V[q].dem);
    }
    if (hops) {
      linkCnt.assign(M.n * M.n, 0);
      routeLinks.assign(M.n, std::vector<std::vector<int>>(M.n));
      for (int a = 0; a < M.n; a++) for (int b = 0; b < M.n; b++) {
        std::vector<int> rt = route(M, a, b, true);
        for (std::size_t i = 0; i + 1 < rt.size(); i++)
          routeLinks[a][b].push_back(std::min(rt[i], rt[i + 1]) * M.n + std::max(rt[i], rt[i + 1]));
      }
    }
  }

  double resCost(int links, double cpu) const {
    return M.cfg.wBw * M.cfg.breq * links / M.cbwMax + (1 - M.cfg.wBw) * cpu / M.ccpuMax;
  }

  // New-instance demand on h is at least, for every VNF type placed there that no
  // existing instance can take, the smallest demand of ANY VNF of that type in the
  // chain: a VNF placed later may create the instance the earlier ones then share,
  // so only the chain-wide minimum stays a lower bound as VNFs are added.
  double nodeLB(int h) const {
    double s = 0;
    const auto& on = onNode[h];
    for (std::size_t i = 0; i < on.size(); i++) {
      bool seen = false;
      for (std::size_t j = 0; j < i && !seen; j++) seen = M.V[on[j]].type == M.V[on[i]].type;
      if (!seen && !reusable[on[i]][h]) s += minDem[on[i]];
    }
    return s;
  }

  void addLinks(int a, int b, int sign) {
    for (int id : routeLinks[a][b]) {
      if (sign > 0) { if (linkCnt[id]++ == 0) distinct++; }
      else          { if (--linkCnt[id] == 0) distinct--; }
    }
  }
  void edgesOf(int f, int h, int sign) {
    for (int p : g->pred[f]) addLinks(p < 0 ? M.cfg.src : hosts[p], h, sign);
  }

  void leaf() {
    R.leaves++;
    double lat = -INF;
    for (int p : g->last) lat = std::max(lat, comp[p] + repOut[p] + D[hosts[p]][M.cfg.dst]);
    lat += intDst;
    if (!(lat <= M.cfg.tmax + EPS) || !hostsFeasible(M, hosts)) return;
    double cost = lat;
    if (obj == Objective::Resource) {
      for (int p : g->last) addLinks(hosts[p], M.cfg.dst, +1);
      std::vector<double> nu; std::vector<char> ru;
      cost = resCost(distinct, placeInstances(M, hosts, nu, ru));
      for (int p : g->last) addLinks(hosts[p], M.cfg.dst, -1);
    }
    const bool better = obj == Objective::Latency
      ? lat < best - EPS
      : cost < best - EPS || (std::fabs(cost - best) <= EPS && lat < bestLat - EPS);
    if (!better) return;
    best = cost; bestLat = lat;
    R.best = makeScheme(M, hosts, *g, "Full search", hops);
    R.bestCost = cost;
    R.improvedSeed = true;
    R.trace.push_back({ nowMs() - t0, cost, isfgText(M, *g, &hosts) });
  }

  bool cut(double costLB, double latLB) const {
    if (obj == Objective::Latency) return latLB >= best - EPS;
    return costLB > best + EPS || (costLB >= best - EPS && latLB >= bestLat - EPS);
  }

  void dfs(std::size_t k, double latLB) {
    if (timeUp) return;
    if ((++tick & 1023) == 0 && nowMs() > deadline) { timeUp = true; return; }
    R.nodes++;
    if (k == g->topo.size()) { leaf(); return; }
    const int f = g->topo[k];

    struct Cand { int h; double comp, lat, cost; };
    std::vector<Cand> cs;
    cs.reserve(M.n);
    for (int h = 0; h < M.n; h++) {
      double c = -INF;
      for (int p : g->pred[f]) {
        const int hp = p < 0 ? M.cfg.src : hosts[p];
        c = std::max(c, (p < 0 ? 0 : comp[p]) + (p < 0 ? repSrc : repOut[p]) + D[hp][h]);
      }
      if (c == INF || M.d[h][M.cfg.dst] == INF) continue;
      c += intIn[f] + M.V[f].proc;
      const double lat = std::max(latLB, c + T.tail[f] + M.d[h][M.cfg.dst]);
      if (lat > M.cfg.tmax + EPS) { R.cutFeasible++; continue; }
      double cost = 0;
      if (obj == Objective::Resource) {
        edgesOf(f, h, +1);
        onNode[h].push_back(f);
        cost = resCost(distinct, cpuLB - usedLB[h] + nodeLB(h));
        onNode[h].pop_back();
        edgesOf(f, h, -1);
      }
      if (cut(cost, lat)) { R.cutBound++; continue; }
      cs.push_back({ h, c, lat, cost });
    }
    std::sort(cs.begin(), cs.end(), [&](const Cand& a, const Cand& b) {
      if (obj == Objective::Resource && std::fabs(a.cost - b.cost) > EPS) return a.cost < b.cost;
      if (std::fabs(a.lat - b.lat) > EPS) return a.lat < b.lat;
      return a.h < b.h;
    });

    for (const Cand& c : cs) {
      if (timeUp) return;
      if (cut(c.cost, c.lat)) { R.cutBound++; continue; }   // best may have improved meanwhile
      const int h = c.h;
      hosts[f] = h; comp[f] = c.comp;
      load[h]++; onNode[h].push_back(f);
      const double oldUse = usedLB[h];
      usedLB[h] = nodeLB(h); cpuLB += usedLB[h] - oldUse;
      bool ok = usedLB[h] <= M.cfg.nodes[h].cap + EPS;
      if (ok && M.cfg.rmin > 0)
        for (int q : onNode[h]) if (vnfReliability(M, q, h, load[h]) < M.cfg.rmin - EPS) { ok = false; break; }
      if (ok) {
        if (hops) edgesOf(f, h, +1);
        dfs(k + 1, c.lat);
        if (hops) edgesOf(f, h, -1);
      } else R.cutFeasible++;
      cpuLB -= usedLB[h] - oldUse; usedLB[h] = oldUse;
      onNode[h].pop_back(); load[h]--;
      hosts[f] = -1;
    }
  }

  void searchIsfg(const Isfg& is) {
    g = &is;
    T = placementFreeTails(M, is);
    const std::size_t F = M.V.size();
    repOut.assign(F, 0); intIn.assign(F, 0);
    for (std::size_t f = 0; f < F; f++) {
      repOut[f] = is.succ[f].size() > 1 ? (is.succ[f].size() - 1) * M.trep : 0;
      intIn[f]  = is.pred[f].size() > 1 ? (is.pred[f].size() - 1) * M.tint : 0;
    }
    repSrc = is.first.size() > 1 ? (is.first.size() - 1) * M.trep : 0;
    intDst = is.last.size() > 1 ? (is.last.size() - 1) * M.tint : 0;
    dfs(0, T.head + M.d[M.cfg.src][M.cfg.dst]);
  }
};

} // namespace

FullResult runFull(const Model& M, Objective obj, const Scheme* seed) {
  FullResult R;
  R.obj = obj;
  R.isfgTotal = 1;
  for (const auto& st : M.structs) R.isfgTotal *= static_cast<double>(st.size());
  R.space = R.isfgTotal * std::pow(static_cast<double>(M.n), static_cast<double>(M.V.size()));   // double: may exceed 2^64
  Search S(M, obj, R, M.cfg.fullMs);
  if (seed && seed->ok) {
    S.best = schemeCost(M, *seed, obj); S.bestLat = seed->lat;
    R.best = *seed; R.bestCost = S.best;
  }

  // I-SFGs in increasing placement-free delay: every level's structures sorted
  // by score, combined best-first (each combination is generated exactly once).
  std::vector<std::vector<int>> ord(M.L);
  for (int li = 0; li < M.L; li++) {
    ord[li].resize(M.structs[li].size());
    for (std::size_t i = 0; i < ord[li].size(); i++) ord[li][i] = static_cast<int>(i);
    std::stable_sort(ord[li].begin(), ord[li].end(), [&](int a, int b) { return M.structScore[li][a] < M.structScore[li][b] - EPS; });
  }
  struct St { double sum; long id; std::vector<int> idx; int last; };
  struct Cmp { bool operator()(const St& a, const St& b) const { return a.sum > b.sum + EPS || (std::fabs(a.sum - b.sum) <= EPS && a.id > b.id); } };
  std::priority_queue<St, std::vector<St>, Cmp> heap;
  long ids = 0;
  {
    St s{ 0, ids++, std::vector<int>(M.L, 0), 0 };
    for (int li = 0; li < M.L; li++) s.sum += M.structScore[li][ord[li][0]];
    heap.push(s);
  }
  const double dsd = M.d[M.cfg.src][M.cfg.dst];

  R.stop = "exhausted";
  while (!heap.empty()) {
    St s = heap.top(); heap.pop();
    const double lb = s.sum + dsd;
    if (lb > M.cfg.tmax + EPS || (obj == Objective::Latency && lb >= S.best - EPS)) {
      R.stop = "bound";          // every remaining I-SFG has a bound at least this large
      break;
    }
    for (int j = s.last; j < M.L; j++) {
      if (s.idx[j] + 1 >= static_cast<int>(ord[j].size())) continue;
      St c{ s.sum, ids++, s.idx, j };
      c.idx[j]++;
      c.sum += M.structScore[j][ord[j][c.idx[j]]] - M.structScore[j][ord[j][s.idx[j]]];
      heap.push(c);
    }
    std::vector<int> pick(M.L);
    for (int li = 0; li < M.L; li++) pick[li] = ord[li][s.idx[li]];
    const Isfg is = makeIsfg(M, pick);
    R.isfgSearched++;
    S.searchIsfg(is);
    if (S.timeUp) { R.stop = "time budget"; break; }
  }
  R.complete = !S.timeUp;
  R.isfgSkipped = R.isfgTotal - R.isfgSearched;
  R.ms = nowMs() - S.t0;
  return R;
}

json::Value fullJson(const Model& M, const FullResult& r) {
  json::Value o = json::Value::object();
  o.set("objective", r.obj == Objective::Latency ? "latency" : "resource");
  o.set("complete", r.complete);
  o.set("stop", r.stop);
  o.set("improvedSeed", r.improvedSeed);
  o.set("cost", r.bestCost);
  json::Value st = json::Value::object();
  st.set("space", r.space);
  st.set("isfgTotal", r.isfgTotal);
  st.set("isfgSearched", r.isfgSearched);
  st.set("isfgSkipped", r.isfgSkipped);
  st.set("nodes", r.nodes);
  st.set("leaves", r.leaves);
  st.set("cutBound", r.cutBound);
  st.set("cutFeasible", r.cutFeasible);
  st.set("ms", r.ms);
  st.set("budgetMs", M.cfg.fullMs);
  o.set("stats", st);
  o.set("scheme", schemeJson(M, r.best));
  json::Value tr = json::Value::array();
  for (const auto& e : r.trace) {
    json::Value t = json::Value::object();
    t.set("ms", e.ms); t.set("cost", e.cost); t.set("text", e.text);
    tr.push(t);
  }
  o.set("trace", tr);
  return o;
}

// ─── Plain enumeration (test oracle) ──────────────────────────

BruteResult runBrute(const Model& M, double cap) {
  BruteResult R;
  if (!(bruteForceSize(M) <= cap)) return R;
  const double t0 = nowMs();
  R.ran = true;
  R.best.source = R.bestRes.source = "Brute force";
  double bestResCost = INF, bestResLat = INF;
  std::vector<int> hosts(M.V.size(), 0), pick(M.L, 0);
  std::vector<Isfg> all;
  for (;;) {
    all.push_back(makeIsfg(M, pick));
    int li = M.L - 1;
    while (li >= 0 && ++pick[li] >= static_cast<int>(M.structs[li].size())) { pick[li] = 0; li--; }
    if (li < 0) break;
  }
  std::function<void(std::size_t)> rec = [&](std::size_t i) {
    if (i < M.V.size()) { for (int h = 0; h < M.n; h++) { hosts[i] = h; rec(i + 1); } return; }
    if (!hostsFeasible(M, hosts)) return;
    for (const Isfg& g : all) {
      R.evaluated += 1;
      EvalResult ev = evalScheme(M, hosts, g);
      if (ev.lat <= M.cfg.tmax + EPS && ev.lat < INF && (!R.best.ok || ev.lat < R.best.lat - EPS))
        R.best = makeScheme(M, hosts, g, "Brute force");
      EvalResult eh = evalScheme(M, hosts, g, true);
      if (eh.lat <= M.cfg.tmax + EPS && eh.lat < INF) {
        const double c = resourceCost(M, hosts, g, true).norm;
        if (c < bestResCost - EPS || (std::fabs(c - bestResCost) <= EPS && eh.lat < bestResLat - EPS)) {
          bestResCost = c; bestResLat = eh.lat;
          R.bestRes = makeScheme(M, hosts, g, "Brute force", true);
        }
      }
    }
  };
  rec(0);
  R.ms = nowMs() - t0;
  return R;
}
