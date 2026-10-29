/* =============================================================
   LOA Hybrid — Problem model shared by every algorithm
   Config (what the UI sends) → Model (shortest delays, layers,
   I-SFG structures) + the common latency / feasibility evaluators.
   Every algorithm is scored with the SAME evalScheme, so any gap
   between them comes from the algorithm, not the cost model.

   Execution structure (I-SFG, paper 2 Section III-C):
     the dependency levels are executed one after another; inside
     a level the VNFs form a series-parallel DAG, e.g. (f2 → f3) ∥ f4.
     Every VNF that ends a level sends its output to every VNF that
     starts the next one, so the packet is copied (divided) at a fork
     and merged at a join.
   Latency of a scheme (paper 1, Eqs. 3-7 / paper 2, Eqs. 3-11):
     longest ingress → egress branch of
       link delay + μ_f·l_u + (out(f) − 1)·τ_rep·l_u + (in(f) − 1)·τ_int·l_u
   ============================================================= */
#pragma once
#include <string>
#include <vector>
#include <limits>
#include "common/json.hpp"
#include "common/bigcount.hpp"

constexpr double EPS = 1e-9;
const double INF = std::numeric_limits<double>::infinity();

// ─── User-facing configuration ────────────────────────────────
struct NodeCfg { std::string name; double cap = 0, fault = 0, x = 0, y = 0; };
struct LinkCfg { int a = 0, b = 0; double delay = 0, bw = 1e18; };     // bw = residual bandwidth (Mbps)
struct VnfCfg  { std::string name, type; int level = 1; double proc = 0, dem = 0, fault = 0; };
// An already-running VNF instance that later requests may share (paper 1, Section III-B)
struct InstanceCfg { int node = 0; std::string type; double residual = 0; };

struct Config {
  std::vector<NodeCfg> nodes;
  std::vector<LinkCfg> links;
  std::vector<VnfCfg>  vnfs;
  int src = 0, dst = 0;
  // Per unit of data: proc (μ_f, on each VNF), trep (τ_rep, dividing), tint (τ_int, merging).
  // pkt = l_u, the request's data size; every one of the three is multiplied by it.
  double trep = 0, tint = 0, tmax = 1e9, pkt = 1;

  // Reliability (paper 2, adapted to one request):
  //   r(f on v) = 1 − (β_f + β_v + α · (load(v) − 1)),  must be ≥ rmin
  double rmin = 0, alpha = 0;

  // Resources (paper 1, ROA):
  //   breq     request bandwidth b_u; links with less residual bandwidth are unusable
  //   qcpu     load this request puts on a shared instance (q_u^cpu)
  //   instCap  capacity of one VNF instance (C_n^max); a new instance starts with instCap − qcpu
  //   wBw      weight α of normalised bandwidth cost (CPU weight β = 1 − α)
  double breq = 0, qcpu = 0, instCap = 100, wBw = 0.5;
  std::vector<InstanceCfg> instances;

  // Structure space inside a dependency level:
  //   allIsfg = true   every series-parallel I-SFG (paper 2, Algorithm 2's space)
  //   allIsfg = false  only stage sequences, e.g. (f2 ∥ f3) → f4 (the earlier model)
  bool allIsfg = true;

  // Search limits and switches
  long   kmax     = 2048;   // cap on the k-shortest window
  long   candCap  = 30000;  // cap on candidate schemes per anchor
  double fullMs   = 20000;  // time budget of the full-combination search (ms)
  bool   full     = true;   // run the full-combination search (every I-SFG × every placement)
  bool   seed     = true;   // Solution A: seed LOA with VDA
  bool   order    = true;   // Solution E: try SCA-ranked arrangements first
  bool   compare  = true;   // also run plain (unseeded) LOA for comparison
  bool   roa      = true;   // also run ROA (paper 1, Algorithms 4 + 5)
};

Config parseConfig(const json::Value& v);
std::string validateConfig(const Config& c);   // "" when valid
std::vector<std::string> configWarnings(const Config& c);

// ─── Internal model ───────────────────────────────────────────
// proc already includes l_u (μ_f · l_u)
struct Vnf { int idx; std::string name, type; double proc, dem, fault; int layer, pos; };

/**
 * One dependency level's execution structure: a series-parallel DAG over the
 * level's VNFs. Token stream `expr` keeps the nesting for printing:
 * ≥ 0 = VNF index, TOK_OPEN '(', TOK_CLOSE ')', TOK_SER '→', TOK_PAR '∥'.
 */
enum { TOK_OPEN = -1, TOK_CLOSE = -2, TOK_SER = -3, TOK_PAR = -4 };
struct Structure {
  std::vector<std::pair<int, int>> edges;   // (a, b): a's output goes to b, both in this level
  std::vector<int> in, out;                 // VNFs fed by the previous level / feeding the next one
  std::vector<unsigned> desc;               // per level position: bitmask of descendants (by position)
  std::vector<int> expr;                    // printable form
  bool staged = false;                      // also a plain stage sequence
};

/** A complete I-SFG: the level structures joined one after another. */
struct Isfg {
  std::vector<int> pick;                    // structure index per level (into Model::structs)
  std::vector<std::vector<int>> pred;       // per VNF: predecessors, -1 = ingress
  std::vector<std::vector<int>> succ;       // per VNF: successors, -2 = egress
  std::vector<int> first, last;             // VNFs fed by ingress / feeding egress
  std::vector<int> topo;                    // topological order of the VNFs
  bool empty() const { return pick.empty(); }
};

struct Model {
  Config cfg;
  int n = 0, L = 0;
  double trep = 0, tint = 0;            // τ_rep · l_u and τ_int · l_u
  std::vector<std::vector<double>> d;   // all-pairs shortest delay
  std::vector<std::vector<int>>    nx;  // next hop for route()
  std::vector<std::vector<int>>    hop; // hops on the fewest-hop route (ties: lower delay)
  std::vector<std::vector<double>> dh;  // delay along that fewest-hop route
  std::vector<std::vector<int>>    nxh; // next hop for the fewest-hop route
  std::vector<std::vector<std::pair<std::string, double>>> inst;  // per node: (type, residual)
  double cbwMax = 1, ccpuMax = 1;       // normalisers of the ROA cost
  std::vector<Vnf> V;
  std::vector<std::vector<int>> layers;
  std::vector<std::vector<Structure>> structs;  // per level, SCA-ranked if cfg.order
  std::vector<std::vector<double>>    structScore;
  BigCount totalPaths;
  double lmin = INF;                    // smallest link delay (Theorem 1)
};

Model buildModel(const Config& cfg);
// Every series-parallel structure (allIsfg) or every stage sequence over `items`
std::vector<Structure> layerStructures(const std::vector<int>& items, bool allIsfg);
double layerScore(const Model& M, const Structure& st);   // placement-free delay of one level
BigCount countPaths(const Model& M);
BigCount structureCount(const Model& M);                  // number of complete I-SFGs
BigCount bruteForceSize(const Model& M);                  // I-SFGs × |V|^|F|
std::vector<int> route(const Model& M, int a, int b, bool fewestHops = false);

Isfg makeIsfg(const Model& M, const std::vector<int>& pick);
// Index of the all-serial (index order) or all-parallel structure of a level
int serialStructure(const Model& M, int layer);
int parallelStructure(const Model& M, int layer);

// Placement-free completion after VNF f finishes (rep/int/proc only, no links), per VNF,
// plus the same from the ingress (`head`). Used as lower bounds by the searches.
struct Tails { std::vector<double> tail; double head = 0; };
Tails placementFreeTails(const Model& M, const Isfg& g);

// ─── Schemes and their evaluation ─────────────────────────────
struct Breakdown { double trans = 0, proc = 0, rep = 0, integ = 0; };

// Resource cost of a scheme (paper 1, Eqs. 1, 2, 16)
struct ResCost {
  double bw = 0;      // b_u × number of distinct links used
  double cpu = 0;     // resources of newly created instances
  double norm = 0;    // α·bw/Cbw_max + β·cpu/Ccpu_max
  int links = 0, reused = 0, created = 0;
};

struct Scheme {
  bool ok = false;
  std::vector<int> hosts;     // VNF index → node index
  Isfg g;                     // execution structure
  double lat = INF;
  Breakdown br;
  std::vector<int> chain;     // critical (slowest) branch
  std::string source;         // "VDA", "LOA", "Full search", ...
  bool hop = false;           // routed on fewest-hop paths (ROA) instead of lowest-delay paths
  ResCost res;
  std::vector<char> reuse;    // per VNF: shares an existing instance
};

struct EvalResult { double lat; std::vector<int> chain; Breakdown br; };

EvalResult evalScheme(const Model& M, const std::vector<int>& hosts, const Isfg& g, bool fewestHops = false);
bool hostsFeasible(const Model& M, const std::vector<int>& hosts, std::string* why = nullptr);
// Decide, VNF by VNF, whether it shares an instance or needs a new one.
// Fills newUse[node] (capacity taken by new instances) and reuse[f]; returns total new-instance demand.
double placeInstances(const Model& M, const std::vector<int>& hosts, std::vector<double>& newUse, std::vector<char>& reuse);
bool canReuse(const Model& M, int f, int host);   // an existing instance of f's type on host has room
ResCost resourceCost(const Model& M, const std::vector<int>& hosts, const Isfg& g, bool fewestHops);
double vnfReliability(const Model& M, int f, int host, int load);
// Every consecutive pair in `order` (VNFs of one level) is an ancestor → descendant pair in st
bool structOK(const Model& M, const Structure& st, const std::vector<int>& order);
Scheme makeScheme(const Model& M, const std::vector<int>& hosts, const Isfg& g, const std::string& source, bool fewestHops = false);

// ─── Text / JSON helpers ──────────────────────────────────────
std::string fmt(double x);
std::string structText(const Model& M, const Structure& st, const std::vector<int>* hosts = nullptr);
std::string isfgText(const Model& M, const Isfg& g, const std::vector<int>* hosts = nullptr);
json::Value schemeJson(const Model& M, const Scheme& s);
json::Value spaceJson(const Model& M);   // structure / placement counts of this instance

// High-resolution wall clock in milliseconds (QueryPerformanceCounter on Windows)
double nowMs();
