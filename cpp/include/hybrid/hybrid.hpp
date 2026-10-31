/* =============================================================
   LOA Hybrid — The combined flow
     1 build model          (model.cpp)
     2 seed with SCA + VDA  (sca_vda.cpp)       Solution A
     3 full search          (full.cpp)          every I-SFG × every placement
     4 LOA / ROA search     (loa.cpp)           paper 1: fallback if the search
                                                ran out of time, or for `compare`
     5 certify              (hybrid.cpp)
   Input: the config JSON from the website. Output: one report JSON.
   ============================================================= */
#pragma once
#include "common/json.hpp"

json::Value runHybrid(const json::Value& config);
// Validation, warnings and search-space size, without running anything (the setup page)
json::Value checkConfig(const json::Value& config);
