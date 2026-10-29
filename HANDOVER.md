# Developer handover: `loa-hybrid` (AI-generated implementation)

**Audience:** the teammate reviewing this before it's used or merged.
**Written by:** Claude (the AI that wrote the code), as a self-audit. Treat the claims below as things to verify, not as settled facts. §4 gives the commands to verify them.
**Date of audit:** 2026-10-24.

## Update 2026-10-28: full-combination search, every I-SFG, logic only in C++

**Sections 1–6 below describe the version before this update.** [FLOW.md](FLOW.md) describes the current one.

**Source layout changed (2026-10-28):** `cpp/src` and `cpp/include` are now split into `paper1/` (loa), `paper2/` (sca_vda), `hybrid/` (hybrid, full, bench), `common/` (model, json, bigcount) and `app/` (main, server, summary, experiments). File names in the sections below refer to the old flat layout; see FLOW.md "Source layout".

| Area | Before | Now |
|---|---|---|
| Structure inside a dependency level | Stage sequences only (ordered set partitions): 13 for 3 VNFs, 75 for 4 | Every series-parallel I-SFG of paper 2, including branches such as `(f2 → f3) ∥ f4` (paper 2, Fig. 1(g)): 19 for 3 VNFs, 195 for 4, 2791 for 5. `"isfg": "staged"` restores the old space |
| Dividing / merging cost | (branches − 1)·τ per stage, no data size | Paper 1 Eqs. 4, 6, 7: μ_f·l_u, (out(f) − 1)·τ_rep·l_u, (in(f) − 1)·τ_int·l_u along the critical branch. New config field `pkt` = l_u (default 1 = old numbers) |
| "All combinations" | `exact.cpp`: plain brute force, at most 2·10⁶ schemes, used only as a certificate | `full.cpp`: exact branch and bound over every I-SFG × every placement. It starts from the best of VDA and LOA/ROA, has a time budget `fullMs` (default 20 s), and its result is the final answer |
| Certificate labels | OPTIMAL_PROVEN / OPTIMAL_VERIFIED / IMPROVED_BY_EXACT / BOUNDED | OPTIMAL (full search finished) / OPTIMAL_PROVEN (latency only: full search ran out of time, LOA stopped on its own bound) / BOUNDED / INFEASIBLE / UNKNOWN |
| Tests | `tests/check_vs_exact.py` | `bin\loa_hybrid.exe --selftest N` (C++: full search vs plain enumeration), `tests/count_sp.py` (structure counts) |
| Browser | `ui.js` validated, `results.js` computed improvements and averages | `POST /api/check` and `GET /api/summary` (C++); the JS only renders |
| n feasibility | – | `bin\loa_hybrid.exe --feasibility` → `web/feasibility.html` |

Status of the findings in §5:

- **B1 (ROA "proof" can be wrong): confirmed, no longer reported as a proof.** The self-test found 23 of 574 ROA bound stops not optimal. ROA's stop is not used as a certificate any more; the resource optimum comes from the full search.
- **B2: fixed.** The final scheme is always the best one any method found, and the full search covers everything the old exact check did.
- **B3: fixed.** `null` is written instead of `nan`.
- **R1: fixed.** The RNG seed now uses the experiment's index.
- **R10: fixed.** The browser doesn't validate any more.
- **R2: still open.** The server is single-threaded. The full search is bounded by `fullMs`, but paper 1's own ROA takes about 5 s on preset L, and that blocks the server.
- **R4, R5, R7, R8, R9: unchanged.**
- **`RESULTS.md` is stale.** The experiments must be re-run with the larger structure space.

> **Repository state:** the project folder is **not a git repository** (`git status` → "not a git repository"), so there is no diff. All AI-written work is in the new folder `loa-hybrid/`, plus one tooling file, `.claude/launch.json`. The pre-existing `loa-walkthrough/` was **not modified**: its files' timestamps (2026-10-23 17:21) are earlier than all `loa-hybrid` files. Run `git init && git add -A && git commit` before reviewing, so later changes can be diffed.

---

## 1. Executive overview

### What was implemented

| Source | What | Status |
|---|---|---|
| **Paper 1**: Xie et al., *Latency-Sensitive and Resource-Efficient Parallel VNF Placement in Mobile Edge Networks: A Dynamic Graph Weighting Approach* (IEEE ToN, 2026) | **LOA** (Alg. 1 DPG construction, simplified; Alg. 2 + 3) and **ROA** (Alg. 4 + 5) | Implemented in C++ |
| **Paper 2**: Tian, Liang & Liu, *Joint VNF Parallelization and Deployment in Mobile Edge Networks* (IEEE TWC, 2023) | **SCA** (Alg. 2), **VDA** (Alg. 3), reliability constraint, Theorem 1 bound | Implemented in C++. The ILP (Alg. 1) is replaced by brute force |
| Our own design (in neither paper) | **"Hybrid"**: seed LOA/ROA with VDA's answer (Solution A), certify the result (Solution B), reliability in LOA's feasibility filter (Solution C), SCA-ordered arrangement search (Solution E) | Implemented |
| Infrastructure | Built-in C++ HTTP server, HTML/CSS/JS front-end (calculator + paper-comparison charts), experiment runner that re-runs paper 1's Section V | Implemented |

The C++ core started as a port of `loa-walkthrough/js/engine.js` (the earlier JavaScript LOA demo) and was then extended.

### Files created (all under `loa-hybrid/`; about 6,700 lines in total)

| Area | Files | Lines | Purpose |
|---|---|---|---|
| C++ headers | `cpp/include/{json,bigcount,model,sca_vda,loa,exact,hybrid,server}.hpp` | 416 | Module APIs |
| C++ sources | `cpp/src/{json,model,sca_vda,loa,exact,hybrid,server,main}.cpp` | 1,950 | Engine + server |
| Experiments | `cpp/src/experiments.cpp` | 398 | Second program, `loa_experiments.exe` |
| Build | `cpp/Makefile`, `cpp/build.bat`, `cpp/run_experiments.bat` | 81 | MinGW / make builds |
| Fixtures | `cpp/tests/preset_{d,e,r,s,l}.json` | – | Input configs (no expected outputs inside) |
| Test | `cpp/tests/check_vs_exact.py` | 105 | Randomized proof-vs-exact check (added during this audit) |
| Results | `cpp/results/*.json, *.log` (8 experiments) + copies in `web/data/results/` | – | Experiment output, 10 runs per point |
| Web | `web/index.html`, `web/results.html`, `web/js/{api,ui,render,main,results}.js`, `web/data/{presets,paper_results}.js`, `web/css/styles.css` | 2,330 | UI (styles copied from `loa-walkthrough` and extended) |
| Docs | `FLOW.md`, `RESULTS.md`, `HANDOVER.md` | – | File-to-file flow, paper comparison, this audit |
| Binaries | `cpp/bin/loa_hybrid.exe`, `cpp/bin/loa_experiments.exe` | – | Build output (32-bit MinGW). **Don't commit**; rebuild instead |

Outside `loa-hybrid/`: `.claude/launch.json` (config for the IDE's preview server; not needed by the project).

---

## 2. Fidelity check (paper vs. implementation)

**Legend:** ✅ matches · ⚠️ simplified or interpreted · ❌ not implemented

### Paper 1: LOA / ROA

| Paper element | Implementation | Fidelity |
|---|---|---|
| Alg. 3/5 outer loop: k = \|V\|+1, k-shortest paths, k ← 2k | `loa.cpp` `runSearch()`; A* k-shortest with an exact heuristic | ✅ |
| Feasibility filter (Alg. 3/5 line 7) | `pathFeasible()`: node capacity (new instances only), the path's own delay ≤ T_max, plus reliability (our addition) | ✅ + extra |
| Partition-stability filter (lines 8–19) | `stabilityFilter()`: greedy in cost order, drops the costlier of two order-conflicting paths | ✅ |
| Coverage check (lines 20–23) | inside `runSearch()` | ✅ |
| Alg. 2/4: A* prefix lower bound, anchors, prune when next path > best | inside `runSearch()` + `evaluateAnchor()` | ✅ |
| Alg. 2/4 lines 10–12: "build S_parallel: partial group reuse for F(p), full construction for F \ F(p)" | **Interpretation:** the anchor fixes host and order for its VNFs; the other VNFs may use any host seen on a path in A(t); every serial/parallel arrangement in each layer is tried | ⚠️ ambiguous in the paper |
| Alg. 2 line 20 prints `flag ← false`, but the text says the search stops | Treated as stop (inherited from the JS walkthrough) | ⚠️ |
| **Parallel structure (I-SFG)** | Restricted to **layers in sequence**, each layer an ordered set partition into stages. The paper's Fig. 3 allows a branch to cross dependency layers; that can't be represented here | ⚠️ **important simplification** |
| Transmission delay, Eq. 3: Σ l_u / b_u over the links of the max branch | Fixed per-link delay, Floyd-Warshall routes (the experiments use the paper's "10–15 ms per link" setting, not Eq. 3) | ⚠️ |
| Processing delay, Eq. 4: μ_n · l_u | Experiments: yes. Calculator: a fixed `proc` per VNF | ⚠️ in the calculator |
| Replication / integration, Eqs. 6–7: per-VNF out/in branch counts on the critical branch | (branches − 1)·τ at each stage fork / merge (`evalScheme`, `model.cpp:309`), evaluated over the critical chain | ⚠️ equivalent only for the layered structures above |
| Instance sharing, constraints 10–12 | `placeInstances()`: one shareable instance per (node, type), capacity `instCap`, request load `qcpu`; only new instances use node capacity | ✅ rule; values are ours |
| ROA weights: edge = min bandwidth b_u × hops, node = 0 if reusable, else c_n | `Weights` in `loa.cpp:33` | ✅ |
| ROA normalisation (Eq. 16): divide by the "maximum observed among compared schemes" | The algorithm uses **fixed per-request scales**: C_bw = b_u × mean hops × (\|F\|+1), C_cpu = Σc_n. The experiments report both this and the paper's max-normalised form | ⚠️ **our choice** |
| Scheme bandwidth, Eq. 1: δ_{u,e} per **distinct** link | `resourceCost()` counts distinct links. But the DPG path cost sums hops per edge, so overlapping routes are counted twice. **This breaks Lemma 4.3 for ROA (see §5, B1)** | ⚠️ / bug |
| Bandwidth constraint 13 | Links with residual bandwidth < b_u are removed before routing | ✅ |
| Ordering of equal-cost ROA paths | Tie-break on delay (our addition, not in the paper) | ⚠️ |

### Paper 2: SCA / VDA

| Paper element | Implementation | Fidelity |
|---|---|---|
| SCA (Alg. 2): incremental heaviest-first construction of candidate I-SFGs, pick min delay | **Enumerates all** ordered partitions per layer and picks the lowest placement-free delay. That's a superset of the paper's candidates, so it can pick a better layout than the real SCA | ⚠️ |
| VDA (Alg. 3): nodes sorted by distance to predecessors; share an existing instance (lines 10–15), else deploy a new one and break (16–21) | `runVDA()` in `sca_vda.cpp:40`. The first acceptable node in sorted order is used, reusable or new. Equivalent, because the order is by delay | ✅ |
| Reliability r = 1 − (φ(k, f) + β_g) | φ = α·(load − 1), counting VNFs of the same request on the node | ⚠️ adapted to one request |
| ILP (Alg. 1, CPLEX) | Replaced by brute force `runExact()`, run only when the space ≤ `exactCap` (default 2×10⁶) | ❌ replaced |
| Theorem 1 bound γ·\|Q\|/\|Φ\| | `theorem1()` in `hybrid.cpp:32`. Reported as information only; it bounds propagation delay, not total latency | ✅ |

### Default parameters chosen by the AI (not given in the papers)

| Parameter | Value | Where |
|---|---|---|
| τ_rep, τ_int | 0.02 ms per packet per extra branch | `experiments.cpp:52` |
| Instance capacity C_n^max / request load q_u | 100 / U[10,30] | `experiments.cpp:54,127` |
| VNF catalogue size | 12 types | `experiments.cpp:53` |
| Waxman α, β | 0.25, 0.25, plus a nearest-neighbour tree for connectivity | `experiments.cpp:97` |
| Heavy-tailed load | Pareto shape 1.5, l_u capped at 400, b_u at 150 | `experiments.cpp:120-122` |
| Max VNFs per dependency level | 5 | `experiments.cpp:134` |
| Deadline T_max in experiments | 10⁶ ms (effectively none) | `experiments.cpp:177` |
| Search caps used for every reported result | k ≤ 256, 200 candidates per anchor (passed by `run_experiments.bat`; the program's built-in defaults are 4096 / 20000) | `experiments.cpp:39`, `run_experiments.bat` |
| Calculator defaults | kmax 2048, candCap 30000, exactCap 2×10⁶, breq 0, instCap 100, wBw 0.5 | `model.hpp:41-57` |

---

## 3. Architecture and end-to-end data flow

[FLOW.md](FLOW.md) has the full map with line numbers. In short:

```
Browser (web/)                                 C++ (cpp/, single process, single-threaded)
──────────────                                 ───────────────────────────────────────────
ui.js run() → api.js runEngine(cfg)
  POST /api/run {config JSON} ───────────────► server.cpp serve() [select loop] → handle()
                                                 → hybrid.cpp runHybrid(json)
                                                    1 model.cpp   parseConfig → validateConfig → buildModel
                                                                  (Floyd-Warshall delay + fewest-hop routes,
                                                                   layers, ordered partitions, instances)
                                                    2 sca_vda.cpp runSCA → runVDA → runVDAAllLayouts → seeds
                                                    3 loa.cpp     runSearch(M, Latency|Resource, seed?) ×4
                                                    4 exact.cpp   runExact (both objectives, if small)
                                                    5 hybrid.cpp  certify() ×2 → report JSON
  render.js renderResult() ◄────────────────── replyJson()
```

### Key types and functions

| Module | Key items |
|---|---|
| `json.hpp/.cpp` | `json::Value` (insertion-ordered object), `json::parse`, `Value::dump` (writes NaN/∞ as `null`) |
| `bigcount.hpp` | `BigCount`: GMP `mpz_class` when built with `-DUSE_GMP`, otherwise a saturating `uint64`. Only used for counting search spaces |
| `model.hpp/.cpp` | `Config`, `Model`, `Scheme`, `ResCost`; `buildModel`, `evalScheme`, `placeInstances`, `resourceCost`, `hostsFeasible`, `schemeJson` |
| `sca_vda.hpp/.cpp` | `runSCA`, `runVDA`, `runVDAAllLayouts` |
| `loa.hpp/.cpp` | `Objective`, `Weights`, `kShortest`, `pathFeasible`, `stabilityFilter`, `evaluateAnchor`, `runSearch` (+ `runLOA` / `runROA` wrappers), `loaJson` |
| `exact.hpp/.cpp` | `runExact`: exhaustive hosts × arrangements, best latency and least resource |
| `hybrid.hpp/.cpp` | `runHybrid` (orchestration), `certify`, `theorem1`, `seedEffect` |
| `server.hpp/.cpp` | `serve`, `parseRequest`, `handle` (Winsock on Windows, BSD sockets elsewhere) |
| `experiments.cpp` | `genTopo`, `genCatalog`, `genRequests`, `runSequence`, 8 experiment definitions, JSON writer |

### Dependencies

- **C++14 standard library only.** On Windows the server links `ws2_32`.
- **GMP:** optional, `-DUSE_GMP`. **Not tested**, because the build machine has no GMP headers.
- **Front-end:** no npm packages and no JS libraries. The only external resource is Google Fonts, loaded by CSS.
- **Python 3:** only for `tests/check_vs_exact.py`. The project doesn't need it at runtime.
- **Toolchain used:** MinGW.org g++ 6.3.0, 32-bit, on Windows 11. **Never compiled on Linux or macOS, and never with MSVC.**

---

## 4. Verification and testing

### What exists

- **No unit-test suite.** No test framework, no assertions inside the C++.
- **Fixtures:** `cpp/tests/preset_*.json` are inputs only; the expected outputs are listed below.
- **`cpp/tests/check_vs_exact.py`** (added in this audit): random small instances, checking that "proven optimal" agrees with the exhaustive search.
- **Already done ad hoc during development, but not saved as scripts:**
  - presets D and E were compared with the old JS engine (58 ms and 50 ms, same as brute force);
  - the UI was exercised in a browser.

### Commands (Windows, from `loa-hybrid\cpp`, MinGW `g++` on PATH)

```bat
:: 1. Build both programs (build.bat also starts the server; Ctrl+C to stop)
g++ -std=c++14 -O2 -Wall -Wextra -Iinclude src\json.cpp src\model.cpp src\sca_vda.cpp src\loa.cpp src\exact.cpp src\hybrid.cpp src\server.cpp src\main.cpp -o bin\loa_hybrid.exe -lws2_32 -static-libgcc -static-libstdc++
g++ -std=c++14 -O2 -Wall -Wextra -Iinclude src\json.cpp src\model.cpp src\sca_vda.cpp src\loa.cpp src\experiments.cpp -o bin\loa_experiments.exe -static-libgcc -static-libstdc++

:: 2. Presets, compare with the expected table below
bin\loa_hybrid.exe tests\preset_d.json > d.json
bin\loa_hybrid.exe tests\preset_s.json > s.json

:: 3. Error handling (expect "Error: JSON parse error ...", exit code 1)
echo {bad | bin\loa_hybrid.exe -

:: 4. Randomized check against the exact search (about 1 minute)
python tests\check_vs_exact.py bin\loa_hybrid.exe 300

:: 5. Small experiment smoke test (about 10 s)
bin\loa_experiments.exe --runs 1 --only sfclen --reqs 3 --kmax 64 --cand 50 --out smoke.json

:: 6. Full experiments (about 2 hours, 8 processes), then copy for the web page
run_experiments.bat
copy results\*.json ..\web\data\results\

:: 7. UI: start the server and open http://localhost:8080 and /results.html
build.bat
```

On Linux or macOS, `make` should build both programs, but **this is untested**.

### Expected results (verified on 2026-10-24 with a clean rebuild)

| Preset | `certificate.label` | `final.lat` | `certificateRes.label` | `finalRes.res.norm` | `finalRes.lat` |
|---|---|---|---|---|---|
| D | OPTIMAL_PROVEN | 58 | OPTIMAL_VERIFIED | 0.682 | 58 |
| E | OPTIMAL_VERIFIED | 50 | OPTIMAL_VERIFIED | 0.682 | 50 |
| R | OPTIMAL_PROVEN | 58 | OPTIMAL_VERIFIED | 0.773 | 76 |
| S | OPTIMAL_PROVEN | 55 | OPTIMAL_VERIFIED | 0.282 | 61 |
| L | BOUNDED | 67 | BOUNDED | 0.640 | 69 (about 12 s) |

`check_vs_exact.py` with 300 instances gave:

| Check | Result |
|---|---|
| LOA proofs (192) | all agree with the exact search |
| ROA proofs (100) | **5 disagree**: seeds 49, 68, 85, 95, 106 (see §5, B1) |
| Any search beating the exact optimum | none |
| Exit code | 0 (ROA disagreements are reported but don't fail the script) |

---

## 5. Edge-case and quality audit

### Confirmed bugs (reproduced during this audit)

| # | Issue | Evidence | Where |
|---|---|---|---|
| **B1** | **ROA's "proven optimal" can be wrong.** A DPG path's resource cost sums hops edge by edge, but a scheme's bandwidth counts distinct links (Eq. 1). When routes overlap, a scheme can cost *less* than the paths it's built from, so pruning by Lemma 4.3 throws away better schemes. LOA is not affected. | 5 of 100 ROA proofs were wrong in `check_vs_exact.py` (e.g. seed 68: ROA 0.383, exact 0.333) | `loa.cpp` `Weights::edge`, prune tests in `runSearch` |
| **B2** | **`certify()` keeps a "proven" answer even when the exact search found a better one.** It sets `exactAgrees:false` but still returns the worse scheme as final. | Same 5 cases: `finalRes` is worse than `exact.schemeRes` | `hybrid.cpp` `certify()`, the `proven(primary)` branch |
| **B3** | **The experiment runner writes invalid JSON** (`"delay":nan`) when an algorithm accepts 0 requests at a point. Both `JSON.parse` and Python's `json` reject it, and `results.html` can't load that file. The shipped results contain no NaN. | `loa_experiments.exe --runs 1 --only sfclen --reqs 3 --kmax 64 --cand 50` → parse error | `experiments.cpp:304` `num()` |

### Risks and quality issues

| # | Issue | Where | Impact |
|---|---|---|---|
| R1 | Experiment RNG seed uses `std::hash<std::string>`, which is implementation-defined. Results are **not reproducible across compilers or platforms** | `experiments.cpp:345` | Teammate's numbers may differ from `RESULTS.md` |
| R2 | The server is single-threaded and runs `runHybrid` inline. Preset L takes about 12 s and blocks every other request. There's no time limit, cancellation or size limit beyond 1 MB of body | `server.cpp` | UI freezes on large configs |
| R3 | The search caps hide failures. With k ≤ 256 / 200 candidates, plain LOA accepts only 72–87 % and plain ROA 65–81 % of requests at 60 nodes. Reported plain-LOA delay is averaged over the accepted (easier) requests | `run_experiments.bat`, `RESULTS.md` | Plain-LOA numbers look better than they are |
| R4 | The Hybrid seed differs between programs: the calculator uses the better of VDA and VDA-on-all-layouts; `experiments.cpp` uses only VDA on SCA's layout | `hybrid.cpp:141` vs `experiments.cpp` A_HYBRID branch | Calculator and experiment numbers aren't strictly the same algorithm |
| R5 | Debug code left in: `--verbose`, `--diag` (re-runs LOA per request for comparison), unbuffered stdout; the usage string omits `--cand/--kmax/--reqs/--verbose/--diag`; global mutable flags `g_*` | `experiments.cpp:39-42, 197-216, 309, 322` | Cosmetic; `--diag` doubles the runtime if used |
| R6 | Duplicate report key: `seedEffect.sameLatency` is a copy of `sameCost` (kept for the old UI) | `hybrid.cpp:91-92` | Cosmetic |
| R7 | Unoptimised hot paths: every heap push in `kShortest` copies the whole path vector; `evaluateAnchor` recomputes `hostsFeasible` / `resourceCost` per candidate; the brute force uses `std::function` recursion | `loa.cpp:72-150, 207-270`, `exact.cpp` | ROA takes about 1–1.5 s per request at 60 nodes |
| R8 | Stability-filter key `a*64+b` assumes fewer than 64 VNFs (validation caps it at 16, so it's safe today); the `hop` sentinel is `1<<29` | `loa.cpp:176,190`, `model.cpp:182` | Latent |
| R9 | Only the happy path is caught: `runHybrid` and the server catch `std::exception`, but `std::bad_alloc` from a huge search can still kill the server process | `hybrid.cpp:109`, `server.cpp:136-140` | Server crash on extreme inputs |
| R10 | The front-end validates less than the C++ does (e.g. per-level caps differ: UI 5, C++ 6) | `ui.js validate()` vs `model.cpp validateConfig()` | Server error message instead of form validation |
| R11 | `FLOW.md` cites line numbers that will drift with edits | `FLOW.md` | Docs |

### Patterns to treat with suspicion

- **"Solutions A/B/C/E" and the "Hybrid"** are the AI's own design, proposed earlier in the session. They are **not in either paper**. Don't present them as paper results.
- **Interpretations sold as the paper's algorithm.** The meaning of S_parallel (Alg. 2 lines 10–12), the `flag` line, the layered I-SFG restriction, and SCA as full enumeration are judgement calls. Check them against the PDF.
- **"OPTIMAL_PROVEN" relies on the paper's Lemmas 4.3 and 4.4 holding for this cost model.** That was checked empirically for LOA (192 of 192) and **shown false for ROA** (B1).
- **Paper numbers** in `web/data/paper_results.js` were taken from the text of the PDF (Section V-C) and checked by hand. The paper's Table III (Appendix C) isn't in the PDF, so figure values were never digitised.
- **Results text**: `RESULTS.md` was written by the AI from the JSON files. Spot-check a few numbers against `cpp/results/*.json` before quoting them.

---

## 6. Human review checklist (before merging or presenting)

1. **Fix or relabel ROA's certificate (B1 + B2).** Simplest safe fix: in `certify()`, when `exactRan` and the exact scheme is better, always use it and label it `IMPROVED_BY_EXACT`. For large instances, report ROA as `BOUNDED`, never `PROVEN`, unless the path cost is made a true lower bound (e.g. compute bandwidth from distinct links). Re-run `python tests\check_vs_exact.py bin\loa_hybrid.exe 300`: it should then report no ROA disagreements.
2. **Review the paper-fidelity interpretations in §2,** especially the layered I-SFG restriction (no branch can cross layers), S_parallel construction, transmission delay as a fixed per-link value instead of Eq. 3, and the fixed ROA normalisers. Decide which ones are acceptable for the report, and say so in it.
3. **Make the experiments reproducible and robust (B3, R1, R4).**
   - Replace the `std::hash` seed with a fixed integer per experiment ID.
   - Write `null` instead of `nan`.
   - Use the same Hybrid seed as the calculator.
   - Re-run `run_experiments.bat`, about 2 hours.
4. **Benchmark and tune search limits (R3, R7).**
   - Try larger k and candidate caps on a subset to see how acceptance and delay for plain LOA and ROA move.
   - Profile `kShortest` and `evaluateAnchor`; storing parent pointers instead of copying path vectors is the obvious first fix.
   - Make sure any conclusion about "plain LOA" isn't an artefact of the caps.
5. **Harden before anyone else uses the server (R2, R5, R9, R10).**
   - Add a request time/size limit, or move solving onto a worker thread.
   - Remove the `--diag` / `--verbose` code and fix the usage text.
   - Align UI and C++ validation.
   - Build once on Linux (`make`) and once with `-DUSE_GMP` to confirm portability claims, which are currently untested.
