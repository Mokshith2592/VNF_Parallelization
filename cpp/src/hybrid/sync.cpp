/* =============================================================
   LOA Hybrid — Merge synchronisation (see sync.hpp)
   ============================================================= */
#include "hybrid/sync.hpp"
#include <algorithm>
#include <cmath>

namespace {

const double STEP = 0.05;       // speed-up step
const double SKEW_TOL = 1e-6;

struct Timing {
  std::vector<double> comp;                   // completion time per VNF
  std::vector<std::vector<double>> arr;       // per VNF: arrival from each predecessor
  std::vector<double> ready;                  // per VNF: last arrival
  std::vector<double> arrEgress;              // arrival at egress from each VNF of g.last
  double readyEgress = 0, lat = 0;
};

struct Sync {
  const Model& M;
  const Scheme& S;
  const Isfg& g;
  const std::vector<std::vector<double>>& D;
  std::vector<double> speed;
  std::vector<char> shared;
  std::vector<double> newDem;                 // demand on a new instance, 0 when shared

  double rep(std::size_t outs) const { return outs > 1 ? (outs - 1) * M.trep : 0.0; }
  double integ(std::size_t ins) const { return ins > 1 ? (ins - 1) * M.tint : 0.0; }
  double proc(int f) const { return M.V[f].proc / speed[f]; }

  Timing timing() const {
    Timing t;
    const std::size_t F = M.V.size();
    t.comp.assign(F, 0); t.arr.assign(F, {}); t.ready.assign(F, 0);
    for (int f : g.topo) {
      double last = -INF;
      for (int p : g.pred[f]) {
        const double v = p < 0 ? rep(g.first.size()) + D[M.cfg.src][S.hosts[f]]
                               : t.comp[p] + rep(g.succ[p].size()) + D[S.hosts[p]][S.hosts[f]];
        t.arr[f].push_back(v);
        last = std::max(last, v);
      }
      t.ready[f] = last;
      t.comp[f] = last + integ(g.pred[f].size()) + proc(f);
    }
    double last = -INF;
    for (int p : g.last) {
      const double v = t.comp[p] + rep(g.succ[p].size()) + D[S.hosts[p]][M.cfg.dst];
      t.arrEgress.push_back(v);
      last = std::max(last, v);
    }
    t.readyEgress = last;
    t.lat = last + integ(g.last.size());
    return t;
  }

  static double skewOf(const std::vector<double>& a) {
    return a.size() < 2 ? 0.0 : *std::max_element(a.begin(), a.end()) - *std::min_element(a.begin(), a.end());
  }

  std::vector<double> freeCpu() const {
    std::vector<double> fr(M.n);
    for (int h = 0; h < M.n; h++) fr[h] = M.cfg.nodes[h].cap;
    for (std::size_t f = 0; f < S.hosts.size(); f++) fr[S.hosts[f]] -= newDem[f] * speed[f];
    return fr;
  }

  // Time f may lose without delaying anything after it: the smallest wait of its outputs
  double spareTime(int f, const Timing& t) const {
    double spare = INF;
    for (int q : g.succ[f]) {
      if (q < 0) {
        for (std::size_t i = 0; i < g.last.size(); i++)
          if (g.last[i] == f) spare = std::min(spare, t.readyEgress - t.arrEgress[i]);
      } else {
        for (std::size_t i = 0; i < g.pred[q].size(); i++)
          if (g.pred[q][i] == f) spare = std::min(spare, t.ready[q] - t.arr[q][i]);
      }
    }
    return spare == INF ? 0.0 : spare;
  }

  // Latest VNFs first, so a whole early branch shares its spare time
  void slowDown() {
    for (auto it = g.topo.rbegin(); it != g.topo.rend(); ++it) {
      const int f = *it;
      if (shared[f] || M.V[f].proc <= 0 || speed[f] <= M.cfg.sMin) continue;
      const double spare = spareTime(f, timing());
      if (spare <= SKEW_TOL) continue;
      speed[f] = std::max(M.cfg.sMin, M.V[f].proc / (proc(f) + spare));
    }
  }

  // The chain of last arrivals from egress back to ingress
  std::vector<int> lateChain(const Timing& t) const {
    std::vector<int> chain;
    int f = -1;
    for (std::size_t i = 0; i < g.last.size(); i++)
      if (t.arrEgress[i] >= t.readyEgress - EPS) f = g.last[i];
    while (f >= 0) {
      chain.push_back(f);
      int next = -1;
      for (std::size_t i = 0; i < g.pred[f].size(); i++)
        if (t.arr[f][i] >= t.ready[f] - EPS) next = g.pred[f][i];
      f = next;
    }
    return chain;   // egress side first
  }

  // Give CPU to the late chain, but only upstream of a merge that is still skewed.
  // A VNF can feed two merges and be early at the other one, so a step that widens
  // the worst skew or the worst buffer is undone and the next VNF is tried.
  bool speedUp() {
    const Timing t = timing();
    const std::vector<int> chain = lateChain(t);
    const std::vector<double> fr = freeCpu();
    std::vector<std::pair<double, int>> cand;
    bool skewed = skewOf(t.arrEgress) > SKEW_TOL;
    for (int f : chain) {
      if (skewed && !shared[f] && M.V[f].proc > 0 && speed[f] + STEP <= M.cfg.sMax + EPS &&
          newDem[f] * STEP <= fr[S.hosts[f]] + EPS)
        cand.push_back({ M.V[f].proc / speed[f] - M.V[f].proc / (speed[f] + STEP), f });
      if (skewOf(t.arr[f]) > SKEW_TOL) skewed = true;
    }
    std::sort(cand.rbegin(), cand.rend());
    const double skew0 = maxSkew(), wait0 = maxWait();
    for (const auto& c : cand) {
      speed[c.second] += STEP;
      if (maxSkew() <= skew0 + SKEW_TOL && maxWait() <= wait0 + SKEW_TOL) return true;
      speed[c.second] -= STEP;
    }
    return false;
  }

  double maxSkew() const {
    const Timing t = timing();
    double m = skewOf(t.arrEgress);
    for (int f : g.topo) m = std::max(m, skewOf(t.arr[f]));
    return m;
  }

  // Worst merge by total waiting time of its branches: what fills the buffer
  double maxWait() const {
    const Timing t = timing();
    double m = 0, w = 0;
    for (double a : t.arrEgress) w += t.readyEgress - a;
    m = w;
    for (int f : g.topo) {
      w = 0;
      for (double a : t.arr[f]) w += t.ready[f] - a;
      m = std::max(m, w);
    }
    return m;
  }
};

double bufferOf(const std::vector<double>& arr, double last, double bw) {
  double wait = 0;
  for (double a : arr) wait += last - a;
  return wait * bw;              // Mbps · ms = kbit
}

} // namespace

SyncResult syncMerges(const Model& M, const Scheme& s) {
  SyncResult R;
  if (!s.ok) return R;
  std::vector<double> nu; std::vector<char> reuse;
  placeInstances(M, s.hosts, nu, reuse);
  Sync Y{ M, s, s.g, s.hop ? M.dh : M.d, std::vector<double>(M.V.size(), 1.0), reuse, std::vector<double>(M.V.size(), 0.0) };
  for (std::size_t f = 0; f < M.V.size(); f++) Y.newDem[f] = reuse[f] ? 0 : M.V[f].dem;

  const Timing t0 = Y.timing();
  const auto cpu = [&]() { double c = 0; for (std::size_t f = 0; f < M.V.size(); f++) c += Y.newDem[f] * Y.speed[f]; return c; };
  R.cpuBefore = cpu();

  // Free CPU on the early branches, spend it on the late one, then use the new spare time
  const int steps = static_cast<int>((M.cfg.sMax - 1) / STEP + 1) * static_cast<int>(M.V.size()) + 1;
  for (int round = 0; round < 3; round++) {
    Y.slowDown();
    for (int i = 0; i < steps && Y.maxSkew() > SKEW_TOL && Y.speedUp(); i++) {}
  }
  Y.slowDown();

  const Timing t1 = Y.timing();
  R.ok = true;
  R.speed = Y.speed;
  R.shared = reuse;
  R.latBefore = t0.lat; R.latAfter = t1.lat;
  R.cpuAfter = cpu();

  const double bw = M.cfg.breq;
  auto addRow = [&](int f, int node, const std::vector<double>& a0, double r0, const std::vector<double>& a1, double r1) {
    MergeRow m;
    m.f = f; m.node = node; m.branches = static_cast<int>(a0.size());
    m.skewBefore = Sync::skewOf(a0); m.skewAfter = Sync::skewOf(a1);
    m.bufBefore = bufferOf(a0, r0, bw); m.bufAfter = bufferOf(a1, r1, bw);
    R.maxSkewBefore = std::max(R.maxSkewBefore, m.skewBefore);
    R.maxSkewAfter  = std::max(R.maxSkewAfter, m.skewAfter);
    R.maxBufBefore  = std::max(R.maxBufBefore, m.bufBefore);
    R.maxBufAfter   = std::max(R.maxBufAfter, m.bufAfter);
    if (M.cfg.bufMax > 0 && m.bufBefore > M.cfg.bufMax + EPS) R.overflowBefore++;
    if (M.cfg.bufMax > 0 && m.bufAfter > M.cfg.bufMax + EPS) R.overflowAfter++;
    R.merges.push_back(m);
  };
  for (int f : s.g.topo)
    if (t0.arr[f].size() > 1) addRow(f, s.hosts[f], t0.arr[f], t0.ready[f], t1.arr[f], t1.ready[f]);
  if (t0.arrEgress.size() > 1) addRow(-2, M.cfg.dst, t0.arrEgress, t0.readyEgress, t1.arrEgress, t1.readyEgress);
  return R;
}

json::Value syncJson(const Model& M, const SyncResult& r) {
  json::Value o = json::Value::object();
  o.set("ok", r.ok);
  if (!r.ok) return o;
  o.set("latBefore", r.latBefore); o.set("latAfter", r.latAfter);
  o.set("cpuBefore", r.cpuBefore); o.set("cpuAfter", r.cpuAfter);
  o.set("maxSkewBefore", r.maxSkewBefore); o.set("maxSkewAfter", r.maxSkewAfter);
  o.set("maxBufBefore", r.maxBufBefore); o.set("maxBufAfter", r.maxBufAfter);
  o.set("overflowBefore", r.overflowBefore); o.set("overflowAfter", r.overflowAfter);
  o.set("bufMax", M.cfg.bufMax);
  json::Value rows = json::Value::array();
  for (const auto& m : r.merges) {
    json::Value x = json::Value::object();
    x.set("at", m.f < 0 ? std::string("egress") : M.V[m.f].name);
    x.set("node", M.cfg.nodes[m.node].name);
    x.set("branches", m.branches);
    x.set("skewBefore", m.skewBefore); x.set("skewAfter", m.skewAfter);
    x.set("bufBefore", m.bufBefore); x.set("bufAfter", m.bufAfter);
    rows.push(x);
  }
  o.set("merges", rows);
  json::Value vs = json::Value::array();
  for (std::size_t f = 0; f < M.V.size(); f++) {
    json::Value x = json::Value::object();
    x.set("name", M.V[f].name);
    x.set("speed", r.speed[f]);
    x.set("shared", r.shared[f] != 0);
    x.set("procBefore", M.V[f].proc);
    x.set("procAfter", M.V[f].proc / r.speed[f]);
    vs.push(x);
  }
  o.set("vnfs", vs);
  return o;
}
