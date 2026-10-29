/* =============================================================
   LOA Hybrid — Bridge to the C++ engine
   The page is served by loa_hybrid.exe itself, so the API is on
   the same origin. No algorithm runs in the browser.
   Globals provided: checkEngine, runEngine, checkConfig
   ============================================================= */

/** Ask the C++ server whether it is up. Resolves to { ok, engine } or { ok:false, error }. */
async function checkEngine() {
  try {
    const r = await fetch('/api/health', { cache: 'no-store' });
    if (!r.ok) return { ok: false, error: `HTTP ${r.status}` };
    return await r.json();
  } catch (e) {
    return { ok: false, error: 'C++ server not reachable. Start it with cpp\\build.bat, then open http://localhost:8080' };
  }
}

/**
 * Send the config to the C++ engine (POST /api/run) and return its report.
 * The report is exactly what runHybrid() in cpp/src/hybrid/hybrid.cpp produced.
 */
async function runEngine(cfg) {
  let r;
  try {
    r = await fetch('/api/run', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg),
    });
  } catch (e) {
    return { ok: false, error: 'C++ server not reachable. Is loa_hybrid.exe still running?' };
  }
  try {
    return await r.json();
  } catch (e) {
    return { ok: false, error: `Bad reply from the C++ server (HTTP ${r.status}).` };
  }
}

/**
 * Ask the C++ engine to validate a config and size its search space (POST /api/check).
 * Nothing is solved; the reply is checkConfig() in cpp/src/hybrid/hybrid.cpp.
 */
async function checkConfig(cfg) {
  try {
    const r = await fetch('/api/check', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfg) });
    return await r.json();
  } catch (e) {
    return null;   // engine offline: the header already says so
  }
}
