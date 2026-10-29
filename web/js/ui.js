/* =============================================================
   LOA Hybrid — Setup UI & Event Delegation
   Reads globals : cfg, REPORT, presetIdx, stale, V  (set by main.js)
   Writes globals: cfg, presetIdx, stale, REPORT, RUNCFG
   Calls          : runEngine (api.js), renderResult, netSVG (render.js),
                    showView (main.js)
   ============================================================= */

// ─── Utility Helpers ─────────────────────────────────────────
const LETTERS = 'ABCDEFGHIJ';
const MAX_NODES = 10, MAX_VNFS = 10;

/** Query selector shorthand. */
const $ = (s, r = document) => r.querySelector(s);

/** Deep clone via JSON round-trip. */
const clone = (o) => JSON.parse(JSON.stringify(o));

/** Clamp x between a and b. */
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));

/** Parse a form value as a finite number (0 on failure). */
const numv = (v) => { const x = parseFloat(v); return isFinite(x) ? x : 0; };


// ─── Setup Helpers ────────────────────────────────────────────

/** Last validation / search-space reply from the C++ engine (POST /api/check). */
let CHECK = null, checkTimer = null, checkSeq = 0;

/** Ask C++ to re-check the setup shortly after the last edit. */
function scheduleCheck() {
  clearTimeout(checkTimer);
  checkTimer = setTimeout(async () => {
    const seq = ++checkSeq;
    const r = await checkConfig(cfg);
    if (seq !== checkSeq) return;   // a newer edit is already being checked
    CHECK = r;
    renderSummary();
  }, 200);
}

/** Set a nested property of cfg using a dotted path. */
function setBind(path, val) {
  const p = path.split('.');
  let o = cfg;
  for (let i = 0; i < p.length - 1; i++) o = o[p[i]];
  o[p[p.length - 1]] = val;
}

/** Build <option> elements for a node selector. */
const nodeOpts = (sel) =>
  cfg.nodes.map((n, i) => `<option value="${i}" ${i === sel ? 'selected' : ''}>${n.name}</option>`).join('');

/** One labelled number input bound to cfg[key]. */
const param = (label, key, step, min = 0, extra = '') =>
  `<label><span>${label}</span><input class="num" type="number" min="${min}" step="${step}" ${extra} value="${cfg[key]}" data-bind="${key}"></label>`;


// ─── Setup Renderers ──────────────────────────────────────────

/** Render the preset buttons. */
function renderPresets() {
  $('#presets').innerHTML = PRESETS.map((p, i) =>
    `<button class="preset" data-act="preset" data-i="${i}" aria-pressed="${i === presetIdx}">` +
    `<b>${p.label}</b><span>${p.blurb}</span></button>`
  ).join('');
}

/** Re-render all setup form controls. */
function renderForms() {
  // Auto-name nodes and VNFs
  cfg.nodes.forEach((n, i) => { n.name = LETTERS[i]; });
  cfg.vnfs.forEach((v, i)  => { v.name = 'f' + (i + 1); });

  $('#selSrc').innerHTML = nodeOpts(cfg.src);
  $('#selDst').innerHTML = nodeOpts(cfg.dst);

  // Node rows
  $('#nodeRows').innerHTML =
    `<div class="rowh rn3"><span>Node</span><span>Capacity C<sub>v</sub></span><span>Fault β<sub>v</sub></span><span>Running instances</span><span></span></div>` +
    cfg.nodes.map((n, i) =>
      `<div class="r rn3">` +
      `<span class="nm">${n.name}</span>` +
      `<input class="num" type="number" min="0" step="5" value="${n.cap}" data-bind="nodes.${i}.cap" aria-label="Capacity of ${n.name}">` +
      `<input class="num" type="number" min="0" max="1" step="0.01" value="${n.fault}" data-bind="nodes.${i}.fault" aria-label="Fault rate of ${n.name}">` +
      `<input class="num" type="text" placeholder="e.g. f2,f4" value="${n.inst || ''}" data-text="nodes.${i}.inst" aria-label="VNF instances already running on ${n.name}">` +
      `<button class="x" data-act="delNode" data-i="${i}" aria-label="Remove node ${n.name}" ${cfg.nodes.length <= 2 ? 'disabled' : ''}>×</button>` +
      `</div>`
    ).join('') +
    `<div class="addrow"><button class="btn sm" data-act="addNode" ${cfg.nodes.length >= MAX_NODES ? 'disabled' : ''}>Add node</button></div>`;

  // Link rows
  $('#linkRows').innerHTML =
    `<div class="rowh rl2"><span>Link</span><span>Delay (ms)</span><span>Bandwidth (Mbps)</span><span></span></div>` +
    cfg.links.map((l, i) =>
      `<div class="r rl2">` +
      `<span class="nm">${cfg.nodes[l.a].name} – ${cfg.nodes[l.b].name}</span>` +
      `<input class="num" type="number" min="1" step="1" value="${l.delay}" data-bind="links.${i}.delay" aria-label="Delay of link ${cfg.nodes[l.a].name}-${cfg.nodes[l.b].name}">` +
      `<input class="num" type="number" min="0" step="10" value="${l.bw}" data-bind="links.${i}.bw" aria-label="Bandwidth of link ${cfg.nodes[l.a].name}-${cfg.nodes[l.b].name}">` +
      `<button class="x" data-act="delLink" data-i="${i}" aria-label="Remove link">×</button>` +
      `</div>`
    ).join('');

  // Add-link row
  $('#linkAdd').innerHTML =
    `<select class="num" id="laA" aria-label="Link end 1">${nodeOpts(0)}</select>` +
    `<select class="num" id="laB" aria-label="Link end 2">${nodeOpts(1)}</select>` +
    `<input  class="num" id="laD" type="number" min="1" value="10" aria-label="Delay">` +
    `<input  class="num" id="laW" type="number" min="0" value="300" aria-label="Bandwidth">` +
    `<button class="btn sm" data-act="addLink">Add link</button>`;

  // VNF rows
  $('#vnfRows').innerHTML =
    `<div class="rowh rv2"><span>VNF</span><span>Level</span><span>Proc. (ms)</span><span>Demand c<sub>n</sub></span><span>Fault β<sub>f</sub></span><span></span></div>` +
    cfg.vnfs.map((v, i) =>
      `<div class="r rv2">` +
      `<span class="nm">${v.name}</span>` +
      `<input class="num" type="number" min="1" max="8" step="1" value="${v.level}" data-bind="vnfs.${i}.level" aria-label="Level of ${v.name}">` +
      `<input class="num" type="number" min="0" step="1" value="${v.proc}" data-bind="vnfs.${i}.proc" aria-label="Processing delay of ${v.name}">` +
      `<input class="num" type="number" min="0" step="5" value="${v.dem}" data-bind="vnfs.${i}.dem" aria-label="Demand of ${v.name}">` +
      `<input class="num" type="number" min="0" max="1" step="0.01" value="${v.fault}" data-bind="vnfs.${i}.fault" aria-label="Fault rate of ${v.name}">` +
      `<button class="x" data-act="delVnf" data-i="${i}" aria-label="Remove ${v.name}" ${cfg.vnfs.length <= 1 ? 'disabled' : ''}>×</button>` +
      `</div>`
    ).join('') ;
  $('[data-act="addVnf"]').disabled = cfg.vnfs.length >= MAX_VNFS;

  // Global params
  $('#params').innerHTML =
    param('Data size l<sub>u</sub> (units)', 'pkt', 0.5, 0.01) +
    param('Dividing τ<sub>rep</sub> (ms per unit, per extra branch)', 'trep', 1) +
    param('Merging τ<sub>int</sub> (ms per unit, per extra branch)', 'tint', 1) +
    param('Deadline T<sub>max</sub> (ms)', 'tmax', 10, 1) +
    `<label><span>Structures inside a level</span><select class="num" data-sel="isfg">` +
      `<option value="all" ${cfg.isfg !== 'staged' ? 'selected' : ''}>every I-SFG, e.g. (f2 → f3) ∥ f4 (paper 2)</option>` +
      `<option value="staged" ${cfg.isfg === 'staged' ? 'selected' : ''}>stage sequences only, e.g. (f2 ∥ f3) → f4</option>` +
    `</select></label>`;
  $('#relParams').innerHTML =
    param('Required R<sub>min</sub> (0 = off)', 'rmin', 0.01, 0, 'max="1"') +
    param('Sharing sensitivity α', 'alpha', 0.01);
  $('#resParams').innerHTML =
    param('Request bandwidth b<sub>u</sub> (Mbps)', 'breq', 10) +
    param('Load on a shared instance q<sub>u</sub>', 'qcpu', 5) +
    param('Instance capacity C<sub>n</sub><sup>max</sup>', 'instCap', 10) +
    param('Bandwidth weight α (β = 1 − α)', 'wBw', 0.1, 0, 'max="1"');
  $('#limParams').innerHTML =
    param('k limit (kmax)', 'kmax', 256, 1) +
    param('Candidates / anchor', 'candCap', 5000, 1) +
    param('Full-search time budget (ms)', 'fullMs', 1000, 0);
  $('#toggles').innerHTML =
    toggle('full',    'Full-combination search: every I-SFG × every placement') +
    toggle('seed',    'Seed LOA with VDA (Solution A)') +
    toggle('order',   'Try SCA-ranked arrangements first (Solution E)') +
    toggle('compare', 'Also run unseeded LOA / ROA for comparison') +
    toggle('roa',     'Run ROA too (paper 1, Algorithms 4 + 5)');

  refreshSetup();
}

/** One checkbox bound to a boolean cfg[key]. */
function toggle(key, label) {
  return `<label class="tg"><input type="checkbox" data-toggle="${key}" ${cfg[key] ? 'checked' : ''}> ${label}</label>`;
}

/** Update the network SVG and the summary without rebuilding all forms. */
function refreshSetup() {
  $('#netEdit').innerHTML = netSVG(cfg, { edit: true });
  renderSummary();
}

/** Summary panel: what the C++ engine said about the current setup. */
function renderSummary() {
  const C = CHECK;
  let h = '', sp = '';
  if (!C) h = `<div class="muted">Checking the setup in C++…</div>`;
  else {
    if (C.layers) h += `<div class="muted">Dependency order</div><div class="dep">${C.layers.map((l) => l.join(' = ')).join(' ≻ ')}</div>`;
    if (!C.ok) h += `<div class="err">${esc(C.error)}</div>`;
    else if (!C.connected) h += `<div class="err">The ingress and egress nodes are not connected (check link bandwidth against b<sub>u</sub>).</div>`;
    (C.warnings || []).forEach((w) => { h += `<div class="warn">${esc(w)}</div>`; });
    if (C.ok) sp = spaceTable(C.space);
  }
  if (stale) h += `<div class="warn">Setup changed since the last run. Run again to refresh the results.</div>`;
  $('#summary').innerHTML = h;
  $('#space').innerHTML = sp;
  $('#runBtn').disabled = !C || !C.ok || !C.connected || ENGINE.ok === false;
}

/** Search-space size reported by spaceJson() in model.cpp. */
function spaceTable(S) {
  return `<table class="pt"><thead><tr><th>Level</th><th>VNFs</th><th>I-SFGs</th><th>of which stage sequences</th></tr></thead><tbody>` +
    S.levels.map((l, i) => `<tr><td>${i + 1}</td><td class="pc">${l.vnfs}</td><td class="pc">${big(String(l.structures))}</td><td class="pc">${big(String(l.staged))}</td></tr>`).join('') +
    `</tbody></table><p class="hint">Full search space: ${big(S.structures)} I-SFGs × ${big(S.placements)} placements = <b>${big(S.total)}</b> schemes ` +
    `(${S.isfg === 'all' ? 'every I-SFG' : 'stage sequences only'}). Paper 1's DPG has ${big(S.dpgPaths)} paths.</p>`;
}

/** Called whenever the user edits a setup field. */
function edited() {
  cfg.vnfs.forEach((v) => { v.level = Math.max(1, Math.round(v.level) || 1); });
  if (presetIdx !== -1) { presetIdx = -1; renderPresets(); }
  if (REPORT) stale = true;
  refreshSetup();
  scheduleCheck();
}


// ─── Run (sends the form to C++) ──────────────────────────────

let running = false;

async function run() {
  if (running) return;
  if (CHECK && !CHECK.ok) { showView('setup'); return; }
  running = true;
  document.querySelectorAll('[data-act="run"]').forEach((b) => { b.disabled = true; });
  $('#runMsg').textContent = 'Running in C++…';
  $('#runMsg').className = 'runmsg';

  const sent = clone(cfg);
  const rep  = await runEngine(sent);

  running = false;
  document.querySelectorAll('[data-act="run"]').forEach((b) => { b.disabled = false; });
  if (!rep.ok) {
    $('#runMsg').textContent = rep.error || 'The C++ engine returned an error.';
    $('#runMsg').className = 'runmsg err';
    showView('setup');
    return;
  }
  $('#runMsg').textContent = '';
  REPORT = rep;
  RUNCFG = sent;
  stale  = false;
  V.tab = 'final'; V.trace = 'seeded';
  $('#modeResult').disabled = false;
  showView('result');
  renderResult();
  refreshSetup();
}


// ─── Network Editor Drag ──────────────────────────────────────
(function attachDrag() {
  let drag = null;

  document.addEventListener('pointerdown', (e) => {
    const nd = e.target.closest('.nd');
    if (!nd || !nd.closest('#netEdit')) return;
    const i   = +nd.dataset.i;
    const svg = nd.closest('svg');
    const pt  = svg.createSVGPoint();
    pt.x = e.clientX; pt.y = e.clientY;
    const sp = pt.matrixTransform(svg.getScreenCTM().inverse());
    drag = { i, ox: sp.x - cfg.nodes[i].x, oy: sp.y - cfg.nodes[i].y, svg };
    e.preventDefault();
  });

  document.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const pt = drag.svg.createSVGPoint();
    pt.x = e.clientX; pt.y = e.clientY;
    const sp = pt.matrixTransform(drag.svg.getScreenCTM().inverse());
    cfg.nodes[drag.i].x = clamp(Math.round(sp.x - drag.ox), 24, 576);
    cfg.nodes[drag.i].y = clamp(Math.round(sp.y - drag.oy), 24, 316);
    refreshSetup();
    e.preventDefault();
  });

  document.addEventListener('pointerup',     () => { drag = null; });
  document.addEventListener('pointercancel', () => { drag = null; });
})();


// ─── Central Event Delegation ─────────────────────────────────
document.addEventListener('click', (e) => {
  const el  = e.target.closest('[data-act]');
  if (!el) return;
  const act = el.dataset.act;

  // Setup actions
  if (act === 'preset') {
    const i = +el.dataset.i;
    presetIdx = i;
    cfg = clone(PRESETS[i].cfg);
    stale = !!REPORT;
    renderPresets();
    renderForms();
    scheduleCheck();
    return;
  }
  if (act === 'addNode') {
    const prev = cfg.nodes[cfg.nodes.length - 1];
    cfg.nodes.push({ name: '?', cap: 40, fault: 0, inst: '', x: Math.min(prev.x + 80, 560), y: prev.y });
    renderForms(); edited(); return;
  }
  if (act === 'delNode') {
    const i = +el.dataset.i;
    cfg.nodes.splice(i, 1);
    cfg.links = cfg.links.filter((l) => l.a !== i && l.b !== i)
      .map((l) => ({ ...l, a: l.a > i ? l.a - 1 : l.a, b: l.b > i ? l.b - 1 : l.b }));
    if (cfg.src >= cfg.nodes.length) cfg.src = 0;
    if (cfg.dst >= cfg.nodes.length) cfg.dst = cfg.nodes.length - 1;
    renderForms(); edited(); return;
  }
  if (act === 'addLink') {
    const a = +$('#laA').value, b = +$('#laB').value, delay = numv($('#laD').value) || 10, bw = numv($('#laW').value);
    if (a !== b && !cfg.links.some((l) => (l.a === a && l.b === b) || (l.a === b && l.b === a))) {
      cfg.links.push({ a, b, delay, bw });
      renderForms(); edited();
    }
    return;
  }
  if (act === 'delLink') {
    cfg.links.splice(+el.dataset.i, 1);
    renderForms(); edited(); return;
  }
  if (act === 'addVnf') {
    const maxLevel = Math.max(...cfg.vnfs.map((v) => v.level), 0);
    cfg.vnfs.push({ name: '?', level: maxLevel + 1, proc: 5, dem: 10, fault: 0 });
    renderForms(); edited(); return;
  }
  if (act === 'delVnf') {
    cfg.vnfs.splice(+el.dataset.i, 1);
    renderForms(); edited(); return;
  }

  if (act === 'run')     { run(); return; }
  if (act === 'toSetup') { showView('setup'); return; }

  if (!REPORT) return;

  // Result view tabs
  if (act === 'tab')   { V.tab = el.dataset.v;   renderVis();   return; }
  if (act === 'trace') { V.trace = el.dataset.v; renderTrace(); return; }
});

// ─── Input Change Delegation ──────────────────────────────────
document.addEventListener('change', (e) => {
  const el = e.target;

  if (el.id === 'selSrc') { cfg.src = +el.value; edited(); return; }
  if (el.id === 'selDst') { cfg.dst = +el.value; edited(); return; }
  if (el.dataset.toggle)  { cfg[el.dataset.toggle] = el.checked; edited(); return; }
  if (el.dataset.sel)     { cfg[el.dataset.sel] = el.value; edited(); return; }
  if (el.dataset.text)    { setBind(el.dataset.text, el.value.trim()); edited(); return; }
  if (el.dataset.bind)    { setBind(el.dataset.bind, numv(el.value)); edited(); }
});
