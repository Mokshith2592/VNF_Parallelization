/* =============================================================
   LOA Hybrid — Paper 2: SCA + VDA (Tian, Liang & Liu, TWC 2023)
   SCA  (Algorithm 2): pick the I-SFG with the lowest placement-free
        delay (execution + dividing + merging), before any placement.
   VDA  (Algorithm 3): place VNFs greedily in I-SFG order, each on the
        node closest to its predecessors' hosts that still fits.
   Result is used as the SEED (initial best) of LOA (Solution A).
   ============================================================= */
#pragma once
#include "common/model.hpp"

struct ScaResult {
  std::vector<int> pick;            // chosen structure index per level
  Isfg g;                           // chosen I-SFG
  double placementFree = INF;       // exec + replication + consolidation
  double layouts = 0;               // number of I-SFGs SCA considered
};

struct VdaResult {
  Scheme scheme;
  std::string failReason;           // set when VDA cannot place a VNF
};

ScaResult runSCA(const Model& M);
VdaResult runVDA(const Model& M, const Isfg& g);

// VDA run once for every I-SFG SCA can build; keeps the best.
// Skipped (tried = 0) when there are more than `maxLayouts`.
struct VdaAllResult { Scheme scheme; double tried = 0; };
VdaAllResult runVDAAllLayouts(const Model& M, double maxLayouts = 20000);
