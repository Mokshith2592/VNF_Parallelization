/* =============================================================
   LOA Hybrid — Paper 1: LOA (Algorithms 2 + 3) and ROA (4 + 5)
   Both run the same search on the DPG; only the weights differ:
     LOA  delay-weighted DPG     edge = link delay, node = processing delay
                                 scheme cost = end-to-end latency
     ROA  resource-weighted DPG  edge = α·b_u·hops / Cbw_max
                                 node = β·(0 if an instance is reused, else c_n) / Ccpu_max
                                 scheme cost = α·C̃bw + β·C̃cpu  (Eq. 16), latency ≤ T_max
   Additions over the paper:
     • optional seed scheme (Solution A): best starts at the seed's cost
     • exact stop reason, candidate-cap flag (Solution B)
     • reliability inside the feasibility filter (Solution C)
   ============================================================= */
#pragma once
#include "common/model.hpp"

enum class Objective { Latency, Resource };

struct Hop  { int f, h; };
struct Path { int id = 0; std::vector<Hop> seq; double cost = 0; };

enum class Stop {
  PrunedBound,   // A* lower bound already above best (Alg. 2/4 lines 3-4)
  PrunedNext,    // next path costs more than best (Alg. 2/4 lines 18-21, Lemma 4.3)
  Exhausted,     // the whole DPG was inside the window
  KLimit,        // k reached cfg.kmax: NOT a proof of optimality
};
const char* stopName(Stop s);

struct LoaStats {
  int    rounds = 0;
  long   finalK = 0;
  long   pathsGenerated = 0;   // sum over rounds of paths taken from the DPG
  long   infeasiblePaths = 0;
  long   stabilityRemoved = 0;
  long   anchors = 0;
  double candidates = 0;
  double feasibleCandidates = 0;
  bool   capped = false;       // some anchor hit cfg.candCap
  double ms = 0;
};

struct LoaEvent { std::string phase; int round; long k; double best; std::string text; };

struct LoaResult {
  Objective obj = Objective::Latency;
  Scheme   best;               // final LM-PDS / RM-PDS (may still be the seed)
  double   bestCost = INF;     // latency (LOA) or normalised resource cost (ROA)
  bool     improvedSeed = false;
  Stop     stop = Stop::Exhausted;
  LoaStats st;
  std::vector<LoaEvent> trace;
};

LoaResult runSearch(const Model& M, Objective obj, const Scheme* seed);
inline LoaResult runLOA(const Model& M, const Scheme* seed) { return runSearch(M, Objective::Latency, seed); }
inline LoaResult runROA(const Model& M, const Scheme* seed) { return runSearch(M, Objective::Resource, seed); }

// Cost of a scheme under an objective (INF when it breaks T_max or capacity)
double schemeCost(const Model& M, const Scheme& s, Objective obj);

json::Value loaJson(const Model& M, const LoaResult& r);
