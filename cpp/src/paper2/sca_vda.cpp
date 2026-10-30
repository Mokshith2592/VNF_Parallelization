/* =============================================================
   LOA Hybrid — Paper 2: SCA + VDA
   Adapted to this project's model (one request, per-VNF processing
   delay, τ_rep / τ_int per extra branch, data size l_u).
   ============================================================= */
#include "paper2/sca_vda.hpp"
#include <algorithm>
#include <map>
#include <numeric>

// ─── SCA (Algorithm 2) ────────────────────────────────────────
// SCA's delay estimate (exec + replication + consolidation + average link
// delay, paper 2 Eqs. 15-16) is additive over levels, so the I-SFG with the
// minimum total is the per-level minimum.
ScaResult runSCA(const Model& M) {
  ScaResult r;
  r.layouts = 1;
  r.placementFree = 0;
  for (int li = 0; li < M.L; li++) {
    const auto& sc = M.scaScore[li];
    int best = 0;
    for (int i = 1; i < static_cast<int>(sc.size()); i++) if (sc[i] < sc[best] - EPS) best = i;
    r.pick.push_back(best);
    r.placementFree += M.structScore[li][best];
    r.layouts *= static_cast<double>(sc.size());
  }
  r.g = makeIsfg(M, r.pick);
  return r;
}

// ─── VDA (Algorithm 3) ────────────────────────────────────────
VdaResult runVDA(const Model& M, const Isfg& g) {
  VdaResult out;
  std::vector<int> hosts(M.V.size(), -1);
  std::vector<double> used(M.n, 0);
  std::vector<int> load(M.n, 0);
  // Room in the shareable instance of each (node, type)
  std::map<std::pair<int, std::string>, double> room;
  for (int h = 0; h < M.n; h++)
    for (const auto& in : M.inst[h]) {
      auto key = std::make_pair(h, in.first);
      room[key] = std::max(room.count(key) ? room[key] : -1.0, in.second);
    }

  // Would putting f on node k keep every VNF on k reliable enough?
  auto reliableOn = [&](int f, int k) {
    if (M.cfg.rmin <= 0) return true;
    const int ld = load[k] + 1;
    if (vnfReliability(M, f, k, ld) < M.cfg.rmin - EPS) return false;
    for (std::size_t q = 0; q < hosts.size(); q++)
      if (hosts[q] == k && vnfReliability(M, static_cast<int>(q), k, ld) < M.cfg.rmin - EPS) return false;
    return true;
  };

  // Own completion time of f on node k. Paper 2 also sums the delay of users already
  // sharing the VNF; here processing delay does not change with sharing, so that sum is
  // the same on every node and only f's own delay decides.
  std::vector<double> comp(M.V.size(), 0);
  auto rep = [&](std::size_t outs) { return outs > 1 ? (outs - 1) * M.trep : 0.0; };
  auto delayOn = [&](int f, int k) {
    double best = -INF;
    for (int p : g.pred[f]) {
      const double v = p < 0 ? rep(g.first.size()) + M.d[M.cfg.src][k]
                             : comp[p] + rep(g.succ[p].size()) + M.d[hosts[p]][k];
      best = std::max(best, v);
    }
    const std::size_t ins = g.pred[f].size();
    return best + (ins > 1 ? (ins - 1) * M.tint : 0.0) + M.V[f].proc;
  };

  for (int f : g.topo) {
    // Lines 6-8: sum of shortest distances from the predecessors' hosts (or the AP), ascending
    std::vector<double> score(M.n, 0);
    for (int k = 0; k < M.n; k++)
      for (int p : g.pred[f]) score[k] += M.d[p < 0 ? M.cfg.src : hosts[p]][k];
    std::vector<int> order(M.n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return score[a] < score[b] - EPS; });

    // Lines 9-23: walk the sorted nodes keeping the lowest delay. A node that runs this
    // type and has room is a candidate (lines 10-15). A node that runs it without room is
    // skipped: at most one instance per type on a node (paper 2, Definition 2). The first
    // node without this type that has capacity is the last candidate (lines 16-21).
    int node = -1; bool shares = false;
    double bestDelay = INF;
    for (int k : order) {
      if (score[k] == INF) continue;
      if (!reliableOn(f, k)) continue;
      auto it = room.find(std::make_pair(k, M.V[f].type));
      if (it != room.end()) {
        if (it->second < M.cfg.qcpu - EPS) continue;
        const double dl = delayOn(f, k);
        if (dl < bestDelay - EPS) { bestDelay = dl; node = k; shares = true; }
        continue;
      }
      if (used[k] + M.V[f].dem > M.cfg.nodes[k].cap + EPS) continue;
      const double dl = delayOn(f, k);
      if (dl < bestDelay - EPS) { bestDelay = dl; node = k; shares = false; }
      break;
    }
    if (node < 0) {
      out.failReason = "no node can host " + M.V[f].name + " (capacity / reliability)";
      out.scheme.source = "VDA";
      out.scheme.g = g;
      return out;
    }
    comp[f] = bestDelay;
    hosts[f] = node; load[node]++;   // line 24
    auto key = std::make_pair(node, M.V[f].type);
    if (shares) room[key] -= M.cfg.qcpu;
    else { used[node] += M.V[f].dem; room[key] = M.cfg.instCap - M.cfg.qcpu; }
  }

  out.scheme = makeScheme(M, hosts, g, "VDA");
  if (!out.scheme.ok) out.failReason = "latency " + fmt(out.scheme.lat) + " ms exceeds T_max";
  return out;
}

// ─── VDA over every I-SFG ─────────────────────────────────────
VdaAllResult runVDAAllLayouts(const Model& M, double maxLayouts) {
  VdaAllResult r;
  r.scheme.source = "VDA (all I-SFGs)";
  double total = 1;
  for (const auto& s : M.structs) total *= static_cast<double>(s.size());
  if (total > maxLayouts) return r;

  std::vector<int> pick(M.L, 0);
  for (;;) {
    VdaResult v = runVDA(M, makeIsfg(M, pick));
    r.tried += 1;
    if (v.scheme.ok && (!r.scheme.ok || v.scheme.lat < r.scheme.lat - EPS)) {
      r.scheme = v.scheme;
      r.scheme.source = "VDA (all I-SFGs)";
    }
    int li = M.L - 1;
    while (li >= 0 && ++pick[li] >= static_cast<int>(M.structs[li].size())) { pick[li] = 0; li--; }
    if (li < 0) break;
  }
  return r;
}
