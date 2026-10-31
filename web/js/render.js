/* =============================================================
   LOA Hybrid — Result Renderers
   Everything drawn here comes from REPORT, the JSON produced by
   runHybrid() in cpp/src/hybrid/hybrid.cpp. RUNCFG is the config that
   was sent (used only for node names and coordinates).
   Globals provided: fmt, netSVG, renderResult, renderVis, renderTrace
   ============================================================= */

/** Format a number: null / Infinity → ∞, integers without decimals, else 1–2 d.p. */
const fmt = (x) =>
  x == null || x === Infinity
    ? '∞'
    : Math.abs(x - Math.round(x)) < 1e-9
      ? String(Math.round(x))
      : (Math.abs(x) < 10 ? x.toFixed(2) : x.toFixed(1));

const fmtMs = (x) => (x == null ? '—' : x < 1 ? x.toFixed(2) + ' ms' : x < 100 ? x.toFixed(1) + ' ms' : Math.round(x).toLocaleString() + ' ms');
const big   = (s) => (/^\d+$/.test(s) ? Number(s).toLocaleString() : s);
const esc   = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const arrow = (s) => esc(s).replace(/ -&gt; /g, ' → ').replace(/ \|\| /g, ' ∥ ');


// ─── Network SVG (same look as the old walkthrough) ───────────

function netSVG(c, o = {}) {
  const { under = '', over = '', use = null } = o;

  const links = c.links.map((l) => {
    const a = c.nodes[l.a], b = c.nodes[l.b];
    return `<line class="lk" x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}"/>`;
  }).join('');

  const labels = c.links.map((l) => {
    const a = c.nodes[l.a], b = c.nodes[l.b];
    return `<text class="lkl" x="${(a.x + b.x) / 2}" y="${(a.y + b.y) / 2 + 4}">${l.delay}</text>`;
  }).join('');

  const nodes = c.nodes.map((n, i) => {
    const u    = use ? use[i] : 0;
    const bad  = use && u > n.cap + 1e-9;
    const role = i === c.src && i === c.dst ? 'ingress and egress'
               : i === c.src ? 'ingress'
               : i === c.dst ? 'egress'
               : '';
    return `<g class="nd${bad ? ' over' : ''}" data-i="${i}" transform="translate(${n.x} ${n.y})">` +
      `<circle r="19"/>` +
      `<text class="nn">${n.name}</text>` +
      (use
        ? `<text class="use${bad ? ' bad' : ''}" y="35">${fmt(u)}/${n.cap}</text>`
        : `<text class="cp" y="35">C ${n.cap}</text>`) +
      (role ? `<text class="role" y="47">${role}</text>` : '') +
      `</g>`;
  }).join('');

  return `${links}${under}${labels}${nodes}${over}`;
}

/** Route polylines from the C++ scheme.routes list. */
function routeLines(c, routes, crit) {
  let s = '';
  for (const r of routes) {
    if (r.crit !== crit) continue;
    for (let j = 0; j + 1 < r.nodes.length; j++) {
      const a = c.nodes[r.nodes[j]], b = c.nodes[r.nodes[j + 1]];
      s += `<line class="rt ${crit ? 'crit' : 'sch'}" x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}"/>`;
    }
  }
  return s;
}

/** VNF name badges stacked above their host node. */
function badges(c, sc) {
  const by = new Map();
  sc.hosts.forEach((h, f) => {
    if (!by.has(h)) by.set(h, []);
    by.get(h).push({ nm: RUNCFG.vnfs[f].name + (sc.reuse && sc.reuse[f] ? ' ↺' : ''), reu: !!(sc.reuse && sc.reuse[f]) });
  });
  let s = '';
  by.forEach((names, h) => {
    const n = c.nodes[h];
    names.forEach(({ nm, reu }, i) => {
      const w = nm.length * 7 + 14;
      s += `<g class="bdg ${reu ? 'reu' : 'sch'}" transform="translate(${n.x} ${n.y - 32 - i * 18})">` +
           `<rect x="${-w / 2}" y="-8" width="${w}" height="16" rx="8"/><text>${nm}</text></g>`;
    });
  });
  return s;
}

/** Scheme drawn on the network. */
function schemeNet(sc) {
  const c = RUNCFG;
  const under = routeLines(c, sc.routes, false) + routeLines(c, sc.routes, true);
  return `<svg viewBox="0 0 600 340" role="img" aria-label="Placement on the MEC network">${netSVG(c, { under, over: badges(c, sc), use: sc.use })}</svg>`;
}

/**
 * Execution structure (I-SFG) left to right, critical chain in green.
 * Columns, rows, edges and the critical flags all come from schemeJson() in model.cpp.
 */
function schemeDiagram(sc) {
  const D = sc.dag, T = D.cols;
  const colW = 104, bw = 80, bh = 38, gap = 10;
  const maxN = Math.max(1, ...D.rows);
  const H  = maxN * (bh + gap) + 10, W = (T + 2) * colW;
  const xc = (j) => colW * (j + 0.5);
  const yc = (i, len) => H / 2 + (i - (len - 1) / 2) * (bh + gap);
  const pos = new Map();
  pos.set(-1, { x: xc(0), y: H / 2, box: false });
  pos.set(-2, { x: xc(T + 1), y: H / 2, box: false });
  D.nodes.forEach((n) => pos.set(n.f, { x: xc(n.col), y: yc(n.row, D.rows[n.col - 1]), box: true }));

  let ed = '', edc = '';
  D.edges.forEach((e) => {
    const a = pos.get(e.a), b = pos.get(e.b);
    const x1 = a.x + (a.box ? bw / 2 : 12), x2 = b.x - (b.box ? bw / 2 : 12), xm = (x1 + x2) / 2;
    const d  = `M${x1} ${a.y} C${xm} ${a.y} ${xm} ${b.y} ${x2} ${b.y}`;
    if (e.crit) edc += `<path class="ed cr" d="${d}"/>`; else ed += `<path class="ed" d="${d}"/>`;
  });

  let bx = '';
  D.nodes.forEach((n) => {
    const p = pos.get(n.f);
    bx += `<rect class="bx${n.crit ? ' cr' : ''}" x="${p.x - bw / 2}" y="${p.y - bh / 2}" width="${bw}" height="${bh}" rx="7"/>` +
          `<text class="bt" x="${p.x}" y="${p.y - 3}">${esc(n.name)}</text>` +
          `<text class="bs" x="${p.x}" y="${p.y + 11}">${esc(n.host)} · ${fmt(n.proc)} ms</text>`;
  });

  const ends = `<circle class="ep2" cx="${xc(0)}" cy="${H / 2}" r="12"/><text class="ept" x="${xc(0)}" y="${H / 2}">${esc(D.src)}</text>` +
               `<circle class="ep2" cx="${xc(T + 1)}" cy="${H / 2}" r="12"/><text class="ept" x="${xc(T + 1)}" y="${H / 2}">${esc(D.dst)}</text>`;

  return `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Execution structure (I-SFG)">${ed}${edc}${bx}${ends}</svg>`;
}


// ─── Certificate ──────────────────────────────────────────────

const CERT = {
  OPTIMAL:        { cls: 'ok',   title: 'Optimal',            text: 'The full-combination search covered every I-SFG and every placement (most of them cut by provable bounds) and found nothing better. This is the optimum of the whole problem, dividing and merging costs included.' },
  OPTIMAL_PROVEN: { cls: 'ok',   title: 'Optimal (proven)',   text: 'The full search ran out of its time budget, but paper 1’s LOA stopped on its own bound with no limit hit, so Lemmas 4.3 and 4.4 prove this scheme optimal.' },
  BOUNDED:        { cls: 'warn', title: 'Bounded',            text: 'The full search ran out of its time budget and no proof is available. The answer is the best scheme any method found.' },
  INFEASIBLE:     { cls: 'bad',  title: 'No feasible scheme', text: 'The full search proved that no placement satisfies the capacities, bandwidth, T_max and reliability requirement.' },
  UNKNOWN:        { cls: 'bad',  title: 'No scheme found',    text: 'No method found a scheme before its limit. Raise the full-search time budget, kmax or the candidate limit.' },
};

/** "+4 ms (5.9 %) above the optimum" for one paper-1 run (numbers from gapJson() in hybrid.cpp). */
function gapLine(name, g, unitFmt) {
  if (!g) return '';
  const ref = g.againstOptimum ? 'the optimum' : 'the best scheme found';
  const txt = !g.found ? 'found no feasible scheme'
            : g.matches ? `reached ${ref}`
            : `${unitFmt(g.cost)}, ${unitFmt(g.gap)} (${g.gapPct.toFixed(1)} %) above ${ref}`;
  return `<li><b>${name}:</b> ${txt}</li>`;
}

/** Certificate panel for latency (LOA side) or resources (ROA side). */
function certHTML(ce, which) {
  const info = CERT[ce.label] || CERT.UNKNOWN;
  const b  = ce.bound || {}, g = ce.gaps || {};
  const ms = (x) => fmt(x) + ' ms', rc = (x) => x.toFixed(3);
  let h = `<div class="ph"><span class="pill">${which === 'roa' ? 'Least resources' : 'Lowest latency'}</span>` +
          `<span class="tag cert-${info.cls}">${ce.label.replace(/_/g, ' ')}</span></div>` +
          `<h3>${which === 'roa' ? 'Least resources: ' : 'Lowest latency: '}${info.title.toLowerCase()}</h3><div class="body">${info.text}</div>`;
  if (ce.reasons.length)
    h += `<ul class="reasons">${ce.reasons.map((r) => `<li>${esc(r)}</li>`).join('')}</ul>`;
  const gl = which === 'roa'
    ? gapLine('ROA alone (paper 1)', g.roaPlain, rc) + gapLine('VDA-seeded ROA', g.roaSeeded, rc)
    : gapLine('LOA alone (paper 1)', g.loaPlain, ms) + gapLine('VDA-seeded LOA', g.loaSeeded, ms);
  if (gl) h += `<div class="watch"><b>Paper 1’s anchor-based search vs the full search</b><ul class="reasons">${gl}</ul></div>`;
  if (b.available)
    h += `<div class="watch"><b>Theorem 1 (paper 2):</b> T′/T ≤ γ·|Q|/|Φ| = ${fmt(b.gamma)} · ${b.Q}/${b.Phi} = <b>${fmt(b.ratio)}</b>` +
         ` with γ = l<sub>max</sub>/l<sub>min</sub> = ${fmt(b.lmax)}/${fmt(b.lmin)}. ${esc(b.note)}</div>`;
  if (which === 'roa') {
    const m = REPORT.model;
    h += `<div class="watch">Resource cost = α·C<sub>bw</sub>/C<sub>bw</sub><sup>ref</sup> + β·C<sub>cpu</sub>/C<sub>cpu</sub><sup>ref</sup> with α = ${fmt(m.wBw)}, β = ${fmt(m.wCpu)}, ` +
         `C<sub>bw</sub><sup>ref</sup> = ${fmt(m.cbwMax)} Mbps, C<sub>cpu</sub><sup>ref</sup> = ${fmt(m.ccpuMax)} units. Routed on fewest-hop paths.</div>`;
  }
  return h;
}


// ─── Final scheme cards ───────────────────────────────────────

function resLine(s) {
  const r = s.res || {};
  return `<div class="cand">` +
    `<div class="c"><span>Bandwidth</span><b>${fmt(r.bw)} Mbps</b></div>` +
    `<div class="c"><span>Links used</span><b>${r.links}</b></div>` +
    `<div class="c"><span>New CPU</span><b>${fmt(r.cpu)}</b></div>` +
    `<div class="c"><span>Instances reused / new</span><b>${r.reused} / ${r.created}</b></div>` +
    `<div class="c"><span>Resource cost</span><b>${(r.norm ?? 0).toFixed(3)}</b></div></div>`;
}

function lmHTML(s, which) {
  const title = which === 'roa' ? 'Least-resource scheme (RM-PDS)' : 'Lowest-latency scheme (LM-PDS)';
  if (!s || !s.ok) return `<h2>${title}</h2><div class="big" style="color:var(--ink3)">∞</div><p class="muted" style="margin-top:8px">No feasible scheme.</p>`;
  const b = s.br, tot = s.lat || 1;
  const seg = (v, c) => `<i class="${c}" style="width:${(v / tot * 100).toFixed(2)}%"></i>`;
  const head = which === 'roa'
    ? `<div class="big">${(s.res.norm).toFixed(3)}<small>resource cost · ${fmt(s.lat)} ms</small></div>`
    : `<div class="big">${fmt(s.lat)}<small>ms</small></div>`;
  return `<h2>${title} <small>source: ${esc(s.source)}</small></h2>${head}
    <div class="sch">${arrow(s.text)}</div>
    <div class="bar">${seg(b.trans, 's1')}${seg(b.proc, 's2')}${seg(b.rep, 's3')}${seg(b.int, 's4')}</div>
    <div class="bl"><span><i style="background:var(--brand)"></i>links ${fmt(b.trans)}</span><span><i style="background:var(--teal)"></i>processing ${fmt(b.proc)}</span><span><i style="background:var(--pnew)"></i>dividing ${fmt(b.rep)}</span><span><i style="background:var(--conf)"></i>merging ${fmt(b.int)}</span></div>
    ${resLine(s)}
    <div class="ex">${schemeDiagram(s)}</div>`;
}


// ─── Flow (phases reported by hybrid.cpp) ─────────────────────

function flowHTML() {
  return `<ol class="flow">` + REPORT.phases.map((p, i) =>
    `<li><span class="fn">${i + 1}</span><div><b>${esc(p.title)}</b>` +
    `<span class="ftime">${fmtMs(p.ms)}</span>` +
    `<div class="ffile"><code>${esc(p.file)}</code> · <code>${esc(p.fn)}</code></div>` +
    `<div class="fsum">${esc(p.summary)}</div></div></li>`
  ).join('') + `</ol>`;
}


// ─── Comparison table (rows built by runHybrid() in hybrid.cpp) ──

const LABEL = (l) => (CERT[l] ? CERT[l].title.toLowerCase() : l.toLowerCase());

function cmpHTML() {
  const rowHTML = (r) => {
    const d = r.delta == null ? '' : Math.abs(r.delta) < 1e-9 ? '<span class="chip ok">= best</span>' : `<span class="chip confl">+${fmt(r.delta)}</span>`;
    const res = r.ok ? `<td class="pc">${fmt(r.bw)}</td><td class="pc">${fmt(r.cpu)}</td><td class="pc">${r.norm.toFixed(3)}</td>` : '<td></td><td></td><td></td>';
    const paper = r.kind === 'anchor' ? LABEL(r.paper) : esc(r.paper);
    return `<tr class="${r.kind}"><td><b>${esc(r.method)}</b><div class="muted sm">${paper}</div></td>` +
      `<td class="pc">${r.ok ? fmt(r.lat) : '—'}</td><td>${d}</td>${res}` +
      `<td class="hop">${r.text ? arrow(r.text) : ''}</td><td class="sm">${esc(r.note || '')}</td><td class="pc">${fmtMs(r.ms)}</td></tr>`;
  };
  return `<table class="pt cmp"><thead><tr><th>Method</th><th>Latency (ms)</th><th></th><th>Bandwidth</th><th>New CPU</th><th>Res. cost</th><th>Scheme</th><th>Notes</th><th>C++ time</th></tr></thead><tbody>` +
    REPORT.comparison.map(rowHTML).join('') +
    `</tbody></table><p class="hint">“+” = extra latency compared with the fastest final scheme. Bandwidth = b<sub>u</sub> × distinct links; New CPU = demand of newly created instances; Res. cost = α·bandwidth + β·CPU, normalised.</p>`;
}


// ─── Full-combination search (fullJson() in full.cpp) ─────────

function fullCard(F, title, costFmt) {
  const S = F.stats;
  const cell = (l, v) => `<div class="c"><span>${l}</span><b>${v}</b></div>`;
  let h = `<h3 class="subh">${title} <small>${F.complete ? 'complete: the optimum' : 'stopped by the time budget: best found'}</small></h3>` +
    `<div class="cand">` +
    cell('Search space', `${big(fmt(S.space))} schemes`) +
    cell('I-SFGs searched', `${fmt(S.isfgSearched)} / ${fmt(S.isfgTotal)}`) +
    cell('Partial placements', big(fmt(S.nodes))) +
    cell('Complete schemes evaluated', big(fmt(S.leaves))) +
    cell('Cut by the bound', big(fmt(S.cutBound))) +
    cell('Cut by capacity / reliability / T<sub>max</sub>', big(fmt(S.cutFeasible))) +
    cell('Time', `${fmtMs(S.ms)} of ${fmtMs(S.budgetMs)}`) +
    `</div>`;
  h += F.trace.length
    ? `<table class="pt"><thead><tr><th>At</th><th>New best</th><th>Scheme</th></tr></thead><tbody>` +
      F.trace.map((t) => `<tr><td class="pc">${fmtMs(t.ms)}</td><td class="pc">${costFmt(t.cost)}</td><td class="hop">${arrow(t.text)}</td></tr>`).join('') + `</tbody></table>`
    : `<p class="hint">Nothing beat the starting best (${esc(F.scheme.source || 'none')}): the search proved it optimal.</p>`;
  return h;
}

function fullHTML() {
  const R = REPORT;
  if (!R.full) return `<p class="muted">The full-combination search is switched off in the setup.</p>`;
  return fullCard(R.full, 'Lowest latency', (x) => fmt(x) + ' ms') +
         (R.fullRes ? fullCard(R.fullRes, 'Least resources', (x) => x.toFixed(3)) : '') +
         `<p class="hint">Starts from the best of VDA and LOA / ROA. I-SFGs are taken in order of placement-free delay + d(s<sub>u</sub>, d<sub>u</sub>); a partial placement is cut when completion + placement-free tail + d(host, d<sub>u</sub>) cannot beat the best.</p>`;
}


// ─── Seed effect (Solution A) ─────────────────────────────────

function seedTable(g, name, seed, unitFmt) {
  const f = (r, x) => (r.ms ? fmtMs(x) : x.toLocaleString());
  let h = `<table class="pt"><thead><tr><th>${name}</th><th>∞ start</th><th>seeded</th></tr></thead><tbody>` +
    g.rows.map((r) => `<tr><td>${r.label}</td><td class="pc">${f(r, r.plain)}</td><td class="pc ${r.better ? 'good' : r.worse ? 'badv' : ''}">${f(r, r.seeded)}</td></tr>`).join('') +
    `</tbody></table>`;
  h += `<p class="hint">${g.sameCost ? '✓ Both runs reach the same cost.' : '<b class="err">The two runs end at different costs (a search limit was hit).</b>'} ` +
       (seed ? `The seed started ${name} at ${unitFmt(seed)}. ` : 'VDA found no seed. ') +
       (g.shortened ? `It let ${name} prune earlier.` : 'Here it did not shorten the search.') + `</p>`;
  return h;
}

function seedHTML() {
  const R = REPORT;
  let h = '';
  if (R.seedEffect) h += seedTable(R.seedEffect, 'LOA', R.seed, (s) => fmt(s.lat) + ' ms');
  if (R.seedEffectRes) h += seedTable(R.seedEffectRes, 'ROA', R.seedRes, (s) => 'resource cost ' + s.res.norm.toFixed(3));
  return h || `<p class="muted">Turn on both “Seed LOA with VDA” and “Also run unseeded LOA / ROA” to compare.</p>`;
}


// ─── Search trace ─────────────────────────────────────────────

function renderTrace() {
  const R = REPORT;
  const opts = [
    ['seeded', 'LOA seeded', R.loaSeeded], ['plain', 'LOA', R.loaPlain],
    ['roaSeeded', 'ROA seeded', R.roaSeeded], ['roaPlain', 'ROA', R.roaPlain],
  ].filter((o) => o[2]);
  if (!opts.some((o) => o[0] === V.trace)) V.trace = opts[0][0];
  $('#traceTabs').innerHTML = opts.map((o) => `<button data-act="trace" data-v="${o[0]}" aria-pressed="${V.trace === o[0]}">${o[1]}</button>`).join('');
  const L = opts.find((o) => o[0] === V.trace)[2];
  const bestFmt = L.objective === 'resource' ? (x) => (x == null ? '∞' : x.toFixed(3)) : fmt;
  $('#trace').innerHTML = `<table class="pt"><thead><tr><th>#</th><th>Round</th><th>k</th><th>Phase</th><th>Best</th><th>Event</th></tr></thead><tbody>` +
    L.trace.map((t, i) =>
      `<tr class="ph-${t.phase}"><td class="id">${i + 1}</td><td>${t.round}</td><td>${t.k}</td>` +
      `<td><span class="chip ${phaseChip(t.phase)}">${t.phase}</span></td><td class="pc">${bestFmt(t.best)}</td><td class="sm">${arrow(t.text)}</td></tr>`
    ).join('') + `</tbody></table>`;
}

const phaseChip = (p) => ({ update: 'inA', prune: 'anchor', done: 'astar', coverage: 'confl', exhaust: 'confl', init: 'pnew' }[p] || 'infeas');


// ─── Visualisation tabs ───────────────────────────────────────

function visOptions() {
  const R = REPORT, o = [];
  if (R.final.ok)                         o.push({ v: 'final', label: 'Lowest latency',  s: R.final,          cap: 'Lowest-latency certified scheme.' });
  if (R.finalRes && R.finalRes.ok)        o.push({ v: 'roa',   label: 'Least resources', s: R.finalRes,       cap: 'Least-resource certified scheme (fewest-hop routes; teal badges = shared instances).' });
  if (R.seed)                             o.push({ v: 'seed',  label: 'VDA seed',      s: R.seed,             cap: 'Paper 2 answer used as LOA\'s starting best.' });
  if (R.loaPlain && R.loaPlain.scheme.ok) o.push({ v: 'loa',   label: 'LOA unseeded',  s: R.loaPlain.scheme,  cap: 'Paper 1 on its own.' });
  return o;
}

function renderVis() {
  const o = visOptions();
  if (!o.length) { $('#tabs').innerHTML = ''; $('#vis').innerHTML = '<p class="muted">No feasible scheme to draw.</p>'; return; }
  if (!o.some((x) => x.v === V.tab)) V.tab = o[0].v;
  $('#tabs').innerHTML = o.map((x) => `<button data-act="tab" data-v="${x.v}" aria-pressed="${x.v === V.tab}">${x.label}</button>`).join('');
  const cur = o.find((x) => x.v === V.tab);
  $('#vis').innerHTML = schemeNet(cur.s) +
    `<div class="cap">${cur.cap} ${fmt(cur.s.lat)} ms: ${arrow(cur.s.text)}. The thick green line is the critical chain that sets the latency.</div>`;
}


// ─── Merge synchronisation (syncJson() in sync.cpp) ───────────

function syncHTML() {
  const Y = REPORT.sync;
  if (!Y.merges.length) return `<p class="hint">This scheme has no merge, so nothing waits in a buffer.</p>`;
  const cell = (l, v) => `<div class="c"><span>${l}</span><b>${v}</b></div>`;
  const pair = (a, b, u) => `${fmt(a)} → ${fmt(b)}${u}`;
  let h = `<div class="cand">` +
    cell('Largest skew', pair(Y.maxSkewBefore, Y.maxSkewAfter, ' ms')) +
    cell('Largest buffer', pair(Y.maxBufBefore, Y.maxBufAfter, ' kbit')) +
    cell('Latency', pair(Y.latBefore, Y.latAfter, ' ms')) +
    cell('CPU of new instances', pair(Y.cpuBefore, Y.cpuAfter, '')) +
    (Y.bufMax > 0 ? cell(`Merges over ${fmt(Y.bufMax)} kbit`, pair(Y.overflowBefore, Y.overflowAfter, '')) : '') +
    `</div>`;
  h += `<table class="pt"><thead><tr><th>Merge</th><th>Node</th><th>Branches</th><th>Skew (ms)</th><th>Buffer (kbit)</th></tr></thead><tbody>` +
    Y.merges.map((m) => `<tr><td>${esc(m.at)}</td><td>${esc(m.node)}</td><td class="pc">${m.branches}</td>` +
      `<td class="pc">${pair(m.skewBefore, m.skewAfter, '')}</td><td class="pc">${pair(m.bufBefore, m.bufAfter, '')}</td></tr>`).join('') +
    `</tbody></table>`;
  h += `<table class="pt"><thead><tr><th>VNF</th><th>CPU speed</th><th>Processing (ms)</th><th>Note</th></tr></thead><tbody>` +
    Y.vnfs.map((v) => `<tr><td>${esc(v.name)}</td><td class="pc">${v.speed.toFixed(2)}</td>` +
      `<td class="pc">${pair(v.procBefore, v.procAfter, '')}</td><td>${v.shared ? 'shared instance, speed fixed' : ''}</td></tr>`).join('') +
    `</tbody></table>`;
  return h;
}

// ─── Whole result view ────────────────────────────────────────

const stat = (l, v, cls = '') => `<div class="stat ${cls}"><span>${l}</span><b>${v}</b></div>`;

function renderResult() {
  const R = REPORT, m = R.model;
  $('#runInfo').textContent = `C++ engine · ${fmtMs(R.totalMs)}`;
  $('#stats').innerHTML =
    stat('Lowest latency', R.final.ok ? fmt(R.final.lat) + ' ms' : '∞', 'best') +
    (R.finalRes ? stat('Least resources', R.finalRes.ok ? R.finalRes.res.norm.toFixed(3) : '∞', 'lb') : '') +
    stat('Certificate', (CERT[R.certificate.label] || CERT.UNKNOWN).title) +
    stat('I-SFGs', big(m.space.structures)) +
    stat('Schemes (I-SFGs × placements)', big(m.space.total)) +
    stat('DPG paths (paper 1)', big(m.totalPaths)) +
    stat('Layers', m.layers.map((l) => l.join(' = ')).join(' ≻ '));
  const ce = CERT[R.certificate.label] || CERT.UNKNOWN;
  $('#cert').className = `panel narr cert-${ce.cls}`;
  $('#cert').innerHTML = certHTML(R.certificate, 'loa');
  $('#lm').innerHTML   = lmHTML(R.final, 'loa');
  $('#roaGrid').hidden = !R.finalRes;
  if (R.finalRes) {
    const cr = CERT[R.certificateRes.label] || CERT.UNKNOWN;
    $('#certRes').className = `panel narr cert-${cr.cls}`;
    $('#certRes').innerHTML = certHTML(R.certificateRes, 'roa');
    $('#lmRes').innerHTML   = lmHTML(R.finalRes, 'roa');
  }
  $('#flow').innerHTML = flowHTML();
  $('#full').innerHTML = fullHTML();
  $('#syncPanel').hidden = !(R.sync && R.sync.ok);
  if (R.sync && R.sync.ok) $('#sync').innerHTML = syncHTML();
  $('#cmp').innerHTML  = cmpHTML();
  $('#seedFx').innerHTML = seedHTML();
  $('#raw').textContent = JSON.stringify(R, null, 2);
  renderVis();
  renderTrace();
}
