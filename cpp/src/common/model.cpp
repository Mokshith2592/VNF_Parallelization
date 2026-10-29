/* =============================================================
   LOA Hybrid — Model construction and shared evaluators
   Started as a port of loa-walkthrough/js/engine.js; the execution
   structure is now paper 2's I-SFG (series-parallel per level), and
   the delay model is paper 1's Eqs. 3-7 with the data size l_u.
   ============================================================= */
#include "common/model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <numeric>
#include <set>

// ─── Config parsing / validation ──────────────────────────────

Config parseConfig(const json::Value& v) {
  Config c;
  const json::Value& nodes = v.get("nodes");
  for (std::size_t i = 0; i < nodes.size(); i++) {
    const json::Value& n = nodes[i];
    NodeCfg nc;
    nc.name  = n.strOr("name", std::string(1, static_cast<char>('A' + i)));
    nc.cap   = n.numOr("cap", 0);
    nc.fault = n.numOr("fault", 0);
    nc.x     = n.numOr("x", 0);
    nc.y     = n.numOr("y", 0);
    c.nodes.push_back(nc);
  }
  c.instCap  = v.numOr("instCap", 100);
  c.qcpu     = v.numOr("qcpu", 0);
  const json::Value& links = v.get("links");
  for (std::size_t i = 0; i < links.size(); i++) {
    LinkCfg lc;
    lc.a     = static_cast<int>(links[i].numOr("a", -1));
    lc.b     = static_cast<int>(links[i].numOr("b", -1));
    lc.delay = links[i].numOr("delay", 0);
    lc.bw    = links[i].numOr("bw", 1e18);
    c.links.push_back(lc);
  }
  const json::Value& vnfs = v.get("vnfs");
  for (std::size_t i = 0; i < vnfs.size(); i++) {
    const json::Value& f = vnfs[i];
    VnfCfg vc;
    char nm[8]; std::snprintf(nm, sizeof nm, "f%u", static_cast<unsigned>(i + 1));
    vc.name  = f.strOr("name", nm);
    vc.level = static_cast<int>(f.numOr("level", 1));
    vc.proc  = f.numOr("proc", 0);
    vc.dem   = f.numOr("dem", 0);
    vc.fault = f.numOr("fault", 0);
    vc.type  = f.strOr("type", vc.name);
    c.vnfs.push_back(vc);
  }
  // Existing instances: either a top-level list, or "inst": "f2,f4" on a node
  if (v.has("instances") && v.get("instances").isArray()) {
    const json::Value& in = v.get("instances");
    for (std::size_t i = 0; i < in.size(); i++) {
      InstanceCfg ic;
      ic.node = static_cast<int>(in[i].numOr("node", 0));
      ic.type = in[i].strOr("type", "");
      ic.residual = in[i].numOr("residual", c.instCap);
      c.instances.push_back(ic);
    }
  }
  for (std::size_t i = 0; i < nodes.size(); i++) {
    std::string list = nodes[i].strOr("inst", ""), cur;
    for (std::size_t j = 0; j <= list.size(); j++) {
      const char ch = j < list.size() ? list[j] : ',';
      if (ch == ',' || ch == ' ' || ch == ';') {
        if (!cur.empty()) { InstanceCfg ic; ic.node = static_cast<int>(i); ic.type = cur; ic.residual = c.instCap; c.instances.push_back(ic); }
        cur.clear();
      } else cur += ch;
    }
  }
  c.src      = static_cast<int>(v.numOr("src", 0));
  c.dst      = static_cast<int>(v.numOr("dst", 0));
  c.trep     = v.numOr("trep", 0);
  c.tint     = v.numOr("tint", 0);
  c.tmax     = v.numOr("tmax", 1e9);
  c.pkt      = v.numOr("pkt", 1);
  c.rmin     = v.numOr("rmin", 0);
  c.alpha    = v.numOr("alpha", 0);
  c.kmax     = static_cast<long>(v.numOr("kmax", 2048));
  c.candCap  = static_cast<long>(v.numOr("candCap", 30000));
  c.fullMs   = v.numOr("fullMs", 20000);
  c.full     = v.boolOr("full", true);
  c.allIsfg  = v.strOr("isfg", "all") != "staged";
  c.seed     = v.boolOr("seed", true);
  c.order    = v.boolOr("order", true);
  c.compare  = v.boolOr("compare", true);
  c.roa      = v.boolOr("roa", true);
  c.breq     = v.numOr("breq", 0);
  c.wBw      = v.numOr("wBw", 0.5);
  return c;
}

std::string validateConfig(const Config& c) {
  const int n = static_cast<int>(c.nodes.size());
  if (n < 2)  return "Need at least 2 MEC nodes.";
  if (n > 500) return "At most 500 MEC nodes are supported.";
  if (c.vnfs.empty())     return "Add at least one VNF.";
  if (c.vnfs.size() > 16) return "At most 16 VNFs are supported.";
  std::map<int, int> perLevel;
  for (const auto& f : c.vnfs) {
    if (f.proc < 0 || f.dem < 0) return "Processing delay and demand cannot be negative.";
    if (++perLevel[f.level] > 6) return "At most 6 VNFs per dependency level.";
  }
  for (const auto& l : c.links) {
    if (l.a < 0 || l.b < 0 || l.a >= n || l.b >= n) return "A link refers to a node that does not exist.";
    if (!(l.delay > 0)) return "Every link delay must be positive.";
  }
  if (c.src < 0 || c.src >= n || c.dst < 0 || c.dst >= n) return "Ingress / egress node out of range.";
  if (c.trep < 0 || c.tint < 0) return "Dividing and merging times cannot be negative.";
  if (!(c.pkt > 0)) return "The data size l_u must be positive.";
  if (c.kmax < 1 || c.candCap < 1) return "kmax and candCap must be positive.";
  if (c.fullMs < 0) return "The full-search time budget cannot be negative.";
  if (c.wBw < 0 || c.wBw > 1) return "The bandwidth weight must be between 0 and 1.";
  if (c.rmin < 0 || c.rmin > 1) return "R_min must be between 0 and 1.";
  if (c.breq < 0 || c.qcpu < 0 || c.instCap < 0) return "Bandwidth, instance load and instance capacity cannot be negative.";
  for (const auto& in : c.instances)
    if (in.node < 0 || in.node >= n) return "An existing instance refers to a node that does not exist.";
  return "";
}

std::vector<std::string> configWarnings(const Config& c) {
  std::vector<std::string> w;
  std::map<int, int> perLevel;
  for (const auto& f : c.vnfs) perLevel[f.level]++;
  for (const auto& kv : perLevel)
    if (kv.second >= 4) { w.push_back("Level " + std::to_string(kv.first) + " has " + std::to_string(kv.second) + " VNFs: the structure space grows very fast (see the search-space table)."); break; }
  std::set<std::string> types;
  for (const auto& f : c.vnfs) types.insert(f.type.empty() ? f.name : f.type);
  for (const auto& in : c.instances)
    if (!types.count(in.type))
      w.push_back("Node " + (in.node >= 0 && in.node < static_cast<int>(c.nodes.size()) ? c.nodes[in.node].name : std::string("?")) +
                  ": \"" + in.type + "\" is not a VNF of this chain, so nothing can reuse it.");
  if (c.src == c.dst) w.push_back("Ingress and egress are the same node.");
  return w;
}

// ─── Level structures (I-SFG inside one dependency level) ─────
//
// A series-parallel structure on a set X is a single VNF, a SERIES of ≥ 2
// parts (each a VNF or a parallel part), or a PARALLEL set of ≥ 2 parts
// (each a VNF or a series part). This decomposition is unique, so the
// enumeration below lists every structure exactly once:
//   |X| = 1, 2, 3, 4, 5, 6  →  1, 3, 19, 195, 2791, 51303 structures
//   (checked independently by tests/count_sp.py)
//   (stage sequences only:  →  1, 3, 13, 75, 541, 4683)

namespace {

enum Kind { LEAF, SERIES, PARALLEL };
struct SpNode { Kind kind; int item; std::vector<int> kids; };

struct SpEnum {
  const std::vector<int>& items;
  std::vector<SpNode> pool;
  std::map<unsigned, std::vector<int>> nonSer, nonPar, all;   // tree ids per subset

  explicit SpEnum(const std::vector<int>& it) : items(it) {}

  void build(unsigned mask) {
    if (all.count(mask)) return;
    std::vector<int> ser, par;
    if ((mask & (mask - 1)) == 0) {          // one VNF
      int pos = 0; while (!((mask >> pos) & 1)) pos++;
      pool.push_back({ LEAF, items[pos], {} });
      const int id = static_cast<int>(pool.size()) - 1;
      nonSer[mask] = nonPar[mask] = all[mask] = { id };
      return;
    }
    std::vector<int> cur;
    // SERIES: ordered blocks, each a VNF or a parallel part
    std::function<void(unsigned)> serRec = [&](unsigned rest) {
      for (unsigned b = rest; b; b = (b - 1) & rest) {
        if (b == mask) continue;               // one block only: not a series
        build(b);
        for (int t : nonSer[b]) {
          cur.push_back(t);
          if (b == rest) { pool.push_back({ SERIES, -1, cur }); ser.push_back(static_cast<int>(pool.size()) - 1); }
          else serRec(rest & ~b);
          cur.pop_back();
        }
      }
    };
    // PARALLEL: unordered blocks (the block holding the lowest VNF comes first)
    std::function<void(unsigned)> parRec = [&](unsigned rest) {
      const unsigned low = rest & (~rest + 1);
      const unsigned others = rest & ~low;
      for (unsigned o = others; ; o = (o - 1) & others) {
        const unsigned b = low | o;
        if (b != mask) {
          build(b);
          for (int t : nonPar[b]) {
            cur.push_back(t);
            if (b == rest) { pool.push_back({ PARALLEL, -1, cur }); par.push_back(static_cast<int>(pool.size()) - 1); }
            else parRec(rest & ~b);
            cur.pop_back();
          }
        }
        if (o == 0) break;
      }
    };
    serRec(mask);
    parRec(mask);
    nonSer[mask] = par;                        // a part that is not a series
    nonPar[mask] = ser;
    std::vector<int> a = ser; a.insert(a.end(), par.begin(), par.end());
    all[mask] = a;
  }
};

struct Part { std::vector<int> in, out; };

Part compile(const SpEnum& E, int id, Structure& st) {
  const SpNode& nd = E.pool[id];
  if (nd.kind == LEAF) { st.expr.push_back(nd.item); return { { nd.item }, { nd.item } }; }
  Part p;
  for (std::size_t k = 0; k < nd.kids.size(); k++) {
    if (k) st.expr.push_back(nd.kind == SERIES ? TOK_SER : TOK_PAR);
    const bool wrap = E.pool[nd.kids[k]].kind != LEAF;
    if (wrap) st.expr.push_back(TOK_OPEN);
    Part q = compile(E, nd.kids[k], st);
    if (wrap) st.expr.push_back(TOK_CLOSE);
    if (nd.kind == SERIES) {
      if (k == 0) p.in = q.in;
      else for (int a : p.out) for (int b : q.in) st.edges.push_back({ a, b });
      p.out = q.out;
    } else {
      p.in.insert(p.in.end(), q.in.begin(), q.in.end());
      p.out.insert(p.out.end(), q.out.begin(), q.out.end());
    }
  }
  return p;
}

// A stage sequence: a VNF, a parallel set of VNFs, or a series of those
bool isStaged(const SpEnum& E, int id) {
  const SpNode& nd = E.pool[id];
  auto flat = [&](int k) {
    const SpNode& c = E.pool[k];
    if (c.kind == LEAF) return true;
    if (c.kind == SERIES) return false;
    for (int x : c.kids) if (E.pool[x].kind != LEAF) return false;
    return true;
  };
  if (nd.kind != SERIES) return flat(id);
  for (int k : nd.kids) if (!flat(k)) return false;
  return true;
}

} // namespace

std::vector<Structure> layerStructures(const std::vector<int>& items, bool allIsfg) {
  const int m = static_cast<int>(items.size());
  if (m == 0) return { Structure{} };
  SpEnum E(items);
  const unsigned full = (1u << m) - 1;
  E.build(full);
  std::map<int, int> posOf;
  for (int i = 0; i < m; i++) posOf[items[i]] = i;

  std::vector<Structure> res;
  for (int id : E.all[full]) {
    const bool staged = isStaged(E, id);
    if (!allIsfg && !staged) continue;
    Structure st;
    Part p = compile(E, id, st);
    st.in = p.in; st.out = p.out; st.staged = staged;
    // Descendants by position: transitive closure over the (acyclic) edges
    st.desc.assign(m, 0);
    for (bool changed = true; changed; ) {
      changed = false;
      for (const auto& e : st.edges) {
        const int a = posOf[e.first], b = posOf[e.second];
        const unsigned nw = st.desc[a] | (1u << b) | st.desc[b];
        if (nw != st.desc[a]) { st.desc[a] = nw; changed = true; }
      }
    }
    res.push_back(std::move(st));
  }
  return res;
}

/**
 * Placement-free delay of one level (every link delay = 0): the fork into the
 * level, the longest internal branch, and the merge out of it. This is what
 * paper 2's SCA minimises. Levels are joined all-to-all, so the I-SFG's
 * placement-free delay is the sum over its levels.
 */
double layerScore(const Model& M, const Structure& st) {
  if (st.in.empty()) return 0;
  std::map<int, int> inDeg, outDeg;
  for (const auto& e : st.edges) { outDeg[e.first]++; inDeg[e.second]++; }
  // Longest internal branch, VNFs in topological order (Kahn)
  std::map<int, double> best;
  std::map<int, int> left = inDeg;
  std::vector<int> ready = st.in;
  double longest = 0;
  while (!ready.empty()) {
    const int f = ready.back(); ready.pop_back();
    const double node = M.V[f].proc + (inDeg[f] > 1 ? (inDeg[f] - 1) * M.tint : 0);
    const double here = best[f] + node;
    if (outDeg[f] == 0) longest = std::max(longest, here);
    for (const auto& e : st.edges) if (e.first == f) {
      const double v = here + (outDeg[f] - 1) * M.trep;
      if (v > best[e.second]) best[e.second] = v;
      if (--left[e.second] == 0) ready.push_back(e.second);
    }
  }
  return (st.in.size() - 1) * M.trep + longest + (st.out.size() - 1) * M.tint;
}

// ─── Model ────────────────────────────────────────────────────

Model buildModel(const Config& cfg) {
  Model M;
  M.cfg = cfg;
  const int n = M.n = static_cast<int>(cfg.nodes.size());
  M.trep = cfg.trep * cfg.pkt;
  M.tint = cfg.tint * cfg.pkt;

  M.d.assign(n, std::vector<double>(n, INF));
  M.nx.assign(n, std::vector<int>(n, -1));
  for (int i = 0; i < n; i++) { M.d[i][i] = 0; M.nx[i][i] = i; }
  for (const auto& l : cfg.links) {
    if (l.a == l.b) continue;
    if (l.bw < cfg.breq - EPS) continue;   // not enough residual bandwidth for this request
    M.lmin = std::min(M.lmin, l.delay);
    if (l.delay < M.d[l.a][l.b]) {
      M.d[l.a][l.b] = M.d[l.b][l.a] = l.delay;
      M.nx[l.a][l.b] = l.b; M.nx[l.b][l.a] = l.a;
    }
  }
  // Floyd-Warshall
  for (int m = 0; m < n; m++)
    for (int i = 0; i < n; i++)
      for (int j = 0; j < n; j++)
        if (M.d[i][m] + M.d[m][j] < M.d[i][j] - EPS) {
          M.d[i][j]  = M.d[i][m] + M.d[m][j];
          M.nx[i][j] = M.nx[i][m];
        }

  // Fewest-hop routes (ties broken by delay): what ROA uses to save bandwidth
  M.hop.assign(n, std::vector<int>(n, 1 << 29));
  M.dh.assign(n, std::vector<double>(n, INF));
  M.nxh.assign(n, std::vector<int>(n, -1));
  for (int i = 0; i < n; i++) { M.hop[i][i] = 0; M.dh[i][i] = 0; M.nxh[i][i] = i; }
  for (const auto& l : cfg.links) {
    if (l.a == l.b || l.bw < cfg.breq - EPS) continue;
    if (M.hop[l.a][l.b] > 1 || l.delay < M.dh[l.a][l.b]) {
      M.hop[l.a][l.b] = M.hop[l.b][l.a] = 1;
      M.dh[l.a][l.b] = M.dh[l.b][l.a] = l.delay;
      M.nxh[l.a][l.b] = l.b; M.nxh[l.b][l.a] = l.a;
    }
  }
  for (int m = 0; m < n; m++)
    for (int i = 0; i < n; i++)
      for (int j = 0; j < n; j++) {
        if (M.dh[i][m] == INF || M.dh[m][j] == INF) continue;
        const int h = M.hop[i][m] + M.hop[m][j];
        const double dd = M.dh[i][m] + M.dh[m][j];
        if (h < M.hop[i][j] || (h == M.hop[i][j] && dd < M.dh[i][j] - EPS)) {
          M.hop[i][j] = h; M.dh[i][j] = dd; M.nxh[i][j] = M.nxh[i][m];
        }
      }

  // Existing instances per node, and the normalisers of the ROA cost
  M.inst.assign(n, {});
  for (const auto& in : cfg.instances) M.inst[in.node].push_back({ in.type, in.residual });
  // Reference scale: a serial chain whose |F|+1 hops each have the network's mean
  // hop length, and every VNF on a new instance. Keeps the two terms comparable.
  double hopSum = 0; int pairs = 0;
  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (i != j && M.dh[i][j] < INF) { hopSum += M.hop[i][j]; pairs++; }
  const double meanHop = pairs ? std::max(1.0, hopSum / pairs) : 1.0;
  M.cbwMax = std::max(1e-9, cfg.breq * meanHop * (cfg.vnfs.size() + 1));
  double dsum = 0; for (const auto& f : cfg.vnfs) dsum += f.dem;
  M.ccpuMax = std::max(1e-9, dsum);

  std::set<int> lvSet;
  for (const auto& f : cfg.vnfs) lvSet.insert(f.level);
  std::vector<int> levels(lvSet.begin(), lvSet.end());
  for (std::size_t i = 0; i < cfg.vnfs.size(); i++) {
    const auto& f = cfg.vnfs[i];
    Vnf v;
    v.idx = static_cast<int>(i); v.name = f.name; v.type = f.type.empty() ? f.name : f.type;
    v.proc = f.proc * cfg.pkt; v.dem = f.dem; v.fault = f.fault;
    v.layer = static_cast<int>(std::lower_bound(levels.begin(), levels.end(), f.level) - levels.begin());
    v.pos = 0;
    M.V.push_back(v);
  }
  M.L = static_cast<int>(levels.size());
  M.layers.assign(M.L, {});
  for (auto& v : M.V) { v.pos = static_cast<int>(M.layers[v.layer].size()); M.layers[v.layer].push_back(v.idx); }

  // Structures per level, ranked by placement-free delay (Solution E)
  for (int li = 0; li < M.L; li++) {
    auto parts = layerStructures(M.layers[li], cfg.allIsfg);
    std::vector<double> sc(parts.size());
    for (std::size_t i = 0; i < parts.size(); i++) sc[i] = layerScore(M, parts[i]);
    if (cfg.order) {
      std::vector<std::size_t> idx(parts.size());
      std::iota(idx.begin(), idx.end(), 0);
      std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return sc[a] < sc[b] - EPS; });
      std::vector<Structure> p2; std::vector<double> s2;
      for (auto i : idx) { p2.push_back(std::move(parts[i])); s2.push_back(sc[i]); }
      parts.swap(p2); sc.swap(s2);
    }
    M.structs.push_back(std::move(parts));
    M.structScore.push_back(std::move(sc));
  }
  M.totalPaths = countPaths(M);
  return M;
}

std::vector<int> route(const Model& M, int a, int b, bool fewestHops) {
  if (M.d[a][b] == INF) return {};
  const auto& nx = fewestHops ? M.nxh : M.nx;
  std::vector<int> out{ a };
  int c = a, guard = 0;
  while (c != b && guard++ <= M.n) { c = nx[c][b]; out.push_back(c); }
  return out;
}

// ─── I-SFG composition ────────────────────────────────────────

Isfg makeIsfg(const Model& M, const std::vector<int>& pick) {
  Isfg g;
  g.pick = pick;
  const std::size_t F = M.V.size();
  g.pred.assign(F, {});
  g.succ.assign(F, {});
  std::vector<int> prevOut{ -1 };
  for (int li = 0; li < M.L; li++) {
    const Structure& st = M.structs[li][pick[li]];
    for (int f : st.in) for (int p : prevOut) { g.pred[f].push_back(p); if (p >= 0) g.succ[p].push_back(f); }
    for (const auto& e : st.edges) { g.pred[e.second].push_back(e.first); g.succ[e.first].push_back(e.second); }
    if (li == 0) g.first = st.in;
    // Topological order inside the level (Kahn, stable on the level's VNF order)
    std::map<int, int> left;
    for (const auto& e : st.edges) left[e.second]++;
    std::vector<int> todo = M.layers[li];
    while (!todo.empty()) {
      for (std::size_t i = 0; i < todo.size(); i++) {
        const int f = todo[i];
        if (left[f] > 0) continue;
        g.topo.push_back(f);
        for (const auto& e : st.edges) if (e.first == f) left[e.second]--;
        todo.erase(todo.begin() + i);
        break;
      }
    }
    prevOut = st.out;
  }
  g.last = prevOut;
  for (int p : g.last) g.succ[p].push_back(-2);
  return g;
}

namespace {
int findStructure(const Model& M, int li, bool parallel) {
  const auto& L = M.layers[li];
  for (std::size_t i = 0; i < M.structs[li].size(); i++) {
    const Structure& st = M.structs[li][i];
    if (parallel ? st.edges.empty() : (st.in.size() == 1 && st.out.size() == 1 && st.edges.size() + 1 == L.size())) {
      if (parallel) return static_cast<int>(i);
      // serial in index order: the chain L[0] → L[1] → ...
      bool ok = st.in[0] == L[0];
      for (std::size_t k = 0; ok && k + 1 < L.size(); k++)
        ok = std::find(st.edges.begin(), st.edges.end(), std::make_pair(L[k], L[k + 1])) != st.edges.end();
      if (ok) return static_cast<int>(i);
    }
  }
  return 0;
}
} // namespace

int serialStructure(const Model& M, int layer)   { return findStructure(M, layer, false); }
int parallelStructure(const Model& M, int layer) { return findStructure(M, layer, true); }

Tails placementFreeTails(const Model& M, const Isfg& g) {
  Tails t;
  t.tail.assign(M.V.size(), 0);
  auto rep = [&](std::size_t outs) { return outs > 1 ? (outs - 1) * M.trep : 0.0; };
  auto integ = [&](std::size_t ins) { return ins > 1 ? (ins - 1) * M.tint : 0.0; };
  for (auto it = g.topo.rbegin(); it != g.topo.rend(); ++it) {
    const int f = *it;
    double best = 0;
    for (int s : g.succ[f])
      best = std::max(best, s < 0 ? integ(g.last.size()) : integ(g.pred[s].size()) + M.V[s].proc + t.tail[s]);
    t.tail[f] = rep(g.succ[f].size()) + best;
  }
  double best = 0;
  for (int s : g.first) best = std::max(best, integ(g.pred[s].size()) + M.V[s].proc + t.tail[s]);
  t.head = rep(g.first.size()) + best;
  return t;
}

// ─── DPG path counting (exact with GMP) ───────────────────────

BigCount countPaths(const Model& M) {
  int maxW = 0;
  for (const auto& l : M.layers) maxW = std::max(maxW, static_cast<int>(l.size()));
  const std::size_t masks = std::size_t(1) << maxW;
  std::vector<BigCount> memo(M.L * masks * M.n);
  std::vector<char> have(memo.size(), 0);

  // cnt(l, mask, host): number of path completions from this state
  std::function<BigCount(int, int, int)> cnt;
  cnt = [&](int l, int mask, int host) -> BigCount {
    const std::size_t key = (static_cast<std::size_t>(l) * masks + mask) * M.n + host;
    if (have[key]) return memo[key];
    BigCount c(0);
    const auto& layer = M.layers[l];
    for (int qi = 0; qi < static_cast<int>(layer.size()); qi++)
      if (!((mask >> qi) & 1))
        for (int y = 0; y < M.n; y++)
          if (M.d[host][y] < INF) c += cnt(l, mask | (1 << qi), y);
    if (l == M.L - 1) {
      if (M.d[host][M.cfg.dst] < INF) c += BigCount(1);
    } else {
      for (int qi = 0; qi < static_cast<int>(M.layers[l + 1].size()); qi++)
        for (int y = 0; y < M.n; y++)
          if (M.d[host][y] < INF) c += cnt(l + 1, 1 << qi, y);
    }
    have[key] = 1; memo[key] = c;
    return c;
  };

  BigCount total(0);
  for (int qi = 0; qi < static_cast<int>(M.layers[0].size()); qi++)
    for (int y = 0; y < M.n; y++)
      if (M.d[M.cfg.src][y] < INF) total += cnt(0, 1 << qi, y);
  return total;
}

BigCount structureCount(const Model& M) {
  BigCount s(1);
  for (const auto& st : M.structs) s *= BigCount(static_cast<unsigned>(st.size()));
  return s;
}

BigCount bruteForceSize(const Model& M) {
  BigCount s = structureCount(M);
  for (std::size_t i = 0; i < M.V.size(); i++) s *= BigCount(static_cast<unsigned>(M.n));
  return s;
}

// ─── Scheme evaluation ────────────────────────────────────────

EvalResult evalScheme(const Model& M, const std::vector<int>& hosts, const Isfg& g, bool fewestHops) {
  const auto& D = fewestHops ? M.dh : M.d;
  const std::size_t F = M.V.size();
  auto rep = [&](std::size_t outs) { return outs > 1 ? (outs - 1) * M.trep : 0.0; };
  auto integ = [&](std::size_t ins) { return ins > 1 ? (ins - 1) * M.tint : 0.0; };
  const double repSrc = rep(g.first.size());
  std::vector<double> comp(F, 0);
  std::vector<Breakdown> br(F);
  std::vector<int> arg(F, -1);

  for (int f : g.topo) {
    double best = -INF, bd = 0, brp = 0; int a = -1;
    for (int p : g.pred[f]) {
      const int hp = p < 0 ? M.cfg.src : hosts[p];
      const double rp = p < 0 ? repSrc : rep(g.succ[p].size());
      const double dd = D[hp][hosts[f]];
      const double v  = (p < 0 ? 0 : comp[p]) + rp + dd;
      if (v > best) { best = v; a = p; bd = dd; brp = rp; }
    }
    const double in = integ(g.pred[f].size());
    comp[f] = best + in + M.V[f].proc;
    Breakdown b = a < 0 ? Breakdown{} : br[a];
    b.trans += bd; b.proc += M.V[f].proc; b.rep += brp; b.integ += in;
    br[f] = b; arg[f] = a;
  }

  double best = -INF, bd = 0; int a = -1;
  for (int p : g.last) {
    const double dd = D[hosts[p]][M.cfg.dst];
    const double v  = comp[p] + rep(g.succ[p].size()) + dd;
    if (v > best) { best = v; a = p; bd = dd; }
  }
  EvalResult r;
  const double inD = integ(g.last.size());
  r.lat = best + inD;
  for (int u = a; u >= 0; u = arg[u]) r.chain.insert(r.chain.begin(), u);
  r.br = a < 0 ? Breakdown{} : br[a];
  r.br.trans += bd;
  r.br.integ += inD;
  return r;
}

double vnfReliability(const Model& M, int f, int host, int load) {
  return 1.0 - (M.V[f].fault + M.cfg.nodes[host].fault + M.cfg.alpha * std::max(load - 1, 0));
}

bool canReuse(const Model& M, int f, int host) {
  for (const auto& in : M.inst[host])
    if (in.first == M.V[f].type && in.second >= M.cfg.qcpu - EPS) return true;
  return false;
}

double placeInstances(const Model& M, const std::vector<int>& hosts, std::vector<double>& newUse, std::vector<char>& reuse) {
  newUse.assign(M.n, 0);
  reuse.assign(hosts.size(), 0);
  // Room left in the instance each (node, type) would share: one shared instance per type and node
  std::map<std::pair<int, std::string>, double> room;
  for (int h = 0; h < M.n; h++)
    for (const auto& in : M.inst[h]) {
      auto key = std::make_pair(h, in.first);
      room[key] = std::max(room.count(key) ? room[key] : -1.0, in.second);
    }
  double total = 0;
  for (std::size_t f = 0; f < hosts.size(); f++) {
    if (hosts[f] < 0) continue;
    auto key = std::make_pair(hosts[f], M.V[f].type);
    auto it = room.find(key);
    if (it != room.end() && it->second >= M.cfg.qcpu - EPS) {
      it->second -= M.cfg.qcpu;
      reuse[f] = 1;
    } else {
      newUse[hosts[f]] += M.V[f].dem;
      total += M.V[f].dem;
      room[key] = M.cfg.instCap - M.cfg.qcpu;   // the new instance can be shared from now on
    }
  }
  return total;
}

ResCost resourceCost(const Model& M, const std::vector<int>& hosts, const Isfg& g, bool fewestHops) {
  ResCost r;
  std::set<std::pair<int, int>> links;
  auto addRoute = [&](int a, int b) {
    std::vector<int> rt = route(M, a, b, fewestHops);
    for (std::size_t i = 0; i + 1 < rt.size(); i++) links.insert({ std::min(rt[i], rt[i + 1]), std::max(rt[i], rt[i + 1]) });
  };
  for (int f : g.topo)
    for (int p : g.pred[f]) addRoute(p < 0 ? M.cfg.src : hosts[p], hosts[f]);
  for (int p : g.last) addRoute(hosts[p], M.cfg.dst);
  std::vector<double> newUse; std::vector<char> reuse;
  r.cpu = placeInstances(M, hosts, newUse, reuse);
  r.links = static_cast<int>(links.size());
  r.bw = M.cfg.breq * r.links;
  for (char c : reuse) { if (c) r.reused++; else r.created++; }
  r.norm = M.cfg.wBw * r.bw / M.cbwMax + (1 - M.cfg.wBw) * r.cpu / M.ccpuMax;
  return r;
}

bool hostsFeasible(const Model& M, const std::vector<int>& hosts, std::string* why) {
  std::vector<double> used;
  std::vector<char> reuse;
  placeInstances(M, hosts, used, reuse);   // only new instances take node capacity
  std::vector<int> load(M.n, 0);
  for (std::size_t f = 0; f < hosts.size(); f++) load[hosts[f]]++;
  for (int h = 0; h < M.n; h++)
    if (used[h] > M.cfg.nodes[h].cap + EPS) {
      if (why) *why = "node " + M.cfg.nodes[h].name + ": demand " + fmt(used[h]) + " > capacity " + fmt(M.cfg.nodes[h].cap);
      return false;
    }
  if (M.cfg.rmin > 0)
    for (std::size_t f = 0; f < hosts.size(); f++) {
      const double r = vnfReliability(M, static_cast<int>(f), hosts[f], load[hosts[f]]);
      if (r < M.cfg.rmin - EPS) {
        if (why) *why = M.V[f].name + "@" + M.cfg.nodes[hosts[f]].name + ": reliability " + fmt(r) + " < " + fmt(M.cfg.rmin);
        return false;
      }
    }
  return true;
}

bool structOK(const Model& M, const Structure& st, const std::vector<int>& order) {
  for (std::size_t i = 0; i + 1 < order.size(); i++)
    if (!((st.desc[M.V[order[i]].pos] >> M.V[order[i + 1]].pos) & 1)) return false;
  return true;
}

Scheme makeScheme(const Model& M, const std::vector<int>& hosts, const Isfg& g, const std::string& source, bool fewestHops) {
  Scheme s;
  EvalResult ev = evalScheme(M, hosts, g, fewestHops);
  s.ok = ev.lat <= M.cfg.tmax + EPS && ev.lat < INF && hostsFeasible(M, hosts);
  s.hosts = hosts; s.g = g; s.lat = ev.lat; s.br = ev.br; s.chain = ev.chain; s.source = source;
  s.hop = fewestHops;
  s.res = resourceCost(M, hosts, g, fewestHops);
  std::vector<double> nu; placeInstances(M, hosts, nu, s.reuse);
  return s;
}

// ─── Text / JSON helpers ──────────────────────────────────────

std::string fmt(double x) {
  if (x == INF) return "inf";
  char b[32];
  if (std::fabs(x - std::round(x)) < 1e-9) std::snprintf(b, sizeof b, "%.0f", x);
  else                                    std::snprintf(b, sizeof b, "%.3g", x);
  return b;
}

std::string structText(const Model& M, const Structure& st, const std::vector<int>* hosts) {
  std::string s;
  for (int t : st.expr) {
    if (t >= 0) s += M.V[t].name + (hosts ? "@" + M.cfg.nodes[(*hosts)[t]].name : "");
    else if (t == TOK_OPEN)  s += "(";
    else if (t == TOK_CLOSE) s += ")";
    else if (t == TOK_SER)   s += " -> ";
    else                     s += " || ";
  }
  return s;
}

std::string isfgText(const Model& M, const Isfg& g, const std::vector<int>* hosts) {
  std::string s;
  for (int li = 0; li < M.L && li < static_cast<int>(g.pick.size()); li++) {
    const Structure& st = M.structs[li][g.pick[li]];
    // A level whose top is a parallel set is wrapped: f1 -> (f2 || f3) -> f4
    int depth = 0; bool topPar = false;
    for (int t : st.expr) { if (t == TOK_OPEN) depth++; else if (t == TOK_CLOSE) depth--; else if (t == TOK_PAR && depth == 0) topPar = true; }
    if (li) s += " -> ";
    s += topPar ? "(" + structText(M, st, hosts) + ")" : structText(M, st, hosts);
  }
  return s;
}

json::Value schemeJson(const Model& M, const Scheme& s) {
  json::Value o = json::Value::object();
  o.set("ok", s.ok);
  o.set("source", s.source);
  if (s.hosts.empty() || s.g.empty()) return o;
  o.set("lat", s.lat);
  json::Value hosts = json::Value::array();
  for (int h : s.hosts) hosts.push(h);
  o.set("hosts", hosts);
  json::Value chain = json::Value::array();
  for (int f : s.chain) chain.push(f);
  o.set("chain", chain);
  json::Value br = json::Value::object();
  br.set("trans", s.br.trans); br.set("proc", s.br.proc); br.set("rep", s.br.rep); br.set("int", s.br.integ);
  o.set("br", br);
  std::vector<double> used; std::vector<char> reuse;
  placeInstances(M, s.hosts, used, reuse);
  json::Value use = json::Value::array();
  for (double u : used) use.push(u);
  o.set("use", use);
  json::Value ru = json::Value::array();
  for (char c : reuse) ru.push(c != 0);
  o.set("reuse", ru);
  ResCost rc = resourceCost(M, s.hosts, s.g, s.hop);
  json::Value res = json::Value::object();
  res.set("bw", rc.bw); res.set("cpu", rc.cpu); res.set("norm", rc.norm);
  res.set("links", rc.links); res.set("reused", rc.reused); res.set("created", rc.created);
  o.set("res", res);
  o.set("routing", s.hop ? "fewest-hops" : "lowest-delay");

  // Execution DAG for the diagram: column = depth from the ingress, row = order in the column.
  // Ingress is id -1 (column 0), egress id -2 (last column).
  std::vector<int> crit{ -1 };
  crit.insert(crit.end(), s.chain.begin(), s.chain.end());
  crit.push_back(-2);
  auto isCrit = [&](int a, int b) {
    for (std::size_t i = 0; i + 1 < crit.size(); i++) if (crit[i] == a && crit[i + 1] == b) return true;
    return false;
  };
  std::vector<int> col(M.V.size(), 0);
  int maxCol = 0;
  for (int f : s.g.topo) {
    for (int p : s.g.pred[f]) col[f] = std::max(col[f], (p < 0 ? 0 : col[p]) + 1);
    maxCol = std::max(maxCol, col[f]);
  }
  std::vector<int> rows(maxCol + 2, 0);
  json::Value dag = json::Value::object(), dn = json::Value::array(), de = json::Value::array();
  for (int f : s.g.topo) {
    json::Value x = json::Value::object();
    x.set("f", f); x.set("col", col[f]); x.set("row", rows[col[f]]++);
    x.set("name", M.V[f].name); x.set("host", M.cfg.nodes[s.hosts[f]].name);
    x.set("proc", M.V[f].proc); x.set("crit", std::find(s.chain.begin(), s.chain.end(), f) != s.chain.end());
    dn.push(x);
  }
  auto edge = [&](int a, int b) {
    json::Value e = json::Value::object();
    e.set("a", a); e.set("b", b); e.set("crit", isCrit(a, b));
    de.push(e);
  };
  for (int f : s.g.topo) for (int p : s.g.pred[f]) edge(p, f);
  for (int p : s.g.last) edge(p, -2);
  json::Value rowCount = json::Value::array();
  for (int c = 1; c <= maxCol; c++) rowCount.push(rows[c]);
  dag.set("nodes", dn); dag.set("edges", de); dag.set("cols", maxCol); dag.set("rows", rowCount);
  dag.set("src", M.cfg.nodes[M.cfg.src].name); dag.set("dst", M.cfg.nodes[M.cfg.dst].name);
  o.set("dag", dag);

  // Physical routes for the UI: every I-SFG edge, plus the critical chain
  json::Value routes = json::Value::array();
  auto addRoute = [&](int a, int b, bool cr) {
    json::Value r = json::Value::object();
    json::Value nodes = json::Value::array();
    for (int x : route(M, a, b, s.hop)) nodes.push(x);
    r.set("nodes", nodes); r.set("crit", cr);
    routes.push(r);
  };
  for (int f : s.g.topo) for (int p : s.g.pred[f]) addRoute(p < 0 ? M.cfg.src : s.hosts[p], s.hosts[f], false);
  for (int p : s.g.last) addRoute(s.hosts[p], M.cfg.dst, false);
  std::vector<int> ch{ M.cfg.src };
  for (int f : s.chain) ch.push_back(s.hosts[f]);
  ch.push_back(M.cfg.dst);
  for (std::size_t i = 0; i + 1 < ch.size(); i++) addRoute(ch[i], ch[i + 1], true);
  o.set("routes", routes);

  o.set("text", isfgText(M, s.g, &s.hosts));
  o.set("structText", isfgText(M, s.g));
  return o;
}

json::Value spaceJson(const Model& M) {
  json::Value o = json::Value::object();
  json::Value lv = json::Value::array();
  for (int li = 0; li < M.L; li++) {
    const int m = static_cast<int>(M.layers[li].size());
    json::Value x = json::Value::object();
    x.set("vnfs", m);
    x.set("structures", static_cast<double>(M.structs[li].size()));
    double staged = 0; for (const auto& st : M.structs[li]) if (st.staged) staged += 1;
    x.set("staged", staged);
    lv.push(x);
  }
  o.set("levels", lv);
  o.set("isfg", M.cfg.allIsfg ? "all" : "staged");
  o.set("structures", structureCount(M).str());
  BigCount pl(1);
  for (std::size_t i = 0; i < M.V.size(); i++) pl *= BigCount(static_cast<unsigned>(M.n));
  o.set("placements", pl.str());
  o.set("total", bruteForceSize(M).str());
  double approx = 1;
  for (const auto& st : M.structs) approx *= static_cast<double>(st.size());
  o.set("totalApprox", approx * std::pow(static_cast<double>(M.n), static_cast<double>(M.V.size())));
  o.set("dpgPaths", M.totalPaths.str());
  o.set("countBackend", BigCount::backend());
  return o;
}

// ─── Timer ────────────────────────────────────────────────────
#ifdef _WIN32
#include <windows.h>
double nowMs() {
  static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
  LARGE_INTEGER t; QueryPerformanceCounter(&t);
  return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}
#else
#include <chrono>
double nowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif
