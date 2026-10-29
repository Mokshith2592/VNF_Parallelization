/* =============================================================
   LOA Hybrid — Paper-comparison summary (see summary.hpp)
   ============================================================= */
#include "app/summary.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

const char* IDS[] = { "capacity", "bandwidth", "requests", "netsize", "sfclen", "parallel", "heavytail", "weights" };
const char* BASELINES[] = { "VDA", "Serial*", "Parallel*" };

bool readAll(const std::string& path, std::string& out) {
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
  return true;
}

const double NaN = std::nan("");
bool ok(double x) { return std::isfinite(x); }

// Field of one algorithm at one point (NaN when absent or null)
double val(const json::Value& algos, const std::string& algo, const std::string& field) {
  if (!algos.has(algo)) return NaN;
  const json::Value& a = algos.get(algo);
  return a.has(field) && a.get(field).isNumber() ? a.get(field).num() : NaN;
}
// Relative reduction of v against base, in percent
double imp(double base, double v) { return ok(base) && base != 0 && ok(v) ? (base - v) / base * 100 : NaN; }
double mean(const std::vector<double>& v) {
  double s = 0; int n = 0;
  for (double x : v) if (ok(x)) { s += x; n++; }
  return n ? s / n : NaN;
}
double bestBase(const json::Value& algos, const std::string& field) {
  double b = NaN;
  for (const char* k : BASELINES) { const double x = val(algos, k, field); if (ok(x) && (!ok(b) || x < b)) b = x; }
  return b;
}
// Paper point at x (empty object when the paper states nothing there)
json::Value paperAt(const json::Value& pts, double x) {
  for (std::size_t i = 0; i < pts.size(); i++) if (std::fabs(pts[i].numOr("x", NaN) - x) < 1e-9) return pts[i];
  return json::Value::object();
}
json::Value num(double x) { return ok(x) ? json::Value(x) : json::Value(); }
std::string f1(double x) { char b[32]; std::snprintf(b, sizeof b, "%.1f", x); return ok(x) ? b : "—"; }
std::string f2(double x) { char b[32]; std::snprintf(b, sizeof b, "%.2f", x); return ok(x) ? b : "—"; }

} // namespace

json::Value resultsSummary(const std::string& webRoot) {
  json::Value out = json::Value::object(), exps = json::Value::array();
  json::Value paper = json::Value::object();
  std::string text;
  if (readAll(webRoot + "/data/paper_results.json", text)) paper = json::parse(text);
  json::Value meta;

  for (const char* id : IDS) {
    json::Value X = json::Value::object();
    X.set("id", id);
    const json::Value P = paper.has(id) ? paper.get(id) : json::Value::object();
    X.set("fig", P.strOr("fig", ""));
    if (!readAll(webRoot + "/data/results/" + id + ".json", text)) { X.set("missing", true); exps.push(X); continue; }
    const json::Value file = json::parse(text);
    if (!file.has("experiments") || file.get("experiments").size() == 0) { X.set("missing", true); exps.push(X); continue; }
    if (meta.isNull()) {
      meta = json::Value::object();
      meta.set("runs", file.numOr("runs", 0)); meta.set("kmax", file.numOr("kmax", 0)); meta.set("candCap", file.numOr("candCap", 0));
    }
    const json::Value& E = file.get("experiments")[0];
    X.set("title", E.strOr("title", id)); X.set("xlabel", E.strOr("xlabel", ""));
    const json::Value& pts = E.get("points");
    const json::Value pDelay = P.has("points") ? P.get("points") : json::Value::array();
    const json::Value R = P.has("roa") ? P.get("roa") : json::Value::object();
    const json::Value pRes = R.has("points") ? R.get("points") : json::Value::array();
    const bool hasROA = pts.size() && pts[0].get("algos").has("Hybrid-ROA");
    X.set("hasROA", hasROA);

    std::vector<double> hy, lo, accL, accH, msL, msH, sb, sv, fl, accR, accHR, msHR, hyDelay, paperImp;
    json::Value points = json::Value::array();
    bool parallelIsVda = pts.size() > 0;
    // Averages compared with the paper use only our points inside the x range the paper
    // reports; extra points (e.g. 10 or 150 nodes) are still drawn in the charts.
    double xLo = -INFINITY, xHi = INFINITY;
    if (pDelay.size()) {
      xLo = INFINITY; xHi = -INFINITY;
      for (std::size_t i = 0; i < pDelay.size(); i++) {
        const double px = pDelay[i].numOr("x", NaN);
        if (ok(px)) { xLo = std::min(xLo, px); xHi = std::max(xHi, px); }
      }
    }
    int outside = 0;
    for (std::size_t i = 0; i < pts.size(); i++) {
      const json::Value& A = pts[i].get("algos");
      const double x = pts[i].numOr("x", NaN);
      const json::Value q = paperAt(pDelay, x), qr = paperAt(pRes, x);
      json::Value d = json::Value::object();
      d.set("hyVsVda", num(imp(val(A, "VDA", "delay"), val(A, "Hybrid", "delay"))));
      d.set("paperLoa", num(q.numOr("loa", NaN)));
      d.set("paperImpVDA", num(q.numOr("impVDA", NaN)));
      d.set("paperRes", num(qr.numOr("res", NaN)));
      d.set("paperSaveBest", num(qr.numOr("saveBest", NaN)));
      if (hasROA) d.set("resSaveBest", num(imp(bestBase(A, "resMax"), val(A, "Hybrid-ROA", "resMax"))));
      json::Value p = json::Value::object();
      p.set("x", x); p.set("algos", A); p.set("d", d);
      points.push(p);

      if (x < xLo - 1e-9 || x > xHi + 1e-9) { outside++; continue; }
      hy.push_back(imp(val(A, "VDA", "delay"), val(A, "Hybrid", "delay")));
      lo.push_back(imp(val(A, "VDA", "delay"), val(A, "LOA", "delay")));
      accL.push_back(val(A, "LOA", "accept")); accH.push_back(val(A, "Hybrid", "accept"));
      msL.push_back(val(A, "LOA", "ms"));      msH.push_back(val(A, "Hybrid", "ms"));
      hyDelay.push_back(val(A, "Hybrid", "delay"));
      if (ok(q.numOr("impVDA", NaN))) paperImp.push_back(q.numOr("impVDA", NaN));
      if (std::fabs(val(A, "Parallel*", "delay") - val(A, "VDA", "delay")) > 1e-6) parallelIsVda = false;
      if (hasROA) {
        sb.push_back(imp(bestBase(A, "resMax"), val(A, "Hybrid-ROA", "resMax")));
        sv.push_back(imp(val(A, "VDA", "resMax"), val(A, "Hybrid-ROA", "resMax")));
        fl.push_back(imp(val(A, "LOA", "res"), val(A, "Hybrid-ROA", "res")));
        accR.push_back(val(A, "ROA", "accept")); accHR.push_back(val(A, "Hybrid-ROA", "accept"));
        msHR.push_back(val(A, "Hybrid-ROA", "ms"));
      }
    }
    X.set("points", points);
    X.set("paperPoints", pDelay);
    X.set("paperRoaPoints", pRes);

    json::Value D = json::Value::object();
    D.set("hy", num(mean(hy))); D.set("lo", num(mean(lo)));
    const bool pvStated = P.has("avgImpVDA");
    D.set("pv", num(pvStated ? P.numOr("avgImpVDA", NaN) : mean(paperImp)));
    D.set("pvDerived", !pvStated && !paperImp.empty());   // mean of the two end points the paper states
    D.set("pb", num(P.has("avgImpBest") ? P.numOr("avgImpBest", NaN) : P.numOr("heavyImpBest", NaN)));
    D.set("accLOA", num(mean(accL))); D.set("accHybrid", num(mean(accH)));
    D.set("msLOA", num(mean(msL)));   D.set("msHybrid", num(mean(msH)));
    X.set("delay", D);
    if (hasROA) {
      json::Value S = json::Value::object();
      S.set("sb", num(mean(sb))); S.set("sv", num(mean(sv))); S.set("fl", num(mean(fl)));
      S.set("pSaveBest", num(R.numOr("avgSaveBest", NaN))); S.set("pSaveVDA", num(R.numOr("avgSaveVDA", NaN)));
      S.set("accROA", num(mean(accR))); S.set("accHROA", num(mean(accHR))); S.set("msHROA", num(mean(msHR)));
      X.set("res", S);
    }

    json::Value notes = json::Value::array();
    if (outside)
      notes.push(std::to_string(outside) + " of our points lie outside the range the paper reports (" + f1(xLo) + "–" + f1(xHi) +
                 "); they are in the chart but not in the averages compared with the paper.");
    if (parallelIsVda)
      notes.push("Parallel* sits exactly under VDA: with small split/merge costs, SCA picks the fully parallel layout. Dashed lines only join the two values the paper states.");
    if (std::string(id) == "heavytail")
      notes.push("Paper: heavy-tailed average " + f2(P.numOr("heavyLoa", NaN)) + " ms (uniform " + f2(P.numOr("uniformLoa", NaN)) + " ms), " +
                 f1(P.numOr("heavyImpBest", NaN)) + "% below the best baseline; ROA " + f2(R.numOr("heavyRes", NaN)) + " (uniform " +
                 f2(R.numOr("uniformRes", NaN)) + "). Ours: hybrid " + f1(mean(hyDelay)) + " ms.");
    if (std::string(id) == "netsize") {
      auto avg = [&](const char* a, const char* f) { std::vector<double> v; for (std::size_t i = 0; i < pts.size(); i++) v.push_back(val(pts[i].get("algos"), a, f)); return mean(v); };
      const json::Value J = P.has("jain") ? P.get("jain") : json::Value::object(), C = P.has("cv") ? P.get("cv") : json::Value::object();
      notes.push("Load balance (Fig. 15, Jain's index / CV of node use): paper LOA " + f2(J.numOr("LOA", NaN)) + " / " + f2(C.numOr("LOA", NaN)) +
                 ", ROA " + f2(J.numOr("ROA", NaN)) + " / " + f2(C.numOr("ROA", NaN)) + ", VDA " + f2(J.numOr("VDA", NaN)) +
                 "; ours hybrid " + f2(avg("Hybrid", "jain")) + " / " + f2(avg("Hybrid", "cv")) + ", Hybrid-ROA " +
                 (hasROA ? f2(avg("Hybrid-ROA", "jain")) + " / " + f2(avg("Hybrid-ROA", "cv")) : std::string("—")) +
                 ", VDA " + f2(avg("VDA", "jain")) + " / " + f2(avg("VDA", "cv")) + ".");
    }
    X.set("notes", notes);
    exps.push(X);
  }
  out.set("meta", meta);
  out.set("experiments", exps);
  return out;
}
