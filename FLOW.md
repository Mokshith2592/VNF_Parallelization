# LOA Hybrid: how the code works

You enter a MEC network and a service chain on the website. The C++ engine:

1. runs **paper 2** (Tian et al., TWC 2023): SCA picks an I-SFG, VDA places it greedily;
2. runs the **full-combination search**: every I-SFG × every placement, with the packet dividing and merging costs. It starts from the VDA scheme and returns a proven optimum when it finishes;
3. runs **paper 1** (Xie et al., ToN 2026), LOA (Algorithms 2 + 3, lowest latency) and ROA (Algorithms 4 + 5, least resources), seeded with VDA, as the fallback when the search ran out of time. With `compare` on they also run for the comparison table, which does not change the answer;
4. certifies the result;
5. synchronises the merges of the final scheme (section 10) and sends everything back to the page.

**All logic is in C++.** The browser only renders what the engine returns: `/api/run` (the report), `/api/check` (validation and search-space size while you edit), `/api/summary` (paper comparison), and `data/feasibility.json`, which the engine writes.

## Source layout (`cpp/src`, headers mirrored in `cpp/include`)

| Folder | Files | What |
|---|---|---|
| `paper1/` | `loa.cpp` | Paper 1: DPG, k shortest paths, anchor search. LOA (Alg. 2 + 3) and ROA (Alg. 4 + 5) |
| `paper2/` | `sca_vda.cpp` | Paper 2: SCA (Alg. 2, pick the I-SFG) and VDA (Alg. 3, greedy placement) |
| `hybrid/` | `hybrid.cpp`, `full.cpp`, `sync.cpp`, `bench.cpp` | Our part: the pipeline, full-combination search, LOA/ROA fallback, certificate, self-test and feasibility benchmark |
| `common/` | `model.cpp`, `json.cpp`, `bigcount.hpp` | Shared by all three: network, levels, I-SFG structures, the delay/resource model (incl. dividing and merging), JSON |
| `app/` | `main.cpp`, `server.cpp`, `summary.cpp`, `experiments.cpp` | Entry points: CLI, HTTP server, paper-comparison summary, `loa_experiments.exe` |

`paper1/` and `paper2/` depend only on `common/`, never on each other; `hybrid/` uses both. Includes are written with the folder, e.g. `#include "paper1/loa.hpp"`.

---

## 1. Why the full search (what paper 1 leaves out)

Paper 1's Algorithm 2 takes the cheapest DPG paths, picks the costliest of them as the **anchor**, keeps the hosts and order of the anchor's VNFs, and only builds schemes around it ("partial group reuse for F(p), full construction for F \ F(p)"). Schemes that move an anchor VNF are never built from that anchor. Paper 1 also builds only part of paper 2's structure space.

The full search removes both restrictions:

| | Paper 1 (LOA / ROA) | Full search (`full.cpp`) |
|---|---|---|
| Structures inside a dependency level | the ones compatible with the anchor | every I-SFG of paper 2 |
| Placements | anchor VNFs fixed; others on hosts seen in the path window | every VNF on every node |
| Dividing / merging cost | evaluated after a scheme is built | inside the search and its bound |
| Result | optimal only if Lemmas 4.3 / 4.4 hold and no limit was hit | optimal whenever it finishes |

---

## 2. Model (`model.hpp`, `model.cpp`)

### Execution structure (I-SFG)

The dependency levels run one after another. Inside a level, the VNFs form a **series-parallel DAG**: one VNF, a series of parts, or parallel parts. For example, with level {f2, f3, f4}:

```
f2 -> f3 -> f4         serial
(f2 || f3) -> f4       stage sequence (the only kind the earlier engine had)
(f2 -> f3) || f4       a branch inside the level (paper 2, Fig. 1(g)): only possible now
f2 || f3 || f4         fully parallel
```

Every VNF that ends a level feeds every VNF that starts the next one. `layerStructures()` enumerates the structures through their unique series / parallel decomposition, so each one appears exactly once:

| VNFs in a level | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| every I-SFG (`"isfg": "all"`, default) | 1 | 3 | 19 | 195 | 2 791 | 51 303 |
| stage sequences only (`"isfg": "staged"`) | 1 | 3 | 13 | 75 | 541 | 4 683 |

These counts are checked independently by `tests/count_sp.py`, which brute-forces every labeled partial order and also computes the counts from generating functions.

### Latency (paper 1, Eqs. 3–7; paper 2, Eqs. 3–11)

`evalScheme()` computes the longest ingress → egress branch (the critical branch). For each VNF f on it:

```
link delay (shortest path between the hosts)
+ μ_f · l_u                       processing             (proc × pkt)
+ (out(f) − 1) · τ_rep · l_u      dividing / replication  (trep × pkt), out(f) = successors of f
+ (in(f)  − 1) · τ_int · l_u      merging / integration   (tint × pkt), in(f)  = predecessors of f
```

The ingress counts as a VNF with out = |first VNFs|, and the egress as one with in = |last VNFs|. With `pkt = 1` the numbers match the earlier engine on stage sequences: every preset gives the same latency as before.

### Other parts (unchanged)

- Instance reuse: `placeInstances()`.
- Resource cost: `resourceCost()`, b_u × distinct links + new-instance CPU, normalised.
- Capacity and reliability: `hostsFeasible()`.
- Routing: lowest delay (LOA), fewest hops (ROA).

---

## 3. One run, end to end

```
 BROWSER                                  C++  (loa_hybrid.exe)
 ui.js  edit → scheduleCheck()  ── POST /api/check ──►  hybrid.cpp checkConfig():  parseConfig, validateConfig,
        renderSummary()          ◄─────────────────────  configWarnings, buildModel, spaceJson (search-space table)

 ui.js  run() ── POST /api/run ──►  server.cpp handle() → hybrid.cpp runHybrid()
                                      1 common/model.cpp    buildModel            levels, I-SFG structures, shortest paths
                                      2 paper2/sca_vda.cpp  runSCA, runVDA,       seed (paper 2)
                                                            runVDAAllLayouts
                                      3 hybrid/full.cpp     runFull(Latency, seed)
                                                            runFull(Resource, seed)
                                      4 paper1/loa.cpp      runLOA, runROA        fallback if the search ran out
                                                                                  of time; else only for `compare`
                                      5 hybrid/hybrid.cpp   certify, gapJson,     certificate, paper-1 gap,
                                                            cmpRow, seedEffect    comparison table rows
 render.js renderResult() ◄──────────  report JSON
```

| Report field | Made by | Shown as |
|---|---|---|
| `model.space` | `spaceJson()` | stats row, setup table |
| `sca`, `vda`, `vdaAll`, `seed`, `seedRes` | `sca_vda.cpp` | comparison table |
| `loaSeeded`, `loaPlain`, `roaSeeded`, `roaPlain` | `loaJson()` | comparison table, trace |
| `full`, `fullRes` | `fullJson()` | "Full-combination search" panel |
| `certificate`, `certificateRes` (with `gaps`: paper 1 vs the optimum) | `certify()`, `gapJson()` | certificate panels |
| `final`, `finalRes` (with `dag`: diagram layout) | `schemeJson()` | scheme cards, diagram, network |
| `comparison` | `cmpRow()` | comparison table |
| `seedEffect`, `seedEffectRes` | `seedEffect()` | seed table |

---

## 4. Full-combination search (`full.hpp`, `full.cpp`)

It's exact branch and bound. Nothing is skipped unless a bound proves it can't win:

1. **I-SFGs in increasing order of their lower bound.** An I-SFG's placement-free delay (the sum of its levels' `layerScore`, i.e. SCA's score) plus d(s_u, d_u) is a lower bound on every placement of it. Levels are combined best-first with a heap, and each combination is generated exactly once. Once this bound reaches the best latency, **all remaining I-SFGs are skipped at once**.
2. **Placements in topological order.** When a VNF is placed, all its predecessors already are, so its completion time is exact. For every placed VNF,
   `completion(f) + placement-free tail(f) + d(host(f), d_u)`
   is a lower bound on the final latency. This holds because any chain of shortest-path hops from a host to the egress is at least their direct shortest distance. Branches at or above the best are cut. Hosts are tried in increasing order of their bound.
3. **Feasibility on partial placements.** These conditions only get stricter as VNFs are added, so cutting on them is safe:
   - capacity: for each VNF type on a node, the smallest demand of any VNF of that type in the chain;
   - reliability: the load only grows;
   - T_max: from the latency bound.
4. **Resources (ROA objective).** The bound is α·b_u·(distinct links so far) + β·(CPU bound). Ties go to the lower latency, as in the rest of the code.
5. **Time budget.** `fullMs` (default 20 s) stops the search; then `complete = false` and the result is only the best found.

Certificate (`hybrid.cpp certify()`):

| Label | When |
|---|---|
| `OPTIMAL` | the full search finished |
| `OPTIMAL_PROVEN` | it ran out of time, but LOA stopped on its own bound with no limit hit (latency only; ROA's bound stop is not a proof, see HANDOVER B1) |
| `BOUNDED` | neither: the best scheme any method found |
| `INFEASIBLE` / `UNKNOWN` | the full search proved no scheme exists / nothing was found |

---

## 5. Verification

```bat
cd loa-hybrid\cpp
bin\loa_hybrid.exe --selftest 2000      :: full search vs plain enumeration, both objectives
python tests\count_sp.py                :: structure counts, independently
bin\loa_hybrid.exe tests\preset_g.json  :: one preset from the command line
```

`--selftest` builds random small instances: 3–5 nodes, 2–5 VNFs, random capacities, reliability, existing instances, repeated VNF types, bandwidth cuts and T_max. For each one:

- the full search must equal `runBrute()` (every I-SFG × every placement, no pruning) on both objectives, and on the latency of resource ties;
- the seeded full search must reach the same optimum;
- LOA, ROA and VDA must never beat it;
- the placement-free head must equal the sum of the level scores.

Last run: 1735 instances checked, all agree. Plain enumeration took 28.4 s and the full search 1.6 s. Paper 1's LOA stopped on its own bound 528 times and was optimal every time. ROA did 373 times and was not optimal in 16 of them.

| Preset | Lowest latency | Least resources (cost / latency) | Notes |
|---|---|---|---|
| D: parallel wins | 58 ms, OPTIMAL | 0.682 / 58 ms, OPTIMAL | same as before |
| E: serial wins | 50 ms, OPTIMAL | 0.682 / 50 ms, OPTIMAL | before: OPTIMAL_VERIFIED |
| **G: branch inside a level** | **68 ms**, OPTIMAL: `f1 → ((f3 → f2) ∥ f4) → f5` | 0.652 / 68 ms, OPTIMAL | stage sequences give 76 ms |
| R: reliability | 58 ms, OPTIMAL | 0.773 / 76 ms, OPTIMAL | same as before |
| S: instance reuse | 55 ms, OPTIMAL | 0.282 / 61 ms, OPTIMAL | same as before |
| L: 8 nodes, 6 VNFs | 67 ms, OPTIMAL | 0.640 / 69 ms, OPTIMAL | before: BOUNDED (5 M schemes, too many for the old exact check) |

---

## 6. How large can n be? (`--feasibility`, `web/feasibility.html`)

```bat
bin\loa_hybrid.exe --feasibility --runs 5 --budget 10000 --out ..\web\data\feasibility.json
```

**Setting.** Paper 1's experimental setting:

- Waxman topologies, link delay U[10,15] ms;
- μ U[0.045,0.2] ms/packet, l_u U[50,100], τ = 0.02 ms/packet;
- parallelism 0.5, at most 5 VNFs per level.

**Grid.** |F| = 2…12 VNFs × |V| = 6…60 nodes, 5 random instances per cell, 10 s budget. Two scenarios:

- **paper setting**: node capacity U[100,300];
- **tight capacity**: node capacity U[40,80], so a node holds one or two VNFs, like a network loaded by earlier requests.

Results of the run on 2026-10-28 (full tables in `web/feasibility.html`):

| | Paper setting | Tight capacity |
|---|---|---|
| Every instance proven optimal in ≤ 1 s | **up to 12 VNFs** on every network tested (worst: 0.87 s at 20 nodes) | up to **7–10 VNFs**, depending on the network |
| Every instance proven optimal within 10 s | up to 12 VNFs | up to **10 VNFs** on every network (12 on 15 nodes) |
| 12 VNFs | all finish | 1–4 of 5 finish on most networks, 0 of 5 at 60 nodes |
| Plain enumeration of the same spaces (estimated) | up to 5·10²³ ms | up to 4·10²³ ms |
| Paper 1's LOA vs the proven optimum (k ≤ 256, 2000 candidates) | 234 schemes found, 13 above the optimum (up to +45.7 %), 66 instances with no scheme | 136 found, 11 above (up to +24.4 %), 142 with no scheme |

**What this means for n:**

- **Up to 10 VNFs**, the all-combinations search is practical in both scenarios. It proves the optimum, usually in milliseconds and at worst in a few seconds.
- **12 VNFs** is fine when capacity is loose. Under tight capacity it often needs more than 10 s.
- **Beyond that**, the time budget turns the result into `BOUNDED`: the best scheme found, at least as good as paper 1 and paper 2.

Plain enumeration (`runBrute`) stops being usable at about 6 VNFs. The difference comes entirely from the bounds in §4.

---

## 7. Build and run

```bat
cd loa-hybrid\cpp
build.bat                       :: build and serve http://localhost:8080
mingw32-make                    :: or: build both programs with the Makefile
bin\loa_hybrid.exe tests\preset_l.json   :: one config, report JSON on stdout
run_experiments.bat             :: paper 1's Section V experiments (about 2 hours)
```

Pages:

- `index.html`: calculator;
- `feasibility.html`: how large n can get;
- `results.html`: paper comparison.

The C++ is plain C++14 and the standard library (plus `ws2_32` on Windows). It was built with MSYS2 UCRT64 g++ 16.1, and `ucrt64\bin` must be on PATH.

## 8. Folder layout

```
cpp/include, cpp/src
  json       JSON value / parser / writer
  bigcount   exact (GMP) or saturating counter for huge counts
  model      config, I-SFG structures, evaluators (latency, resources, feasibility), scheme JSON
  sca_vda    paper 2: SCA, VDA
  loa        paper 1: LOA / ROA (one DPG search, two weightings)
  full       full-combination search + plain enumeration (test oracle)
  hybrid     orchestration, certificate, comparison rows, /api/check
  bench      --selftest, --feasibility
  summary    /api/summary (paper comparison numbers)
  server     HTTP server: /api/run, /api/check, /api/summary, static files
  main       entry point
  experiments  second program: paper 1's Section V
cpp/tests    preset_*.json, count_sp.py
web/         index.html, feasibility.html, results.html, js/ (render only), data/
```

## 9. Where this differs from the papers

- **I-SFG between levels.** Every last VNF of a level feeds every first VNF of the next (series composition). A branch can't skip a level, as in paper 1's Fig. 3. Inside a level, every series-parallel I-SFG is allowed. Non-series-parallel DAGs (an "N" shape) are not.
- **Transmission delay.** This is the per-link delay on shortest paths, not paper 1's Eq. 3 term l_u / b_u.
- **SCA.** It enumerates every I-SFG of a level and takes the lowest placement-free delay. That's a superset of paper 2's incremental construction, and it ignores the average link delay T̂ of Eq. 15.
- **The ILP (paper 2, Algorithm 1).** It's replaced by the full search, which is exact when it finishes.
- **Instance reuse, normalisers, reliability, ROA tie-break.** These are as before (HANDOVER §2).

---

## 10. Merge synchronisation (`sync.hpp`, `sync.cpp`)

Parallel branches can reach a merge at different times. The early packets wait in the merge node's buffer, and a large gap can overflow it. This is our own extension; neither paper models the buffer.

After the final scheme is chosen, `syncMerges()` shifts CPU between its VNFs. Hosts and the I-SFG do not change.

- **Model.** A VNF at speed s takes `proc / s` and uses `dem · s` CPU, for s in [`sMin`, `sMax`]. This is paper 1's Eq. 5 with no fixed overhead. VNFs on a shared instance keep s = 1.
- **Skew** at a merge = last arrival − first arrival. **Buffer** at a merge = b_u · (sum of each branch's wait), in kbit (Mbps · ms). `bufMax` is the limit per merge; 0 means none.
- **Slow down:** latest VNFs first, each VNF uses its spare time (the smallest wait of its outputs), so nothing after it is delayed.
- **Speed up:** the late chain, upstream of a merge that is still skewed, in steps of 0.05, using free node capacity. A step that widens the worst skew or the worst buffer is undone, because a VNF can feed two merges.
- **Guarantees, checked by `--selftest`:** latency, skew and buffer never rise; speeds stay in range; shared instances stay at 1; no node exceeds its capacity.

`bufMax` only reports overflow; it does not reject a scheme. The certificate refers to the scheme at speed 1, so the latency after synchronisation can be below the certified optimum: it is a different model, with CPU as an extra resource.
