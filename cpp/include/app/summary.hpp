/* =============================================================
   LOA Hybrid — Paper-comparison summary (served as GET /api/summary)
   Reads the experiment results (web/data/results/<id>.json, written by
   loa_experiments.exe) and the numbers paper 1 reports
   (web/data/paper_results.json), and computes every derived value the
   results page shows: improvements over VDA, savings over the best
   baseline, averages, acceptance. results.js only draws them.
   ============================================================= */
#pragma once
#include "common/json.hpp"
#include <string>

json::Value resultsSummary(const std::string& webRoot);
