/* =============================================================
   LOA Hybrid — Orchestration and certification
     1 model        (model.cpp)
     2 seed         SCA + VDA, paper 2 (sca_vda.cpp)
     3 full search  every I-SFG × every placement, from the seed (full.cpp)
     4 LOA / ROA    paper 1 (loa.cpp): the fallback when the search ran out of
                    time; with `compare` also run for the comparison table
     4 full search  every I-SFG × every placement, seeded with the
                    best of 2 and 3 (full.cpp)
     5 certify      optimal when the full search finished
   ============================================================= */
#include "hybrid/hybrid.hpp"
#include "common/model.hpp"
#include "paper2/sca_vda.hpp"
#include "paper1/loa.hpp"
#include "hybrid/full.hpp"
#include <algorithm>
#include <cmath>

namespace {

double msSince(double t0) { return nowMs() - t0; }

json::Value phase(const std::string& id, const std::string& title, const std::string& file,
                  const std::string& fn, double ms, const std::string& summary) {
  json::Value p = json::Value::object();
  p.set("id", id); p.set("title", title); p.set("file", file); p.set("fn", fn);
  p.set("ms", ms); p.set("summary", summary);
  return p;
}

bool proven(const LoaResult& r) {
  return r.stop != Stop::KLimit && !r.st.capped && r.st.stabilityRemoved == 0;
}

// Paper 2, Theorem 1: T'/T ≤ γ·|Q|/|Φ|, γ = l_max / l_min.
// l_max = largest shortest-path delay between two adjacent VNFs in the VDA scheme.
json::Value theorem1(const Model& M, const Scheme& vda) {
  json::Value b = json::Value::object();
  if (!vda.ok) { b.set("available", false); return b; }
  double lmax = 0;
  auto hostOf = [&](int f) { return f < 0 ? M.cfg.src : vda.hosts[f]; };
  for (int f : vda.g.topo) for (int p : vda.g.pred[f]) lmax = std::max(lmax, M.d[hostOf(p)][hostOf(f)]);
  for (int p : vda.g.last) lmax = std::max(lmax, M.d[hostOf(p)][M.cfg.dst]);
  const double lmin  = M.lmin == INF ? 0 : M.lmin;
  const double gamma = lmin > 0 ? lmax / lmin : INF;
  const double Q = static_cast<double>(M.V.size()), Phi = static_cast<double>(M.L);
  b.set("available", true);
  b.set("lmax", lmax); b.set("lmin", lmin); b.set("gamma", gamma);
  b.set("Q", Q); b.set("Phi", Phi);
  b.set("ratio", gamma * Q / Phi);
  b.set("note", "Bounds VDA's propagation delay against the optimum (paper 2, Theorem 1).");
  return b;
}

/**
 * Certificate for one objective. `final` is the best scheme any phase found.
 *   OPTIMAL         the full search covered every I-SFG × placement
 *   OPTIMAL_PROVEN  full search cut short, but LOA stopped on its own bound
 *                   (Lemmas 4.3 / 4.4; latency only, see FLOW.md §8)
 *   BOUNDED         neither: best scheme found so far
 *   INFEASIBLE / UNKNOWN
 */
json::Value certify(const Model& M, const LoaResult* primary, const FullResult* full, Objective obj, const Scheme& final) {
  json::Value cert = json::Value::object();
  json::Value reasons = json::Value::array();
  std::string label;
  if (full && full->complete) {
    label = final.ok ? "OPTIMAL" : "INFEASIBLE";
    reasons.push("The full search covered all " + fmt(full->space) + " schemes (" + fmt(full->isfgTotal) + " I-SFGs × placements): " +
                 fmt(full->leaves) + " complete schemes evaluated, the rest cut by bounds.");
  } else {
    if (full) reasons.push("The full search hit its time budget (" + fmt(M.cfg.fullMs) + " ms) after " + fmt(full->isfgSearched) + " of " + fmt(full->isfgTotal) + " I-SFGs.");
    else      reasons.push("The full search is switched off.");
    if (primary) {
      if (primary->stop == Stop::KLimit) reasons.push("LOA: k reached kmax = " + std::to_string(M.cfg.kmax) + " before the bound was met.");
      if (primary->st.capped)            reasons.push("LOA: an anchor hit the candidate cap (" + std::to_string(M.cfg.candCap) + ").");
      if (primary->st.stabilityRemoved)  reasons.push("LOA: " + std::to_string(primary->st.stabilityRemoved) + " path(s) removed by the stability filter (Lemma 4.4 no longer applies).");
    }
    if (obj == Objective::Latency && primary && proven(*primary) && final.ok &&
        std::fabs(schemeCost(M, primary->best, obj) - schemeCost(M, final, obj)) < 1e-9)
      label = "OPTIMAL_PROVEN";
    else label = final.ok ? "BOUNDED" : "UNKNOWN";
  }
  cert.set("label", label);
  cert.set("reasons", reasons);
  return cert;
}

/** How far a paper-1 run is from the best scheme (the optimum when certified). */
json::Value gapJson(const Model& M, const LoaResult& r, const Scheme& final, Objective obj, bool optimal) {
  json::Value g = json::Value::object();
  const double a = schemeCost(M, r.best, obj), b = schemeCost(M, final, obj);
  g.set("cost", a); g.set("best", b);
  g.set("found", r.best.ok);
  if (r.best.ok && final.ok) {
    g.set("gap", a - b);
    g.set("gapPct", b > 0 ? (a - b) / b * 100 : 0);
    g.set("matches", std::fabs(a - b) < 1e-9);
  }
  g.set("againstOptimum", optimal);
  return g;
}

/** Seeded vs unseeded run of the same search: one row per counter, lower is better. */
json::Value seedEffect(const LoaResult& plain, const LoaResult& seeded) {
  json::Value g = json::Value::object(), rows = json::Value::array();
  auto line = [&](const std::string& label, double a, double b, bool ms) {
    json::Value r = json::Value::object();
    r.set("label", label); r.set("plain", a); r.set("seeded", b); r.set("ms", ms);
    r.set("better", b < a - 1e-9); r.set("worse", b > a + 1e-9);
    rows.push(r);
  };
  line("Rounds (k doublings + 1)", plain.st.rounds, seeded.st.rounds, false);
  line("DPG paths taken", static_cast<double>(plain.st.pathsGenerated), static_cast<double>(seeded.st.pathsGenerated), false);
  line("Anchors evaluated", static_cast<double>(plain.st.anchors), static_cast<double>(seeded.st.anchors), false);
  line("Candidate schemes", plain.st.candidates, seeded.st.candidates, false);
  line("C++ time", plain.st.ms, seeded.st.ms, true);
  g.set("rows", rows);
  g.set("sameCost", plain.best.ok == seeded.best.ok && (!plain.best.ok || std::fabs(plain.bestCost - seeded.bestCost) < 1e-6));
  g.set("shortened", seeded.st.anchors < plain.st.anchors || seeded.st.rounds < plain.st.rounds);
  return g;
}

/** One row of the method comparison table; `delta` is the extra latency over the final scheme. */
json::Value cmpRow(const Model& M, const std::string& method, const std::string& paper, const Scheme* s,
                   const Scheme& fin, const std::string& note, double ms, const std::string& kind) {
  json::Value r = json::Value::object();
  r.set("method", method); r.set("paper", paper); r.set("note", note); r.set("kind", kind);
  r.set("ms", ms >= 0 ? json::Value(ms) : json::Value());
  const bool ok = s && s->ok;
  r.set("ok", ok);
  if (ok) {
    r.set("lat", s->lat);
    if (fin.ok) r.set("delta", s->lat - fin.lat);
    r.set("bw", s->res.bw); r.set("cpu", s->res.cpu); r.set("norm", s->res.norm);
    r.set("text", isfgText(M, s->g, &s->hosts));
  }
  return r;
}

std::string summary(const LoaResult& r) {
  return std::to_string(r.st.rounds) + " rounds, " + std::to_string(r.st.anchors) + " anchors, stop: " + stopName(r.stop);
}

std::string fullSummary(const FullResult& r) {
  return (r.complete ? "complete (" + r.stop + ")" : "stopped by the time budget") + ", " + fmt(r.isfgSearched) + "/" + fmt(r.isfgTotal) +
         " I-SFGs searched, " + fmt(r.nodes) + " partial placements, " + fmt(r.leaves) + " complete schemes";
}

const Scheme& better(const Model& M, const Scheme& a, const Scheme& b, Objective obj) {
  const double ca = schemeCost(M, a, obj), cb = schemeCost(M, b, obj);
  if (cb < ca - EPS) return b;
  if (obj == Objective::Resource && std::fabs(cb - ca) <= EPS && b.ok && b.lat < a.lat - EPS) return b;
  return a;
}

} // namespace

json::Value checkConfig(const json::Value& input) {
  json::Value rep = json::Value::object();
  Config cfg;
  try { cfg = parseConfig(input); }
  catch (const std::exception& e) { rep.set("ok", false); rep.set("error", std::string("Bad config: ") + e.what()); return rep; }
  const std::string bad = validateConfig(cfg);
  json::Value warns = json::Value::array();
  for (const auto& w : configWarnings(cfg)) warns.push(w);
  rep.set("warnings", warns);
  if (!bad.empty()) { rep.set("ok", false); rep.set("error", bad); return rep; }
  const Model M = buildModel(cfg);
  json::Value layers = json::Value::array();
  for (const auto& l : M.layers) { json::Value a = json::Value::array(); for (int f : l) a.push(M.V[f].name); layers.push(a); }
  rep.set("layers", layers);
  rep.set("space", spaceJson(M));
  rep.set("connected", !M.totalPaths.isZero());
  rep.set("ok", true);
  return rep;
}

json::Value runHybrid(const json::Value& input) {
  json::Value rep = json::Value::object();
  json::Value phases = json::Value::array();
  const double tAll = nowMs();

  Config cfg;
  try { cfg = parseConfig(input); }
  catch (const std::exception& e) { rep.set("ok", false); rep.set("error", std::string("Bad config: ") + e.what()); return rep; }
  const std::string bad = validateConfig(cfg);
  if (!bad.empty()) { rep.set("ok", false); rep.set("error", bad); return rep; }

  // ── 1. Model ────────────────────────────────────────────────
  double t = nowMs();
  const Model M = buildModel(cfg);
  if (M.totalPaths.isZero()) { rep.set("ok", false); rep.set("error", "The ingress and egress nodes are not connected (check link bandwidth against b_u)."); return rep; }
  {
    json::Value m = json::Value::object();
    m.set("n", M.n); m.set("L", M.L); m.set("vnfs", static_cast<int>(M.V.size()));
    json::Value layers = json::Value::array();
    for (const auto& l : M.layers) { json::Value a = json::Value::array(); for (int f : l) a.push(M.V[f].name); layers.push(a); }
    m.set("layers", layers);
    m.set("totalPaths", M.totalPaths.str());
    m.set("bruteSize", bruteForceSize(M).str());
    m.set("countBackend", BigCount::backend());
    m.set("lmin", M.lmin);
    m.set("instances", static_cast<int>(cfg.instances.size()));
    m.set("cbwMax", M.cbwMax); m.set("ccpuMax", M.ccpuMax);
    m.set("wBw", cfg.wBw); m.set("wCpu", 1 - cfg.wBw);
    m.set("pkt", cfg.pkt); m.set("trep", M.trep); m.set("tint", M.tint);
    m.set("layouts", structureCount(M).toDouble());
    m.set("space", spaceJson(M));
    rep.set("model", m);
    phases.push(phase("model", "Build model", "cpp/src/common/model.cpp", "buildModel", msSince(t),
      std::to_string(M.L) + " levels, " + structureCount(M).str() + " I-SFGs, " + M.totalPaths.str() + " DPG paths, " + std::to_string(cfg.instances.size()) + " existing instances"));
  }

  // ── 2. Seed: SCA + VDA (paper 2) ────────────────────────────
  t = nowMs();
  const ScaResult sca = runSCA(M);
  const VdaResult vda = runVDA(M, sca.g);
  const VdaAllResult vdaAll = runVDAAllLayouts(M);
  const double seedMs = msSince(t);
  Scheme seed;
  if (vda.scheme.ok) seed = vda.scheme;
  if (vdaAll.scheme.ok && (!seed.ok || vdaAll.scheme.lat < seed.lat - EPS)) seed = vdaAll.scheme;
  // ROA seed: the VDA placements re-routed on fewest-hop paths, cheaper resource cost wins
  Scheme seedR;
  for (const Scheme* s : { &vda.scheme, &vdaAll.scheme }) {
    if (!s->ok) continue;
    Scheme h = makeScheme(M, s->hosts, s->g, s->source, true);
    if (h.ok && (!seedR.ok || h.res.norm < seedR.res.norm - EPS)) seedR = h;
  }
  {
    json::Value s = json::Value::object();
    s.set("layout", isfgText(M, sca.g));
    s.set("placementFree", sca.placementFree);
    s.set("layouts", sca.layouts);
    rep.set("sca", s);
    json::Value v = schemeJson(M, vda.scheme);
    v.set("failReason", vda.failReason);
    rep.set("vda", v);
    json::Value va = schemeJson(M, vdaAll.scheme);
    va.set("tried", vdaAll.tried);
    rep.set("vdaAll", va);
    rep.set("seed", seed.ok ? schemeJson(M, seed) : json::Value());
    rep.set("seedRes", seedR.ok ? schemeJson(M, seedR) : json::Value());
    phases.push(phase("seed", "Seed with SCA + VDA (paper 2)", "cpp/src/paper2/sca_vda.cpp", "runSCA / runVDA / runVDAAllLayouts", seedMs,
      seed.ok ? "seed = " + fmt(seed.lat) + " ms from " + seed.source : "VDA found no feasible scheme; LOA starts from inf"));
  }

  // ── 3. Full-combination search from the seed ────────────────
  FullResult full, fullR;
  Scheme final = seed;
  if (cfg.full) {
    full = runFull(M, Objective::Latency, seed.ok ? &seed : nullptr);
    final = full.best;
    rep.set("full", fullJson(M, full));
    phases.push(phase("full", "Full-combination search, latency (every I-SFG × placement)", "cpp/src/hybrid/full.cpp", "runFull(M, Latency, &seed)", full.ms, fullSummary(full)));
  }
  Scheme finalR = seedR;
  if (cfg.roa && cfg.full) {
    fullR = runFull(M, Objective::Resource, seedR.ok ? &seedR : nullptr);
    finalR = fullR.best;
    rep.set("fullRes", fullJson(M, fullR));
    phases.push(phase("fullRes", "Full-combination search, resources", "cpp/src/hybrid/full.cpp", "runFull(M, Resource, &seedRes)", fullR.ms, fullSummary(fullR)));
  }
  // The full search's answer is proven when it finished; only then is LOA / ROA unnecessary
  const bool needLoa = !cfg.full || !full.complete;
  const bool needRoa = cfg.roa && (!cfg.full || !fullR.complete);

  // ── 4. LOA / ROA (paper 1): the fallback when the search ran out of time.
  //       With `compare` they also run for the comparison table; that does not change the answer.
  LoaResult seeded, plain;
  bool haveSeeded = false, havePlain = false;
  const std::string loaRole = needLoa ? "fallback" : "for comparison";
  if (cfg.seed && (needLoa || cfg.compare)) {
    seeded = runLOA(M, seed.ok ? &seed : nullptr);
    haveSeeded = true;
    rep.set("loaSeeded", loaJson(M, seeded));
    phases.push(phase("loa", "LOA search, seeded, " + loaRole + " (paper 1, Alg. 2 + 3)", "cpp/src/paper1/loa.cpp", "runLOA(M, &seed)", seeded.st.ms, summary(seeded)));
  }
  if (cfg.compare || (!cfg.seed && needLoa)) {
    plain = runLOA(M, nullptr);
    havePlain = true;
    rep.set("loaPlain", loaJson(M, plain));
    phases.push(phase("loaPlain", "LOA search, unseeded, " + loaRole + " (paper 1 alone)", "cpp/src/paper1/loa.cpp", "runLOA(M, nullptr)", plain.st.ms, summary(plain)));
  }
  const LoaResult* primary = haveSeeded ? &seeded : havePlain ? &plain : nullptr;
  if (needLoa) {
    if (haveSeeded) final = better(M, final, seeded.best, Objective::Latency);
    if (havePlain)  final = better(M, final, plain.best, Objective::Latency);
  }

  LoaResult roaS, roaP;
  bool haveRoaS = false, haveRoaP = false;
  if (cfg.roa) {
    const std::string roaRole = needRoa ? "fallback" : "for comparison";
    if (cfg.seed && (needRoa || cfg.compare)) {
      roaS = runROA(M, seedR.ok ? &seedR : nullptr);
      haveRoaS = true;
      rep.set("roaSeeded", loaJson(M, roaS));
      phases.push(phase("roa", "ROA search, seeded, " + roaRole + " (paper 1, Alg. 4 + 5)", "cpp/src/paper1/loa.cpp", "runROA(M, &seedRes)", roaS.st.ms, summary(roaS)));
    }
    if (cfg.compare || (!cfg.seed && needRoa)) {
      roaP = runROA(M, nullptr);
      haveRoaP = true;
      rep.set("roaPlain", loaJson(M, roaP));
      phases.push(phase("roaPlain", "ROA search, unseeded, " + roaRole + " (paper 1 alone)", "cpp/src/paper1/loa.cpp", "runROA(M, nullptr)", roaP.st.ms, summary(roaP)));
    }
  }
  const LoaResult* rp = haveRoaS ? &roaS : haveRoaP ? &roaP : nullptr;
  if (needRoa) {
    if (haveRoaS) finalR = better(M, finalR, roaS.best, Objective::Resource);
    if (haveRoaP) finalR = better(M, finalR, roaP.best, Objective::Resource);
  }
  // Time the latency answer really needed: seed + search, plus LOA only when it was the fallback
  double solveMs = seedMs + (cfg.full ? full.ms : 0);
  if (needLoa) solveMs += (haveSeeded ? seeded.st.ms : 0) + (havePlain ? plain.st.ms : 0);
  rep.set("solveMs", solveMs);

  // ── 5. Certify both objectives ──────────────────────────────
  t = nowMs();
  json::Value cert = certify(M, primary, cfg.full ? &full : nullptr, Objective::Latency, final);
  cert.set("bound", theorem1(M, vda.scheme.ok ? vda.scheme : seed));
  const bool optimal = cert.get("label").str() == "OPTIMAL";
  json::Value gaps = json::Value::object();
  if (havePlain)  gaps.set("loaPlain", gapJson(M, plain, final, Objective::Latency, optimal));
  if (haveSeeded) gaps.set("loaSeeded", gapJson(M, seeded, final, Objective::Latency, optimal));
  cert.set("gaps", gaps);
  rep.set("certificate", cert);
  rep.set("final", schemeJson(M, final));
  std::string certSummary = cert.get("label").str();

  if (cfg.roa) {
    json::Value certR = certify(M, rp, cfg.full ? &fullR : nullptr, Objective::Resource, finalR);
    const bool optR = certR.get("label").str() == "OPTIMAL";
    json::Value gr = json::Value::object();
    if (haveRoaP) gr.set("roaPlain", gapJson(M, roaP, finalR, Objective::Resource, optR));
    if (haveRoaS) gr.set("roaSeeded", gapJson(M, roaS, finalR, Objective::Resource, optR));
    certR.set("gaps", gr);
    rep.set("certificateRes", certR);
    rep.set("finalRes", schemeJson(M, finalR));
    certSummary += " / ROA " + certR.get("label").str();
  }
  phases.push(phase("certify", "Certify", "cpp/src/hybrid/hybrid.cpp", "certify", msSince(t), certSummary));

  // ── Seed effect (what Solution A saved) ─────────────────────
  if (haveSeeded && havePlain) rep.set("seedEffect", seedEffect(plain, seeded));
  if (haveRoaS && haveRoaP)    rep.set("seedEffectRes", seedEffect(roaP, roaS));

  // ── Method comparison table ─────────────────────────────────
  const double totalMs = msSince(tAll);
  json::Value rows = json::Value::array();
  {
    json::Value r = json::Value::object();
    r.set("method", "SCA"); r.set("paper", "paper 2 · Algorithm 2"); r.set("ok", false); r.set("kind", "");
    r.set("text", isfgText(M, sca.g)); r.set("ms", json::Value());
    r.set("note", "Chosen before placement: placement-free delay " + fmt(sca.placementFree) + " ms, " + fmt(sca.layouts) + " I-SFGs considered.");
    rows.push(r);
  }
  rows.push(cmpRow(M, "VDA", "paper 2 · Algorithm 3", &vda.scheme, final,
                   vda.scheme.ok ? "Greedy placement of the SCA I-SFG (shares instances when it can)." : vda.failReason, seedMs, ""));
  rows.push(cmpRow(M, "VDA, every I-SFG", "paper 2 · extension", &vdaAll.scheme, final,
                   vdaAll.tried > 0 ? "VDA run on all " + fmt(vdaAll.tried) + " I-SFGs, best kept." : "Skipped: too many I-SFGs.", -1, ""));
  if (havePlain)
    rows.push(cmpRow(M, "LOA", "paper 1 · Algorithms 2 + 3", &plain.best, final,
                     std::string("Stop: ") + stopName(plain.stop) + ". " + std::to_string(plain.st.rounds) + " rounds, " + std::to_string(plain.st.anchors) + " anchors.", plain.st.ms, ""));
  if (haveSeeded)
    rows.push(cmpRow(M, "Hybrid: VDA-seeded LOA", "paper 1 + paper 2", &seeded.best, final,
                     std::string("Stop: ") + stopName(seeded.stop) + ". " + (seeded.improvedSeed ? "Improved on the seed." : "Seed was already the best."), seeded.st.ms, "inA"));
  if (haveRoaP)
    rows.push(cmpRow(M, "ROA", "paper 1 · Algorithms 4 + 5", &roaP.best, final,
                     std::string("Stop: ") + stopName(roaP.stop) + ". " + std::to_string(roaP.st.rounds) + " rounds, " + std::to_string(roaP.st.anchors) + " anchors.", roaP.st.ms, ""));
  if (haveRoaS)
    rows.push(cmpRow(M, "Hybrid-ROA: VDA-seeded ROA", "paper 1 + paper 2", &roaS.best, final,
                     std::string("Stop: ") + stopName(roaS.stop) + ". " + (roaS.improvedSeed ? "Improved on the seed." : "Seed was already the cheapest."), roaS.st.ms, "inA"));
  if (cfg.full) {
    rows.push(cmpRow(M, "Full search (latency)", "every I-SFG × every placement", &full.best, final,
                     (full.complete ? "Complete: " : "Time budget hit: ") + fmt(full.leaves) + " complete schemes of " + fmt(full.space) + " evaluated, the rest cut by bounds." +
                     (full.improvedSeed ? " Improved on the VDA seed." : " The VDA seed was already the best."), full.ms, "inA"));
    if (cfg.roa)
      rows.push(cmpRow(M, "Full search (resources)", "every I-SFG × every placement", &fullR.best, final,
                       (fullR.complete ? "Complete: " : "Time budget hit: ") + fmt(fullR.leaves) + " complete schemes evaluated." +
                       (fullR.improvedSeed ? " Improved on the VDA seed." : " The VDA seed was already the cheapest."), fullR.ms, "inA"));
  }
  rows.push(cmpRow(M, "Final: lowest latency", cert.get("label").str(), &final, final, final.source, solveMs, "anchor"));
  if (cfg.roa)
    rows.push(cmpRow(M, "Final: least resources", rep.get("certificateRes").get("label").str(), &finalR, final, finalR.source, -1, "anchor"));
  rep.set("comparison", rows);

  rep.set("phases", phases);
  rep.set("totalMs", totalMs);
  rep.set("ok", true);
  return rep;
}
