/* =============================================================
   LOA Hybrid — Paper comparison page
   Draws GET /api/summary, computed by resultsSummary() in
   cpp/src/app/summary.cpp from data/results/<id>.json (loa_experiments.exe)
   and data/paper_results.json. For every experiment: a delay chart
   (LOA side) and a resource chart (ROA side), each with a table view,
   plus two summary tables. No value is computed here.
   ============================================================= */

// Fixed categorical order (validated palette slots 1–7)
const SERIES = [
  { key: 'LOA',        cls: 's1', label: 'LOA (ours)' },
  { key: 'Hybrid',     cls: 's2', label: 'Hybrid (ours)' },
  { key: 'VDA',        cls: 's3', label: 'VDA (paper 2)' },
  { key: 'Serial*',    cls: 's4', label: 'Serial*' },
  { key: 'Parallel*',  cls: 's5', label: 'Parallel*' },
  { key: 'ROA',        cls: 's6', label: 'ROA (ours)' },
  { key: 'Hybrid-ROA', cls: 's7', label: 'Hybrid-ROA (ours)' },
];
const METRIC = {
  delay:  { label: 'Average delay (ms)', get: (a) => a.delay, paper: (q) => q.loa, paperName: 'LOA, value reported in the paper', fmt: (x) => f1(x) },
  resMax: { label: 'Normalised resource consumption', get: (a) => a.resMax, paper: (q) => q.res, paperName: 'ROA, value reported in the paper', fmt: (x) => f3(x) },
};

const $  = (s) => document.querySelector(s);
const nn = (x) => x == null || Number.isNaN(x);
const f1 = (x) => (nn(x) ? '—' : x.toFixed(1));
const f2 = (x) => (nn(x) ? '—' : x.toFixed(2));
const f3 = (x) => (nn(x) ? '—' : x.toFixed(3));
const pct = (x) => (nn(x) ? '—' : (x >= 0 ? '' : '−') + Math.abs(x).toFixed(1) + '%');
const pc0 = (x) => (nn(x) ? '—' : Math.round(x * 100) + '%');
const xfmt = (x) => (Math.abs(x - Math.round(x)) < 1e-9 ? String(Math.round(x)) : x.toFixed(1));

let SUM = null;
const byId = {};
const seriesOf = (E) => SERIES.filter((s) => E.points[0].algos[s.key]);


// ─── Chart ────────────────────────────────────────────────────

function niceTicks(lo, hi, n = 5) {
  const span = hi - lo || 1, step0 = span / n;
  const mag = Math.pow(10, Math.floor(Math.log10(step0)));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= step0);
  const a = Math.floor(lo / step) * step, b = Math.ceil(hi / step) * step;
  const t = []; for (let v = a; v <= b + 1e-9; v += step) t.push(+v.toFixed(6));
  return t;
}

function chartSVG(E, paperPts, metric) {
  const M = METRIC[metric], S = seriesOf(E);
  const W = 640, H = 300, m = { l: 52, r: 16, t: 14, b: 40 };
  const xs = E.points.map((p) => p.x);
  const vals = [];
  E.points.forEach((p) => S.forEach((s) => { const d = M.get(p.algos[s.key]); if (!nn(d)) vals.push(d); }));
  const pp = (paperPts || []).filter((q) => M.paper(q) != null);
  pp.forEach((q) => vals.push(M.paper(q)));
  const yt = niceTicks(Math.min(...vals) * 0.95, Math.max(...vals) * 1.03);
  const y0 = yt[0], y1 = yt[yt.length - 1];
  const xpos = (x) => m.l + (xs.length === 1 ? 0.5 : (x - xs[0]) / (xs[xs.length - 1] - xs[0])) * (W - m.l - m.r);
  const ypos = (v) => m.t + (1 - (v - y0) / (y1 - y0)) * (H - m.t - m.b);

  let g = '';
  yt.forEach((v) => {
    g += `<line class="gl" x1="${m.l}" x2="${W - m.r}" y1="${ypos(v)}" y2="${ypos(v)}"/>` +
         `<text class="yl" x="${m.l - 8}" y="${ypos(v) + 4}">${metric === 'delay' ? xfmt(v) : v.toFixed(2)}</text>`;
  });
  xs.forEach((x) => { g += `<text class="xl" x="${xpos(x)}" y="${H - m.b + 18}">${xfmt(x)}</text>`; });
  g += `<text class="axt" x="${(m.l + W - m.r) / 2}" y="${H - 4}">${E.xlabel}</text>`;
  g += `<text class="axt" transform="translate(12 ${(m.t + H - m.b) / 2}) rotate(-90)">${M.label}</text>`;

  let lines = '';
  S.slice().reverse().forEach((s) => {
    const pts = E.points.map((p) => [xpos(p.x), ypos(M.get(p.algos[s.key])), M.get(p.algos[s.key])]).filter((q) => !nn(q[2]));
    if (!pts.length) return;
    lines += `<polyline class="ln ${s.cls}" points="${pts.map((q) => q[0] + ',' + q[1]).join(' ')}"/>`;
    pts.forEach((q) => { lines += `<circle class="mk ${s.cls}" cx="${q[0]}" cy="${q[1]}" r="4"/>`; });
  });
  if (pp.length) {
    const ppos = pp.map((q) => [xpos(q.x), ypos(M.paper(q))]);
    lines += `<polyline class="ln paper" points="${ppos.map((q) => q.join(',')).join(' ')}"/>`;
    ppos.forEach((q) => { lines += `<rect class="mk paper" x="${q[0] - 5}" y="${q[1] - 5}" width="10" height="10" transform="rotate(45 ${q[0]} ${q[1]})"/>`; });
  }
  const hits = xs.map((x, i) => {
    const w = (W - m.l - m.r) / Math.max(xs.length - 1, 1);
    return `<rect class="hit" data-e="${E.id}" data-m="${metric}" data-i="${i}" x="${xpos(x) - w / 2}" y="${m.t}" width="${w}" height="${H - m.t - m.b}"/>`;
  }).join('');
  return `<svg class="chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="${E.title}: ${M.label} per algorithm">${g}<line class="cross" id="cross-${E.id}-${metric}" y1="${m.t}" y2="${H - m.b}" x1="-10" x2="-10"/>${lines}${hits}</svg>`;
}

function legendHTML(E, paperName) {
  return `<div class="clegend">` +
    seriesOf(E).map((s) => `<span><i class="sw ${s.cls}"></i>${s.label}</span>`).join('') +
    (paperName ? `<span><i class="sw paper"></i>${paperName}</span>` : '') + `</div>`;
}


// ─── Tables ───────────────────────────────────────────────────

function delayTable(E) {
  const S = seriesOf(E);
  let h = `<table class="pt"><thead><tr><th>${E.xlabel}</th>` + S.map((s) => `<th>${s.label}</th>`).join('') +
    `<th>Hybrid vs VDA</th><th>Paper LOA</th><th>Paper LOA vs VDA</th><th>Accepted LOA / Hybrid</th><th>ms/req LOA / Hybrid</th></tr></thead><tbody>`;
  E.points.forEach((p) => {
    const a = p.algos, d = p.d;
    h += `<tr><td>${xfmt(p.x)}</td>` + S.map((s) => `<td class="pc">${f1(a[s.key].delay)}</td>`).join('') +
      `<td class="pc">${pct(d.hyVsVda)}</td><td class="pc">${f2(d.paperLoa)}</td><td class="pc">${nn(d.paperImpVDA) ? '—' : d.paperImpVDA.toFixed(1) + '%'}</td>` +
      `<td class="pc">${pc0(a.LOA.accept)} / ${pc0(a.Hybrid.accept)}</td><td class="pc">${f1(a.LOA.ms)} / ${f1(a.Hybrid.ms)}</td></tr>`;
  });
  return h + `</tbody></table>`;
}

function resTable(E) {
  const S = seriesOf(E);
  let h = `<table class="pt"><thead><tr><th>${E.xlabel}</th>` + S.map((s) => `<th>${s.label}</th>`).join('') +
    `<th>Hybrid-ROA saving vs best baseline</th><th>Paper ROA</th><th>Paper saving vs best baseline</th>` +
    `<th>Fixed-scale cost: Hybrid-ROA / LOA / VDA</th><th>Bandwidth (Mbps/req): Hybrid-ROA / VDA</th><th>New CPU/req: Hybrid-ROA / VDA</th><th>Accepted ROA / Hybrid-ROA</th></tr></thead><tbody>`;
  E.points.forEach((p) => {
    const a = p.algos, d = p.d, H = a['Hybrid-ROA'];
    h += `<tr><td>${xfmt(p.x)}</td>` + S.map((s) => `<td class="pc">${f3(a[s.key].resMax)}</td>`).join('') +
      `<td class="pc">${pct(d.resSaveBest)}</td><td class="pc">${f3(d.paperRes)}</td><td class="pc">${nn(d.paperSaveBest) ? '—' : d.paperSaveBest.toFixed(1) + '%'}</td>` +
      `<td class="pc">${f3(H.res)} / ${f3(a.LOA.res)} / ${f3(a.VDA.res)}</td>` +
      `<td class="pc">${f1(H.bw)} / ${f1(a.VDA.bw)}</td><td class="pc">${f1(H.cpu)} / ${f1(a.VDA.cpu)}</td>` +
      `<td class="pc">${pc0(a.ROA.accept)} / ${pc0(H.accept)}</td></tr>`;
  });
  return h + `</tbody></table>`;
}


// ─── Tooltip ──────────────────────────────────────────────────

function showTip(ev, el) {
  const E = byId[el.dataset.e], metric = el.dataset.m, M = METRIC[metric], p = E.points[+el.dataset.i];
  const cross = document.getElementById(`cross-${E.id}-${metric}`);
  const x = +el.getAttribute('x') + +el.getAttribute('width') / 2;
  cross.setAttribute('x1', x); cross.setAttribute('x2', x);
  const paper = metric === 'delay' ? p.d.paperLoa : p.d.paperRes;
  const tip = $('#tip');
  tip.innerHTML = `<b>${E.xlabel}: ${xfmt(p.x)}</b>` +
    seriesOf(E).map((s) => `<div><i class="sw ${s.cls}"></i>${s.label}<span>${M.fmt(M.get(p.algos[s.key]))}${metric === 'delay' ? ' ms' : ''}</span></div>`).join('') +
    (nn(paper) ? '' : `<div><i class="sw paper"></i>Paper<span>${M.fmt(paper)}</span></div>`);
  tip.hidden = false;
  const r = tip.getBoundingClientRect();
  let left = ev.clientX + 14, top = ev.clientY + 14;
  if (left + r.width > innerWidth - 8) left = ev.clientX - r.width - 14;
  if (top + r.height > innerHeight - 8) top = ev.clientY - r.height - 14;
  tip.style.left = Math.max(8, left) + 'px'; tip.style.top = Math.max(8, top) + 'px';
}
document.addEventListener('pointermove', (ev) => {
  const el = ev.target.closest && ev.target.closest('.hit');
  if (el) { showTip(ev, el); return; }
  $('#tip').hidden = true;
  document.querySelectorAll('.cross').forEach((c) => { c.setAttribute('x1', -10); c.setAttribute('x2', -10); });
});


// ─── Summaries ────────────────────────────────────────────────

const titleOf = (E) => E.title.replace(/ \(Fig.*$/, '');

function delaySummary() {
  let h = `<table class="pt"><thead><tr><th>Experiment</th><th>Paper</th><th>Ours: Hybrid vs VDA</th><th>Ours: LOA vs VDA</th>` +
          `<th>Paper: LOA vs VDA</th><th>Paper: LOA vs best baseline</th><th>Accepted LOA / Hybrid</th><th>ms/req LOA / Hybrid</th></tr></thead><tbody>`;
  SUM.experiments.forEach((E) => {
    if (E.missing) { h += `<tr><td>${E.id}</td><td colspan="7" class="muted">no results file yet</td></tr>`; return; }
    const D = E.delay;
    h += `<tr><td><a href="#${E.id}">${titleOf(E)}</a></td><td>${E.fig}</td>` +
         `<td class="pc"><b>${pct(D.hy)}</b></td><td class="pc">${pct(D.lo)}</td>` +
         `<td class="pc">${nn(D.pv) ? '—' : D.pv.toFixed(1) + '%'}${D.pvDerived ? '<sup>†</sup>' : ''}</td>` +
         `<td class="pc">${nn(D.pb) ? '—' : D.pb.toFixed(1) + '%'}</td>` +
         `<td class="pc">${pc0(D.accLOA)} / ${pc0(D.accHybrid)}</td><td class="pc">${f1(D.msLOA)} / ${f1(D.msHybrid)}</td></tr>`;
  });
  return h + `</tbody></table><p class="hint">Positive = lower delay than VDA. † mean of the two end points the paper states.</p>`;
}

function resSummary() {
  let h = `<table class="pt"><thead><tr><th>Experiment</th><th>Paper</th><th>Ours: Hybrid-ROA vs best baseline</th><th>Ours: Hybrid-ROA vs VDA</th>` +
          `<th>Paper: ROA vs best baseline</th><th>Paper: ROA vs VDA</th><th>Ours, fixed-scale cost: Hybrid-ROA vs LOA</th><th>Accepted ROA / Hybrid-ROA</th><th>ms/req Hybrid-ROA</th></tr></thead><tbody>`;
  SUM.experiments.forEach((E) => {
    if (E.missing || !E.res) { h += `<tr><td>${E.id}</td><td colspan="8" class="muted">no ROA results yet</td></tr>`; return; }
    const S = E.res;
    h += `<tr><td><a href="#${E.id}">${titleOf(E)}</a></td><td>${E.fig.replace('(a)', '(b)')}</td>` +
         `<td class="pc"><b>${pct(S.sb)}</b></td><td class="pc">${pct(S.sv)}</td>` +
         `<td class="pc">${nn(S.pSaveBest) ? '—' : S.pSaveBest.toFixed(1) + '%'}</td><td class="pc">${nn(S.pSaveVDA) ? '—' : S.pSaveVDA.toFixed(1) + '%'}</td>` +
         `<td class="pc">${pct(S.fl)}</td><td class="pc">${pc0(S.accROA)} / ${pc0(S.accHROA)}</td><td class="pc">${f1(S.msHROA)}</td></tr>`;
  });
  return h + `</tbody></table><p class="hint">Positive = less resource consumption. "Normalised resource consumption" follows the paper: α·bandwidth/max + β·new CPU/max, where max is the largest value any compared algorithm used at that point. The fixed-scale cost is what ROA itself minimises (same reference scale for every algorithm).</p>`;
}


// ─── Boot ─────────────────────────────────────────────────────

(async function boot() {
  try {
    const r = await fetch('/api/summary', { cache: 'no-store' });
    SUM = await r.json();
  } catch (e) {
    $('#setupNote').textContent = 'The C++ server is not reachable. Start it with cpp\\build.bat and open http://localhost:8080/results.html.';
    return;
  }
  if (!SUM.experiments) { $('#setupNote').textContent = SUM.error || 'The C++ server returned no summary.'; return; }
  const meta = SUM.meta;
  $('#setupNote').textContent = meta
    ? `C++ run: ${meta.runs} runs per point, search limits k ≤ ${meta.kmax}, ${meta.candCap} candidates per anchor. Delay and resources are averaged over accepted requests.`
    : 'No results found in web/data/results/. Run cpp\\run_experiments.bat first (see FLOW.md).';
  $('#summary').innerHTML = delaySummary();
  $('#summaryRes').innerHTML = resSummary();

  let html = '';
  SUM.experiments.forEach((E) => {
    if (E.missing) return;
    byId[E.id] = E;
    html += `<section class="panel mt" id="${E.id}"><h2>${E.title}</h2>` +
      `<h3 class="subh">Delay <small>LOA side, ${E.fig}</small></h3>${legendHTML(E, E.paperPoints.some((q) => q.loa != null) ? METRIC.delay.paperName : '')}${chartSVG(E, E.paperPoints, 'delay')}` +
      `<details class="tview"><summary>Delay table</summary><div class="tbl-wrap">${delayTable(E)}</div></details>` +
      (E.hasROA
        ? `<h3 class="subh">Resources <small>ROA side, ${E.fig.replace('(a)', '(b)')}</small></h3>${legendHTML(E, E.paperRoaPoints.length ? METRIC.resMax.paperName : '')}${chartSVG(E, E.paperRoaPoints, 'resMax')}` +
          `<details class="tview"><summary>Resource table</summary><div class="tbl-wrap">${resTable(E)}</div></details>`
        : '') +
      E.notes.map((t) => `<p class="hint">${t}</p>`).join('') + `</section>`;
  });
  $('#charts').innerHTML = html;
})();
