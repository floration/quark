/* ===== BOOT ==========================================================================================
   Load data/manifest.js (the models from the game's export, the map palettes), restore the autosave, start WebGL,
   the UI and the frame loop. Q.shot() renders a fixed-size picture of a view (the comparisons with the game). */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, S = Q.scene, R = Q.R, h = U.h;

  async function restore() {
    const s = await Q.loadAutosave();
    if (!s || !s.state) return;
    for (const [id, src] of Object.entries(s.sources || {})) Q.sources[id] = { name: src.name, bytes: src.bytes };
    Q.state = Object.assign({ edits: {}, prov: {} }, s.state);
    for (const [k, e] of Object.entries(Q.state.edits)) if (!Q.models[k] || (e.type === 'mesh' && !Q.sources[e.src])) delete Q.state.edits[k];
    Q.normalizeEdits(); Q.dropStaleProv();
    for (const k of Object.keys(Q.state.edits)) Q.bump(k);
    const n = Object.keys(Q.state.edits).length;
    if (n) U.banner('Restored your last session (' + n + ' edited model' + (n > 1 ? 's' : '') + '), saved ' + new Date(s.saved).toLocaleString() + '.', false,
      [h('button.mini', { type: 'button', onclick: async () => { Q.state = { edits: {}, prov: {} }; Q.sources = {}; Q.clearHistory(); await Q.clearAutosave(); U.$('#banner').hidden = true; U.afterLoad(); } }, 'start fresh instead')]);
  }

  // a picture of a view at w x h: opts { mode, key, yaw, pitch, palette, team, weapon, w, h, ... any S.view field,
  //   cam: { pos: [x, y, z], yaw, pitch } (degrees: the game's --cam, relative to the model / the cart),
  //   only: [keys] (draw only these models) }
  Q.shot = function (opts) {
    opts = opts || {};
    const save = Object.assign({}, S.view), saveSel = S.sel, saveTime = S.time;
    for (const [k, v] of Object.entries(opts)) if (k in S.view) S.view[k] = v;
    if (opts.weapon) S.view.fpWeapon = opts.weapon;
    if (opts.key) S.sel = opts.key;
    if (opts.time != null) S.time = opts.time;
    S.camOverride = opts.cam ? { pos: opts.cam.pos, yaw: opts.cam.yaw * Math.PI / 180, pitch: opts.cam.pitch * Math.PI / 180 } : null;
    S.build(); if (opts.frame) { S.frame(S.sel); S.build(); }
    if (opts.only) for (const it of S.current.items) it.hidden = opts.only.indexOf(it.key) < 0;   // (comparisons: these models alone)
    S.updateOverlay();
    const w = opts.w || 1280, hh = opts.h || 720, t = R.cachedTarget(w, hh), cv = document.createElement('canvas');
    cv.width = w; cv.height = hh;
    R.draw(S.current, t);
    R.readTarget(t, cv.getContext('2d'));
    const url = cv.toDataURL('image/png');
    Object.assign(S.view, save); S.sel = saveSel; S.time = saveTime; S.camOverride = null; S.build(); S.updateOverlay();
    return url;
  };

  // without model data (or WebGL) nothing on the toolbars can work: they are disabled under the banner
  function disableTools() {
    document.querySelectorAll('#top button, #top select, #top input, #viewBar button, #viewBar select, #viewBar input, #lib input, #lib select').forEach(el => { el.disabled = true; });
    document.querySelectorAll('#top .menu, #viewBar .menu').forEach(m => m.classList.add('off'));
  }

  async function boot() {
    try {
      await new Promise((res, rej) => { Q.onManifest = res; Q.loadScript('data/manifest.js').catch(rej); });
    } catch (e) {
      U.banner('No model data next to this page (data/manifest.js). Build it: python tools/model_editor/build.py <export folder> <this folder>', true);
      disableTools(); Q.bootError = String(e); return;
    }
    U.$('#gameVersion').textContent = 'game ' + (Q.manifest.version || '?');
    Q.buildModels(Q.manifest);
    try { R.init(U.$('#gl')); } catch (e) { U.banner('WebGL2 is needed (Chrome or Edge): ' + e.message, true); disableTools(); Q.bootError = String(e); return; }
    await restore();
    U.init();
    U.renderLibrary();
    let sel = null; try { sel = localStorage.getItem('qme.sel'); } catch (e) { }
    U.select(Q.models[sel] ? sel : Q.order[0], true);
    const bad = Q.order.filter(k => Q.models[k].err), refused = Q.order.filter(k => Q.models[k].shipped && Q.models[k].shipped.err);
    if (bad.length) U.banner('Could not read ' + bad.join(', ') + ' from the export.', true);
    else if (refused.length) U.banner('The game refuses the shipped override' + (refused.length > 1 ? 's' : '') + ' of ' + refused.join(', ') +
      ' (assets/overrides) and draws the default instead: see the model panel.', true);
    U.syncUndo();
    Q.ready = true;
    requestAnimationFrame(U.loop);
  }
  boot().catch(e => { Q.bootError = String(e && e.stack || e); U.fail('start', e); });
})();
