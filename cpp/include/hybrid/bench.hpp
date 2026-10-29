/* =============================================================
   LOA Hybrid — Self-test and feasibility benchmark
     loa_hybrid --selftest [N]
         N random small instances; the full search must agree with
         the plain enumeration (runBrute) on both objectives, and no
         heuristic may beat it. Exit code 0 only if every check holds.
     loa_hybrid --feasibility [--runs R] [--budget MS] [--out FILE]
         For each (number of VNFs, number of nodes) on a grid, R random
         instances in paper 1's experimental setting: how long the full
         search takes, whether it finishes inside the budget, and how far
         paper 1's LOA is from the optimum it proves. Writes JSON for
         web/feasibility.html.
   ============================================================= */
#pragma once
#include <string>

int runSelfTest(int instances);
int runFeasibility(int runs, double budgetMs, const std::string& out);
