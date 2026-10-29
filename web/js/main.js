/* =============================================================
   LOA Hybrid — Global State, View Switching & Entry Point
   Provides globals: cfg, presetIdx, REPORT, RUNCFG, stale, V, ENGINE,
                     showView
   ============================================================= */

// ─── Global State ─────────────────────────────────────────────

/** Current user-editable configuration. Initialised from the first preset. */
let cfg       = JSON.parse(JSON.stringify(PRESETS[0].cfg));
let presetIdx = 0;     // index of the active preset (-1 = custom)
let REPORT    = null;  // last report from the C++ engine
let RUNCFG    = null;  // config that produced REPORT
let stale     = false; // cfg changed after last run

/** Engine status from /api/health (ok: null while unknown). */
const ENGINE = { ok: null };

/**
 * Result view state.
 *  tab   – network tab: 'final' | 'roa' | 'seed' | 'loa'
 *  trace – trace tab: 'seeded' | 'plain'
 */
const V = { tab: 'final', trace: 'seeded' };


// ─── View switching ───────────────────────────────────────────

function showView(which) {
  const res = which === 'result' && REPORT;
  document.getElementById('setup').hidden  = !!res;
  document.getElementById('result').hidden = !res;
  document.getElementById('modeSetup').setAttribute('aria-current', res ? 'false' : 'true');
  document.getElementById('modeResult').setAttribute('aria-current', res ? 'true' : 'false');
  window.scrollTo(0, 0);
}


// ─── Bootstrap ────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', async () => {
  renderPresets();
  renderForms();

  document.getElementById('modeSetup').addEventListener('click', () => showView('setup'));
  document.getElementById('modeResult').addEventListener('click', () => showView('result'));

  const h  = await checkEngine();
  const el = document.getElementById('engine');
  ENGINE.ok = !!h.ok;
  el.dataset.state = h.ok ? 'ok' : 'bad';
  el.textContent   = h.ok ? 'C++ engine connected' : 'C++ engine offline';
  el.title         = h.ok ? h.engine : h.error;
  if (!h.ok) {
    document.getElementById('runMsg').textContent = h.error;
    document.getElementById('runMsg').className = 'runmsg err';
  }
  refreshSetup();
  if (h.ok) scheduleCheck();
});
