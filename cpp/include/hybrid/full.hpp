/* =============================================================
   LOA Hybrid — Full-combination search (paper 1 + paper 2 merged)
   Paper 1's Algorithm 2 keeps the anchor path's VNFs fixed and only
   builds schemes around it. This search drops that restriction:
   it covers EVERY I-SFG (paper 2's structure space, including the
   dividing and merging costs) × EVERY placement of every VNF, and
   returns a proven optimum when it finishes.

   It is exact branch and bound, not a heuristic:
     • I-SFGs are visited in increasing placement-free delay
       (SCA's score + the shortest ingress → egress delay). Once that
       bound reaches the best latency, every later I-SFG is skipped.
     • Inside an I-SFG, VNFs are placed in topological order; each
       partial placement has an exact completion time per VNF, and
         completion(f) + placement-free tail(f) + d(host(f), egress)
       is a lower bound on the final latency (triangle inequality on
       shortest-path delays). Branches at or above the best are cut.
     • Capacity and reliability are checked on partial placements
       with conditions that only get tighter as VNFs are added.
   A time budget (cfg.fullMs) stops it early; then `complete` is false
   and the result is only the best scheme found.
   ============================================================= */
#pragma once
#include "common/model.hpp"
#include "paper1/loa.hpp"

struct FullEvent { double ms; double cost; std::string text; };

struct FullResult {
  Objective obj = Objective::Latency;
  bool   complete = false;        // whole space covered: best is optimal
  std::string stop;               // "bound", "exhausted", "time budget"
  Scheme best;
  double bestCost = INF;
  bool   improvedSeed = false;
  double space = 0;               // I-SFGs × |V|^|F|
  double isfgTotal = 0;           // I-SFGs in the space
  double isfgSearched = 0;        // I-SFGs whose placements were searched
  double isfgSkipped = 0;         // skipped by the placement-free bound / T_max
  double nodes = 0;               // partial placements expanded
  double leaves = 0;              // complete schemes evaluated
  double cutBound = 0;            // partial placements cut by the lower bound
  double cutFeasible = 0;         // cut by capacity / reliability / T_max
  double ms = 0;
  std::vector<FullEvent> trace;   // every improvement of the best
};

FullResult runFull(const Model& M, Objective obj, const Scheme* seed);
json::Value fullJson(const Model& M, const FullResult& r);

// Plain enumeration of every I-SFG × placement, no pruning at all.
// Test oracle for runFull (selftest); runs only when the space ≤ cap.
struct BruteResult {
  bool ran = false;
  Scheme best, bestRes;
  double evaluated = 0, ms = 0;
};
BruteResult runBrute(const Model& M, double cap);
