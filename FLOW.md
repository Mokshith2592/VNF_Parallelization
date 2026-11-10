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
                                      6 hybrid/sync.cpp     syncMerges(final)     packet sync at merges (§10)
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
| `sync` (gap, buffer, latency, CPU before → after; per merge; per VNF speed) | `syncJson()` | "Merge synchronisation" panel |
| `solveMs` (seed + search, plus LOA only when it was the fallback) | `runHybrid()` | time in the "Final" comparison row |

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
- the placement-free head must equal the sum of the level scores;
- packet sync (§10) must never raise latency, gap or buffer, must keep speeds in [sMin, sMax] and shared instances at 1, and must not push a node over its capacity.

Last run (2026-11-01, `--selftest 1500`): 1,297 instances checked, 203 skipped as too large or disconnected, all agree. Paper 1's LOA has never stopped on its own bound above the optimum; ROA has (see HANDOVER B1), so ROA's stop is never used as a proof.

Presets (the web page's buttons; the same configs are in `cpp/tests/preset_*.json`):

| Preset | Lowest latency | What it shows |
|---|---|---|
| D: parallel wins | 58 ms, OPTIMAL | f2 ∥ f3 is fastest |
| E: serial wins | 50 ms, OPTIMAL | heavy copy / merge cost makes serial best |
| G: branch inside a level | 68 ms, OPTIMAL: `f1 → ((f3 → f2) ∥ f4) → f5` | paper 2, Fig. 1(g); stage sequences give 76 ms |
| R: reliability | 58 ms, OPTIMAL | R_min = 0.95 stops co-locating VNFs |
| S: instance reuse | 55 ms, OPTIMAL | ROA shares existing instances |
| L: 8 nodes, 6 VNFs | 67 ms, OPTIMAL | 5 M schemes; LOA stops on its k limit |
| P: packet sync | 20 ms, OPTIMAL; 15 ms after sync | 5 ms and 15 ms branches: gap 10 → 0 ms, buffer 500 → 0 kbit |
| X1: LOA misses f4 ∥ f5 | 44 ms, OPTIMAL (LOA 49, VDA 52) | LOA at k ≤ 256, 200 candidates |
| X2: LOA misses the mixed layout | 48 ms, OPTIMAL (LOA 55, VDA 58) | `(f1 → f3) ∥ f2`; LOA at k ≤ 256, 200 candidates |

X1 and X2 use the experiments' search limits on purpose. With no limits, LOA reaches 44 and 48 ms too (k = 768 and 3,072); in 820 random small networks, unlimited LOA always reached the optimum.

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

Results of the run on 2026-10-28, before the SCA / VDA fixes and the new pipeline order (full tables in `web/feasibility.html`; re-run the command above to refresh them). The newer same-request test in §11 measures the same question on networks that fill up with requests.

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
set PATH=D:\new_software\ucrt64\bin;%PATH%
build.bat                       :: build and serve http://localhost:8080
mingw32-make                    :: or: build both programs with the Makefile
bin\loa_hybrid.exe tests\preset_l.json   :: one config, report JSON on stdout
run_experiments.bat 10          :: paper 1's Section V sweep, 10 runs per point (5–6 hours)
run_experiments.bat 10 netsize  :: one experiment only
```

`run_experiments.bat` reads two limits from the environment, `KMAX` (default 1024) and `CAND` (default 200); paper 1 sets none. It writes `results\<name>.json` through a `.tmp` file, so a stopped run keeps the old results, and copies finished files to `web\data\results\`. Keep the window open and the PC awake until it prints `Done`.

The same-request test (§11) is a mode of the experiment program:

```bat
bin\loa_experiments.exe --paired --runs 3 --kmax 1024 --cand 200 --full-ms 2000 --only sfclen --out results\paired_sfclen.json
```

Options: `--only <experiment>`, `--runs N`, `--reqs N` (requests per run), `--sfc N` (chain length for every point), `--kmax`, `--cand`, `--full-ms`, `--staged`.

Pages:

- `index.html`: calculator;
- `feasibility.html`: how large n can get;
- `results.html`: paper comparison.

The C++ is plain C++14 and the standard library (plus `ws2_32` on Windows). It was built with MSYS2 UCRT64 g++ 16.1, and `ucrt64\bin` must be on PATH.

## 8. Folder layout

```
cpp/src (headers in cpp/include, same folders)
  common/  json        JSON value / parser / writer
           model       config, I-SFG structures, evaluators (latency, resources, feasibility), scheme JSON
           bigcount    exact (GMP) or saturating counter for huge counts (header only)
  paper1/  loa         paper 1: LOA / ROA (one DPG search, two weightings)
  paper2/  sca_vda     paper 2: SCA, VDA
  hybrid/  hybrid      pipeline, certificate, comparison rows, /api/check
           full        full-combination search + plain enumeration (test oracle)
           sync        packet sync at merges (§10)
           bench       --selftest, --feasibility
  app/     main        entry point of loa_hybrid.exe
           server      HTTP server: /api/run, /api/check, /api/summary, static files
           summary     /api/summary (paper comparison numbers)
           experiments second program, loa_experiments.exe: paper 1's Section V, --paired
cpp/tests    preset_*.json (d, e, g, l, r, s, sync, x1, x2), count_sp.py
cpp/results  raw experiment output (*.json, *.log); not in git
web/         index.html, feasibility.html, results.html, js/ (render only), data/
```

## 9. Where this differs from the papers

The code was checked line by line against both papers on 2026-10-30.

**Matches:** the delay model (paper 1, Eqs. 4, 6, 7); LOA / ROA (k = |V|+1 and doubling, feasibility, stability filter, coverage, smallest covering prefix, anchors, both pruning rules); ROA weights and instance reuse; node and instance capacity; paper 2's Theorem 1 bound.

**Fixed after the check:**

- **SCA** now adds paper 2's average link delay T̂ (Eqs. 15–16): `scaScore`, with l̂ = mean shortest delay between node pairs. The full search keeps the placement-free `structScore`, because its bound needs it.
- **VDA** now follows Alg. 3: it compares delay across nodes that can share an instance, stops at the first node that can host a new one, and keeps at most one instance per type on a node (paper 2, Definition 2).

**Still differs:**

- **Search limits.** Paper 1 keeps doubling k with no limit and has no candidate cap; the code has `kmax` and `candCap` so it always finishes. Every LOA "failure" in the experiments comes from these limits.
- **I-SFG between levels.** Every last VNF of a level feeds every first VNF of the next (series composition); a branch can't skip a level. Inside a level every series-parallel I-SFG is allowed; non-series-parallel DAGs (an "N" shape) are not.
- **Transmission delay** is the per-link delay on shortest paths (paper 1, Section V), not Eq. 3's l_u / b_u.
- **Source fork and egress merge** are charged as copy / merge costs; the papers sum only over VNFs.
- **SCA's candidate set** is every I-SFG of a level, a superset of paper 2's incremental construction.
- **VDA** ignores the delay of users already sharing an instance: here sharing does not slow anyone down, so it is the same on every node.
- **Reliability** counts this request's VNFs on a node and adds a node fault rate; paper 2 counts the users sharing that VNF.
- **ROA normalisation** during the search uses fixed per-request scales; paper 1's maximums are only known after comparing schemes.
- **The ILP (paper 2, Algorithm 1)** is replaced by the full search, which is exact when it finishes. Paper 1's Serial, ParaDSP and PODPR baselines are not built; `Serial*` is VDA on an all-serial layout.

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

Measured with `--paired` (capacity, SFC length, requests and heavy-tailed sweeps, 60 nodes, 3 runs per point): 1,858 of 1,884 requests had an uneven merge. On those, the largest gap fell from 5.12 to 1.40 ms on average, the largest buffer from 336 to 79 kbit, latency from 89.0 to 87.0 ms and CPU from 201 to 195. The extra CPU was not charged against later requests, and ROA schemes are not synchronised.

---

## 11. Results (as of 2026-10-31)

**Same-request test** (`--paired`): each request is solved by plain LOA (k ≤ 1024, 200 candidates) and by the hybrid (VDA, full search with a 2 s budget, LOA fallback) on the same residual network.

| Setting | Requests | LOA found | Hybrid found | Proven optimal | Hybrid lower delay | Delay on both, LOA / hybrid (ms) | Mean time, LOA / hybrid (ms) |
|---|---|---|---|---|---|---|---|
| 60 nodes, SFC 5–10 | 540 | 485 | 540 | 540 | 81 of 485 | 86.1 / 84.1 | 494 / 3.5 |
| 10–300 nodes, SFC 7 | 854 | 795 | 851 | 854 | 105 of 795 | 79.2 / 78.3 | 497 / 0.9 |
| 60 nodes, SFC 12 | 300 | 40 | 300 | 231 | 29 of 40 | 123.2 / 109.1 | 520 / 754 |
| 60 nodes, SFC 14 | 150 | 5 | 150 | 72 | 3 of 5 | 159.9 / 126.9 | 374 / 1,345 |

The hybrid was worse on none of the 1,844 requests; the 3 it did not solve were proven to have no scheme. The search ran out of time on 147 requests (SFC 12–14), and LOA as the fallback improved none of them. Starting the search from VDA alone gave the same answers as starting it from LOA on 990 of 990 requests, which is why the pipeline runs the search before LOA.

**Paper 1's sweep** (`run_experiments.bat 10`, k ≤ 1024, 200 candidates, `web/data/results/`): plain LOA accepts 89–99 % of requests and the VDA-seeded hybrid 94–100 %; both beat VDA by about 10–20 ms. LOA still hits a search limit on 65–85 % of requests. In this sweep "Hybrid" is VDA-seeded LOA without the full search, and its delay is averaged over more (harder) accepted requests, so use the same-request test to compare methods.

**What the results mean.** With no search limits, LOA reached the optimum in all 820 small networks tried, as paper 1's Lemma 4.4 predicts. The hybrid's advantage is an exact, certified answer in milliseconds where LOA needs a much larger search, plus the packet sync of §10.
