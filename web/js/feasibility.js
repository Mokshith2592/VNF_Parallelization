/* =============================================================
   LOA Hybrid — Feasibility page
   Draws data/feasibility.json, written by
   loa_hybrid.exe --feasibility (runFeasibility() in cpp/src/hybrid/bench.cpp).
   Every number and verdict comes from that file.
   ============================================================= */

const $ = (s) => document.querySelector(s);
const big = (x) => (x >= 1e6 ? x.toExponential(1).replace('e+', ' × 10^') : Math.round(x).toLocaleString());
const ms = (x) => (x == null ? '—' : x < 1 ? x.toFixed(2) + ' ms' : x < 1000 ? x.toFixed(0) + ' ms' : (x / 1000).toFixed(1) + ' s');
const dur = (x) => {
  if (x < 1000) return ms(x);
  const s = x / 1000;
  if (s < 3600) return s.toFixed(0) + ' s';
  if (s < 86400 * 2) return (s / 3600).toFixed(1) + ' h';
  if (s < 86400 * 365 * 2) return (s / 86400).toFixed(0) + ' days';
  return big(s / (86400 * 365)) + ' years';
};
const VERDICT = { interactive: 'interactive (≤ 1 s)', feasible: 'feasible (within budget)', partial: 'partial', infeasible: 'out of budget', 'not run': 'not run' };

function countsHTML(D) {
  return `<table class="pt"><thead><tr><th>VNFs in one level</th>` + D.structureCounts.map((c) => `<th class="pc">${c.vnfs}</th>`).join('') + `</tr></thead><tbody>` +
    `<tr><td>Every I-SFG (paper 2)</td>` + D.structureCounts.map((c) => `<td class="pc">${big(c.isfg)}</td>`).join('') + `</tr>` +
    `<tr><td>Stage sequences only</td>` + D.structureCounts.map((c) => `<td class="pc">${big(c.staged)}</td>`).join('') + `</tr>` +
    `</tbody></table>`;
}

function cell(p) {
  if (!p || p.verdict === 'not run') return `<td class="heat v-notrun">—</td>`;
  return `<td class="heat v-${p.verdict}" title="${VERDICT[p.verdict]}">` +
    `<b>${ms(p.msMean)}</b><span>max ${ms(p.msMax)}</span><span>${p.completed}/${p.runs} done</span></td>`;
}

function grid(S, D, f, title) {
  const at = (n, F) => S.points.find((p) => p.nodes === n && p.vnfs === F);
  return `<h3 class="subh">${title}</h3><div class="tbl-wrap"><table class="pt heatmap"><thead><tr><th>Nodes \\ VNFs</th>` +
    D.vnfGrid.map((F) => `<th class="pc">${F}</th>`).join('') + `</tr></thead><tbody>` +
    D.nodeGrid.map((n) => `<tr><th>${n}</th>` + D.vnfGrid.map((F) => f(at(n, F))).join('') + `</tr>`).join('') +
    `</tbody></table></div>`;
}

const bruteCell = (p) => (!p || p.verdict === 'not run' ? `<td class="pc muted">—</td>` : `<td class="pc">${dur(p.bruteMsEst)}</td>`);
const loaCell = (p) => {
  if (!p || p.verdict === 'not run' || !p.feasible) return `<td class="pc muted">—</td>`;
  const parts = [];
  if (p.loaFound) parts.push(p.loaSubopt ? `<span class="badv">${p.loaSubopt}/${p.loaFound} above, ≤ ${p.loaGapMaxPct.toFixed(1)} %</span>` : `<span class="good">${p.loaFound}/${p.loaFound} optimal</span>`);
  if (p.loaMiss) parts.push(`<span class="badv">${p.loaMiss} no scheme</span>`);
  return `<td class="pc sm">${parts.join('<br>')}</td>`;
};

function scenarioHTML(S, D) {
  const lim = S.limits.map((l) => `<li><b>${l.nodes} nodes:</b> interactive up to ${l.maxInteractive || '—'} VNFs, ` +
    `within ${ms(D.budgetMs)} up to ${l.maxFeasible || '—'} VNFs</li>`).join('');
  return `<section class="panel mt"><h2>${S.title}</h2>` +
    `<ul class="reasons">${lim}</ul>` +
    grid(S, D, cell, `Full search time, mean (max) <small>${D.runs} random instances per cell, budget ${ms(D.budgetMs)}</small>`) +
    `<div class="clegend">${['interactive', 'feasible', 'partial', 'infeasible'].map((v) => `<span><i class="sw heat v-${v}"></i>${VERDICT[v]}</span>`).join('')}</div>` +
    grid(S, D, bruteCell, 'Plain enumeration of every combination, estimated') +
    grid(S, D, loaCell, 'Paper 1’s LOA against the proven optimum') +
    `</section>`;
}

(async function boot() {
  let D;
  try {
    const r = await fetch('data/feasibility.json', { cache: 'no-store' });
    D = await r.json();
  } catch (e) {
    $('#scenarios').innerHTML = `<section class="panel mt"><p class="muted">No data/feasibility.json yet. Run <code>cpp\\bin\\loa_hybrid.exe --feasibility --out ..\\web\\data\\feasibility.json</code>.</p></section>`;
    return;
  }
  $('#counts').innerHTML = countsHTML(D);
  $('#setting').textContent = D.setting;
  $('#scenarios').innerHTML = D.scenarios.map((S) => scenarioHTML(S, D)).join('');
})();
