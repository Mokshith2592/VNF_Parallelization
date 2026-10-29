# Paper 1 results vs our C++ implementation

> **Stale since 2026-10-28.** The numbers below were produced by the earlier engine, which only allowed stage sequences inside a dependency level. The engine now searches every I-SFG of paper 2 (e.g. `(f2 → f3) ∥ f4`), so LOA, VDA and the hybrids can pick structures they could not before. Re-run `cpp\run_experiments.bat` (about 2 hours) before quoting these numbers. `loa_experiments.exe --staged` reproduces the earlier structure space. For the full-combination search and how large n can get, see `web/feasibility.html` and FLOW.md §6.

This compares the experiments of paper 1 (Xie et al., "Latency-Sensitive and Resource-Efficient Parallel VNF Placement in Mobile Edge Networks", Section V-C) with the same experiments re-run using our C++ code (`cpp/src/experiments.cpp`), at **10 runs per point, as in the paper**. It covers both halves of the paper:
- **LOA** (Algorithms 2 + 3), the latency side: delay figures 6a–13a and 11a;
- **ROA** (Algorithms 4 + 5), the resource side: resource figures 6b–13b and 11b, plus load balance (Fig. 15).

**Where to find the data**
- Raw numbers: `cpp/results/*.json`, with 95 % confidence intervals.
- Interactive charts: **http://localhost:8080/results.html** (start the server with `cpp\build.bat`). It has 16 charts: delay and resources for each of the 8 experiments.

**Algorithms compared**
- From paper 1: **LOA** and **ROA**.
- Ours: **Hybrid** (VDA-seeded LOA) and **Hybrid-ROA** (VDA-seeded ROA).
- From paper 2: **VDA**.
- Simple stand-ins: **Serial\*** and **Parallel\***.

Every algorithm embeds the same request sequence on its own copy of the network. VNF instances are shared between requests.

---

## 1. Bottom line

| | Paper 1 | Ours |
|---|---|---|
| **Latency:** LOA vs VDA | ≈ 3–5 % lower delay (1.0–10.6 % per point) | **Hybrid: 9.0 % lower** (3.9–11.4 % per point), same acceptance as VDA |
| **Resources:** ROA vs best baseline | 13.4–19.6 % saving (avg. per experiment) | **Hybrid-ROA: 11.3–14.6 % saving** (24.7 % in the weight sweep) |
| **Resources:** ROA vs VDA | 42.5–45.1 % saving | 12.0–17.4 % saving (paper's metric), **36.9–41.9 %** (fixed-scale cost ROA minimises) |
| **Load balance** (Jain / CV) | LOA 0.615 / 0.813, ROA 0.379 / 1.357, VDA 0.627 | Hybrid 0.64 / 0.77, Hybrid-ROA 0.30 / 1.71, VDA 0.63 / 0.79 |

1. **The latency side holds up.** The hybrid has a lower average delay than VDA at all 41 points, about 9 % on average. That is a little above the paper's 3–5 %.
2. **The resource side matches the paper closely.** Under the paper's own metric, Hybrid-ROA saves 11–15 % against the best baseline in every experiment. The paper reports 13–20 %. The weight sweep reproduces the paper's trend as well: a large saving when CPU dominates (56.5 % at α = 0.1; paper 32.1 %) and a small one when bandwidth dominates (18.3 % at α = 0.9; paper 2.9 %).
3. **The load-balance trade-off also matches.** ROA packs VNFs onto nodes with reusable instances, so its load is much less even (Jain 0.30, CV 1.71). The latency-oriented hybrid spreads load like VDA does. The paper shows the same split: ROA 0.379 / 1.357, LOA 0.615 / 0.813.
4. **Saving resources costs latency.** Hybrid-ROA's delay is about **twice** the hybrid's (152–202 ms vs 84–112 ms). To reuse instances it routes traffic to wherever they already run. The paper doesn't report ROA's delay.
5. **Plain LOA and plain ROA aren't usable at this scale with capped searches.**
   - We cap the search at k ≤ 256 paths and 200 candidates per anchor.
   - With those caps, plain LOA accepts only 72–87 % of requests and plain ROA 65–81 %.
   - The VDA seed fixes this: the hybrid accepts exactly as many requests as VDA (94–100 %), and Hybrid-ROA nearly as many (90–99 %).
   - Plain LOA's "12.9–23.6 % lower delay" therefore isn't a fair number: it drops the hard requests.
6. **Optimality is rarely proven at 60 nodes.** It holds for 7–23 % of searches, mostly when layers are narrow.

---

## 2. Latency side (LOA, Algorithms 2 + 3)

"Imp." = delay reduction relative to VDA. Paper values are the ones stated in its text.

| Experiment | Paper fig. | Paper LOA delay | Paper imp. | Our hybrid delay | **Our hybrid imp.** | Our plain LOA imp. (accepted) |
|---|---|---|---|---|---|---|
| Node capacity 100→300 | 6a | 136.44 → 116.97 ms | 4.0 % avg | 113.6 → 83.4 ms | **8.8 %** (3.9 → 9.1) | 18.9 % (72 %) |
| Link bandwidth 100→300 | 7a | 114.78 ms @100 | 7.5 % → 3.1 % | 86.2 → 84.8 ms | **10.4 %** (10.8 → 10.6) | 13.6 % (86 %) |
| Requests 10→50 | 8a | 110.63 → 119.41 ms | 1.0 % → 5.9 % | 80.6 → 88.7 ms | **9.7 %** (7.4 → 10.3) | 13.5 % (84 %) |
| Network size 20→100 | 9a | 98.65 → 127.36 ms | 10.6 % → 1.1 % | 105.7 → 80.9 ms | **8.5 %** (5.2 → 8.1) | 16.6 % (74 %) |
| SFC length 5→10 | 10a | 95.09 → 154.13 ms | 2.8 % → 3.3 % | 69.0 → 121.5 ms | **7.8 %** (7.3 → 5.2) | 13.6 % (75 %) |
| Parallelism 0.1→0.9 | 12a | 118.05 → 117.91 ms | 3.8 % → 3.7 % | 94.1 → 74.9 ms | **10.2 %** (10.7 → 8.9) | 13.0 % (87 %) |
| Heavy-tailed load | 13a | 139.36 ms (uniform 119.74) | 5.1 % vs best | 112.5 ms (uniform 84.6) | **7.0 %** | 23.6 % (75 %) |
| Weights α 0.1→0.9 | 11a | 119.14 → 119.01 ms | 2.6 % → 2.8 % | 84.3 → 85.2 ms | **9.8 %** (9.1 → 8.6) | 12.9 % (86 %) |

Serial\* is 14–18 % slower than VDA in every experiment, as in the paper, where Serial is among the slowest baselines.

### Do the delay curves have the same shape?

| Experiment | Paper | Ours (hybrid) | Match? |
|---|---|---|---|
| Node capacity ↑ | delay ↓ (136 → 117) | delay ↓ (114 → 83) | ✓ |
| Link bandwidth ↑ | roughly flat | flat (86 → 85) | ✓ |
| Requests ↑ | delay ↑ (111 → 119) | delay ↑ (81 → 89) | ✓ |
| Network size ↑ | delay ↑ (99 → 127) | delay ↓ (106 → 81) | ✗ |
| SFC length ↑ | delay ↑ (95 → 154) | delay ↑ (69 → 122) | ✓ |
| Parallelism ↑ | flat (118 → 118) | delay ↓ (94 → 75) | ✗ |
| Weights α ↑ | flat (119 → 119) | flat (84 → 85) | ✓ (the weight only affects ROA) |
| Heavy-tailed vs uniform | +16 % delay | +33 % delay | ✓ direction, larger effect |
| LOA's gain as requests ↑ | grows (1.0 → 5.9 %) | grows (7.4 → 10.3 %) | ✓ |
| LOA's gain as network size ↑ | shrinks (10.6 → 1.1 %) | grows (5.2 → 8.1 %) | ✗ |
| LOA's gain as SFC length ↑ | flat (2.8 → 3.3 %) | shrinks (7.3 → 5.2 %) | ✗ |

---

## 3. Resource side (ROA, Algorithms 4 + 5)

"Normalised resource consumption" follows the paper, Eq. 16: α·bandwidth/max + β·new CPU/max, where max is the largest value any compared algorithm used at that point, and α = β = 0.5 unless swept. "Saving" = reduction relative to the best of VDA, Serial\* and Parallel\*.

| Experiment | Paper fig. | Paper ROA value | Paper saving vs best (avg.) | Our Hybrid-ROA value | **Our saving vs best** | Paper / our saving vs VDA |
|---|---|---|---|---|---|---|
| Node capacity 100→300 | 6b | 0.255 → 0.192 | 19.6 % (15.9–27.1) | 0.631 → 0.625 | **14.6 %** (13.0–16.1) | 45.1 % / 17.4 % |
| Link bandwidth 100→300 | 7b | 0.329 → 0.193 | 14.7 % (4.4 → 24.3) | 0.669 → 0.648 | **12.1 %** (11.8 → 12.7) | 42.5 % / 13.2 % |
| Requests 10→50 | 8b | 0.275 → 0.229 | 15.4 % (21.3 → 11.2) | 0.683 → 0.650 | **12.1 %** (11.0 → 10.6) | – / 13.6 % |
| Network size 20→100 | 9b | 0.185 → 0.223 | 13.4 % (13.6 → 14.9) | 0.756 → 0.629 | **11.9 %** (8.7 → 14.6) | – / 12.9 % |
| SFC length 5→10 | 10b | 0.144 → 0.216 | 16.1 % (13.5 → 18.0) | 0.665 → 0.671 | **12.2 %** (9.3 → 10.3) | – / 13.3 % |
| Parallelism 0.1→0.9 | 12b | 0.189 → 0.227 | 16.3 % (20.0 → 8.4) | 0.654 → 0.643 | **13.8 %** (10.4 → 14.4) | – / 14.8 % |
| Heavy-tailed load | 13b | 0.193 (uniform 0.248) | 15.6 % (uniform 13.8) | 0.669 (uniform 0.661) | **11.3 %** | – / 12.0 % |
| Weights α 0.1→0.9 | 11b | 0.262 → 0.104 | 15.6 % (32.1 → 2.9) | 0.407 → 0.743 | **24.7 %** (56.5 → 18.3) | 43.8 % / 27.6 % |

**How to read these numbers**
- **Absolute values aren't comparable.** The "max" in the normalisation is taken over a different set of algorithms: ours includes LOA and the hybrid, which use little bandwidth. So our values sit around 0.6–0.7 and the paper's around 0.1–0.3. Compare the *savings*.
- **The fixed-scale cost** is what ROA itself minimises, with the same reference scale for every algorithm. On it, Hybrid-ROA uses **35.6–38.7 % less than LOA** and **36.9–41.9 % less than VDA**. That is close to the paper's 42–45 % saving against VDA.
- **Where the saving comes from:** Hybrid-ROA creates far fewer instances, about 70 units of new CPU per request against VDA's about 200. It spends more bandwidth to get there, about 300 against 170 Mbps per request, because it routes to reusable instances.
- **The weight sweep has one exception.** At α = 0.7 (bandwidth weighted heavily), Hybrid-ROA is 1 % *worse* than Serial\* under the paper's metric. That is the trade-off above showing through. On the fixed-scale cost it is still the cheapest at every α.

### Do the resource curves have the same shape?

| Experiment | Paper (ROA) | Ours (Hybrid-ROA) | Match? |
|---|---|---|---|
| Node capacity ↑ | consumption ↓ | ≈ flat (0.631 → 0.625) | ~ |
| Link bandwidth ↑ | consumption ↓ | slightly ↓ (0.669 → 0.648) | ✓ |
| Requests ↑ | consumption ↓ (more reuse) | ↓ (0.683 → 0.650) | ✓ |
| Network size ↑ | consumption ↑ | ↓ (0.756 → 0.629) | ✗ |
| SFC length ↑ | consumption ↑ | ≈ flat | ~ |
| Weight α ↑ | saving shrinks (32.1 → 2.9 %) | saving shrinks (56.5 → 18.3 %) | ✓ |
| ROA below every baseline | yes | yes at 40 of 41 points (not at α = 0.7) | ✓ |

### Load balance (Fig. 15, network-size sweep)

| | Jain's index (paper / ours) | CV of node use (paper / ours) |
|---|---|---|
| LOA (ours: hybrid) | 0.615 / 0.64 | 0.813 / 0.77 |
| ROA (ours: Hybrid-ROA) | 0.379 / 0.30 | 1.357 / 1.71 |
| VDA | 0.627 / 0.63 | – / 0.79 |

All three match the paper in direction and are close in size. The latency-oriented search balances load like VDA, while the resource-oriented search concentrates it.

---

## 4. Why some trends don't match

- **Network size:** our Waxman generator keeps the same edge probability, so larger networks are denser and have shorter paths, which lowers delay and resource use. GT-ITM in the paper evidently keeps paths growing. Small networks (20 nodes) also run out of capacity, so only 68–94 % of requests are accepted.
- **Parallelism:** we charge only 0.02 ms per packet per extra branch for replication and integration. The paper doesn't give τ. Its flat curve suggests split/merge costs that cancel out the shorter critical path.
- **SFC length:** with the search capped, long chains (9–10 VNFs) more often end at the seed, so the hybrid's gain shrinks.
- **Size of ROA's saving against VDA:** the paper's VDA baseline may reuse instances less than ours. Our VDA follows paper 2's Algorithm 3, which shares any instance it passes. Against VDA, our ROA's gain is therefore smaller under the paper's metric (12–17 % vs 42–45 %). On the fixed-scale cost it is about the same (37–42 %).

---

## 5. Runtime per request (C++, 8 experiments running at once on one laptop)

| | LOA | Hybrid | ROA | Hybrid-ROA | VDA |
|---|---|---|---|---|---|
| Average per experiment | 475–648 ms | 502–658 ms | 1.1–1.5 s | 1.0–1.3 s | 53–86 ms |

ROA is about twice as slow as LOA. Its cost has many ties (zero for shared instances, equal hop counts), so more paths and anchors need checking. These times are higher than in the earlier latency-only run because all 8 experiments shared the CPU and each request now runs four searches.

---

## 6. What was run, and what differs from the paper

**Same as paper 1, Section V-A:**
- 60 nodes, SFC length 7, 30 requests, parallelism 0.5, α = β = 0.5 (defaults); 10 runs per point
- link delay U[10,15] ms, link bandwidth U[100,300] Mbps, node capacity U[100,300]
- VNF demand c_n U[20,50], processing U[0.045,0.2] ms per packet, data size U[50,100] packets, request bandwidth U[30,60] Mbps
- requests embedded one after another, each algorithm on its own copy of the network; VNF instances shared between requests

**Different:**

| Item | Paper 1 | Ours |
|---|---|---|
| Topology | GT-ITM | Waxman random graph (GT-ITM's flat model), new graph per run |
| Instance capacity C_n^max / request load q_u | not stated | 100 / U[10,30] |
| τ_rep, τ_int | not stated | 0.02 ms per packet per extra branch |
| ROA cost normalisers inside the algorithm | "maximum observed among compared schemes" | b_u × mean hops × (\|F\|+1), and Σ c_n (fixed per request) |
| Search limits | not stated | k ≤ 256, 200 candidates per anchor |
| Deadline T_max | not stated | none (10⁶ ms) |
| Parallelism model | "VNFs assigned to the same level" | each VNF joins the current level with probability p (max 5 per level) |
| Serial / Parallel baselines | ref. [17] algorithms | Serial\* / Parallel\*: fully serial or fully parallel layout, placed with VDA |

**Not reproduced:**
- the DNF, ParaDSP and PODPR baselines;
- the request × length heat-map (Fig. 14);
- the scalability index (Fig. 16).

The paper's Table III (Appendix C) isn't in the PDF, so only the numbers quoted in its text are compared.

**Re-run:** `cpp\run_experiments.bat` runs all 8 experiments at 10 runs per point and takes about 2 hours on this laptop; pass `5` for a quicker run. Then copy `cpp\results\*.json` to `web\data\results\`.
