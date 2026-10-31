/* =============================================================
   LOA Hybrid — Merge synchronisation
   Parallel branches that reach a merge at different times leave the
   early packets waiting in the merge node's buffer. This step takes
   a finished scheme and shifts CPU between its VNFs so the branches
   arrive closer together:
     - a VNF that finishes early gets less CPU (it may use its spare time)
     - a late VNF gets more CPU, from what was freed or from free capacity
   Processing time follows paper 1, Eq. 5 with no fixed overhead:
   proc / s at CPU dem · s, for a speed s in [sMin, sMax].
   VNFs on a shared instance keep s = 1: other requests depend on them.
   Hosts and the I-SFG do not change, and latency never rises.
   ============================================================= */
#pragma once
#include "common/json.hpp"
#include "common/model.hpp"

struct MergeRow {
  int    f = -2;                  // merging VNF, -2 = egress
  int    node = -1;
  int    branches = 0;
  double skewBefore = 0, skewAfter = 0;   // last arrival − first arrival (ms)
  double bufBefore = 0, bufAfter = 0;     // peak buffer (kbit): b_u · Σ wait of each branch
};

struct SyncResult {
  bool   ok = false;
  std::vector<double> speed;      // per VNF
  std::vector<char>   shared;     // per VNF: on a shared instance, speed fixed
  std::vector<MergeRow> merges;
  double latBefore = 0, latAfter = 0;
  double cpuBefore = 0, cpuAfter = 0;     // CPU of the new instances
  double maxSkewBefore = 0, maxSkewAfter = 0;
  double maxBufBefore = 0, maxBufAfter = 0;
  int    overflowBefore = 0, overflowAfter = 0;   // merges above bufMax
};

SyncResult syncMerges(const Model& M, const Scheme& s);
json::Value syncJson(const Model& M, const SyncResult& r);
